$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $PSScriptRoot 'vendor\emf_common.ps1')

function Write-InstallStatus([int]$Percent, [string]$Text) {
    Write-Host ("@@progress:{0}|{1}" -f $Percent, $Text)
}

function Get-SafeGamePath([string]$Root, [string]$Relative) {
    if (-not $Relative -or [IO.Path]::IsPathRooted($Relative)) { throw "Invalid relative path: $Relative" }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes game directory: $Relative" }
    $probe = $path
    while ($probe -and $probe.Length -ge $base.Length) {
        if (Test-Path -LiteralPath $probe) {
            if ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked install paths are not supported: $probe"
            }
        }
        if ($probe -eq $base) { break }
        $probe = [IO.Path]::GetDirectoryName($probe)
    }
    return $path
}

function Get-InstallerManifest([string]$Package) {
    $deps = [IO.File]::ReadAllText((Join-Path $Package 'backend\dependencies.json')) | ConvertFrom-Json
    $mod = [IO.File]::ReadAllText((Join-Path $Package 'backend\mod-manifest.json')) | ConvertFrom-Json
    $hashes = @($deps.game_sha256, $deps.eml_dll.sha256, $deps.eml_cli.sha256, $deps.emf_zip.sha256) + @($deps.emf_files | ForEach-Object { $_.sha256 })
    foreach ($hash in $hashes) { if ($hash -notmatch '^[a-f0-9]{64}$') { throw '설치 구성의 SHA-256 값이 올바르지 않습니다.' } }
    foreach ($entry in $mod.files) {
        $source = Get-SafeGamePath (Join-Path $Package 'mod') $entry.path
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Sha256Hex $source) -ne $entry.sha256) {
            throw "설치 파일이 손상됐습니다: $($entry.path)"
        }
    }
    return @{ Dependencies = $deps; Mod = $mod }
}

function Get-InstallPlan([string]$Game, [string]$Package) {
    $root = [IO.Path]::GetFullPath($Game).TrimEnd('\')
    foreach ($name in @('enshrouded.exe', 'enshrouded.kfc', 'enshrouded.kfc_resources')) {
        $at = Get-SafeGamePath $root $name
        if (-not (Test-Path -LiteralPath $at -PathType Leaf)) { throw "Enshrouded 게임 폴더를 선택해 주세요. 파일 없음: $name" }
    }
    $manifest = Get-InstallerManifest $Package
    if ((Get-Sha256Hex (Join-Path $root 'enshrouded.exe')) -ne $manifest.Dependencies.game_sha256) {
        throw '지원하지 않는 게임 버전입니다. 이 설치 파일은 클라이언트 빌드 1283531용입니다.'
    }
    $running = Get-GameRunning $root
    if ($running) { throw "게임과 모드 매니저를 종료한 뒤 다시 설치해 주세요. $running" }
    $core = Join-Path $root 'emf\emf_core.dll'
    $coreSpec = @($manifest.Dependencies.emf_files | Where-Object { $_.path -eq 'emf/emf_core.dll' })[0]
    if ((Test-Path -LiteralPath $core) -and (Get-Sha256Hex $core) -ne $coreSpec.sha256) {
        throw '다른 버전의 EMF가 이미 설치돼 있습니다. 기존 모드 구성을 보호하기 위해 자동 교체를 중단했습니다. 이 설치 파일은 EMF 0.3.5를 사용합니다.'
    }
    $tools = Read-KnownTools (Join-Path $Package 'backend\vendor\known_tools.txt')
    $proxy = Join-Path $root 'dinput8.dll'
    $movePlan = $null
    if ((Test-Path -LiteralPath $proxy) -and -not (Test-EmfLoader $proxy) -and (Get-DllToolName $proxy $tools) -ne 'eml') {
        $movePlan = Get-MovePlan $root $proxy $tools
        $null = Get-SafeGamePath $root $movePlan.Name
    }
    $bytes = (Get-Item -LiteralPath (Join-Path $root 'enshrouded.kfc')).Length +
             (Get-Item -LiteralPath (Join-Path $root 'enshrouded.kfc_resources')).Length
    $required = 2 * $bytes + 128MB
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($root))
    if ($drive.AvailableFreeSpace -lt $required) { throw ("설치·백업 공간이 부족합니다. 약 {0:N0} MB가 필요합니다." -f ($required / 1MB)) }
    return @{ Game = $root; Package = $Package; Manifest = $manifest; MovePlan = $movePlan; Tools = $tools; RequiredBytes = $required }
}

function Get-VerifiedDownload($Spec, [string]$Destination, [string[]]$Candidates, [bool]$Offline) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf) -and (Get-Sha256Hex $candidate) -eq $Spec.sha256) {
            Copy-Item -LiteralPath $candidate -Destination $Destination -Force
            return $Destination
        }
    }
    if ($Offline) { throw "오프라인 설치에 필요한 검증된 파일이 없습니다: $($Spec.file)" }
    if (-not $Spec.url.StartsWith('https://github.com/', [StringComparison]::OrdinalIgnoreCase)) { throw '허용되지 않은 다운로드 주소입니다.' }
    Write-Host ("공식 배포본 다운로드: " + $Spec.file)
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $oldProgress = $ProgressPreference
    try {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -UseBasicParsing -Uri $Spec.url -OutFile $Destination -Headers @{ 'User-Agent' = 'SpeedMod-Installer/1.0' }
    } finally { $ProgressPreference = $oldProgress }
    if ((Get-Sha256Hex $Destination) -ne $Spec.sha256) { throw "다운로드 검증에 실패했습니다: $($Spec.file)" }
    return $Destination
}

function Get-PreparedDependencies($Plan, [string]$Stage, [string]$DependencyDirectory, [bool]$Offline) {
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null
    $deps = $Plan.Manifest.Dependencies
    $emlCandidates = @((Join-Path $Plan.Game 'speedmod_eml.dll'), (Join-Path $Plan.Game 'eml.dll'), (Join-Path $Plan.Game 'dinput8.dll'))
    $cliCandidates = @((Join-Path $Plan.Game 'SpeedModInstaller\tools\emm.exe'), (Join-Path $Plan.Game 'EnshroudedMods\emm.exe'))
    if ($DependencyDirectory) {
        $emlCandidates = @((Join-Path $DependencyDirectory $deps.eml_dll.file)) + $emlCandidates
        $cliCandidates = @((Join-Path $DependencyDirectory $deps.eml_cli.file)) + $cliCandidates
    }
    $eml = Get-VerifiedDownload $deps.eml_dll (Join-Path $Stage 'eml-dinput8.dll') $emlCandidates $Offline
    $cli = Get-VerifiedDownload $deps.eml_cli (Join-Path $Stage 'emm.exe') $cliCandidates $Offline
    $emf = Join-Path $Stage 'emf-files'
    New-Item -ItemType Directory -Force -Path $emf | Out-Null
    $allPresent = $true
    foreach ($entry in $deps.emf_files) {
        $at = Get-SafeGamePath $Plan.Game $entry.path
        if (-not (Test-Path -LiteralPath $at -PathType Leaf) -or (Get-Sha256Hex $at) -ne $entry.sha256) { $allPresent = $false }
    }
    $zip = $null
    try {
        if (-not $allPresent) {
            $zipCandidates = @()
            if ($DependencyDirectory) { $zipCandidates += Join-Path $DependencyDirectory $deps.emf_zip.file }
            $zipPath = Get-VerifiedDownload $deps.emf_zip (Join-Path $Stage 'emf.zip') $zipCandidates $Offline
            $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
            $zipMap = @{}
            foreach ($zipEntry in $zip.Entries) {
                $normalized = $zipEntry.FullName.Replace('\', '/')
                if ($zipMap.ContainsKey($normalized)) { throw 'EMF 패키지에 중복 경로가 있습니다.' }
                $zipMap[$normalized] = $zipEntry
            }
            $loaderEntries = @($zipMap.Keys | Where-Object { $_ -eq 'game/dinput8.dll' -or $_.EndsWith('/game/dinput8.dll') })
            if ($loaderEntries.Count -ne 1) { throw 'EMF 패키지 형식이 예상과 다릅니다.' }
            $prefix = $loaderEntries[0].Substring(0, $loaderEntries[0].Length - 'game/dinput8.dll'.Length)
        }
        foreach ($entry in $deps.emf_files) {
            $target = Get-SafeGamePath $emf $entry.path
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            if ($allPresent) { Copy-Item -LiteralPath (Get-SafeGamePath $Plan.Game $entry.path) -Destination $target }
            else {
                $found = $zipMap[$prefix + 'game/' + $entry.path.Replace('\', '/')]
                if (-not $found) { throw "EMF 패키지 파일 없음: $($entry.path)" }
                [IO.Compression.ZipFileExtensions]::ExtractToFile($found, $target, $true)
            }
            if ((Get-Sha256Hex $target) -ne $entry.sha256) { throw "EMF 구성요소 검증 실패: $($entry.path)" }
        }
    } finally { if ($zip) { $zip.Dispose() } }
    return @{ Eml = $eml; Cli = $cli; Emf = $emf }
}

function New-InstallTransaction([string]$Game) {
    $relative = 'SpeedModInstaller\backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    $backup = Get-SafeGamePath $Game $relative
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    return @{ Game = $Game; Backup = $backup; Entries = (New-Object 'Collections.Generic.List[object]'); Directories = (New-Object 'Collections.Generic.List[string]'); Seen = @{}; Status = 'preparing' }
}

function Write-TransactionJournal($Transaction) {
    $record = [ordered]@{ version = 1; game = $Transaction.Game; status = $Transaction.Status; files = @($Transaction.Entries.ToArray()); created_directories = @($Transaction.Directories.ToArray()) }
    [IO.File]::WriteAllText((Join-Path $Transaction.Backup 'transaction.json'), ($record | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
}

function New-ManagedInstallDirectory($Transaction, [string]$Path) {
    $relative = $Path.Substring($Transaction.Game.Length + 1)
    $checked = Get-SafeGamePath $Transaction.Game $relative
    $missing = New-Object 'Collections.Generic.List[string]'
    while (-not (Test-Path -LiteralPath $checked)) {
        $missing.Add($checked)
        $checked = [IO.Path]::GetDirectoryName($checked)
    }
    for ($i = $missing.Count - 1; $i -ge 0; $i--) {
        $null = [IO.Directory]::CreateDirectory($missing[$i])
        $Transaction.Directories.Add($missing[$i].Substring($Transaction.Game.Length + 1))
    }
    if ($missing.Count) { Write-TransactionJournal $Transaction }
}

function Save-InstallOriginal($Transaction, [string]$Relative) {
    if ($Transaction.Seen.ContainsKey($Relative)) { return }
    $at = Get-SafeGamePath $Transaction.Game $Relative
    $exists = Test-Path -LiteralPath $at -PathType Leaf
    $copy = Get-SafeGamePath (Join-Path $Transaction.Backup 'files') $Relative
    if ($exists) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $copy) | Out-Null
        Copy-Item -LiteralPath $at -Destination $copy
        if ((Get-Sha256Hex $at) -ne (Get-Sha256Hex $copy)) { throw "기존 파일 백업 검증 실패: $Relative" }
    } elseif (Test-Path -LiteralPath $at) { throw "파일 경로가 폴더입니다: $Relative" }
    $Transaction.Entries.Add([pscustomobject]@{ path = $Relative; existed = [bool]$exists })
    $Transaction.Seen[$Relative] = $true
    Write-TransactionJournal $Transaction
}

function Copy-InstallFile($Transaction, [string]$Relative, [string]$Source, [bool]$KeepExisting = $false) {
    $target = Get-SafeGamePath $Transaction.Game $Relative
    if ($KeepExisting -and (Test-Path -LiteralPath $target -PathType Leaf)) { Write-Host "기존 설정 유지: $Relative"; return }
    if ((Test-Path -LiteralPath $target -PathType Leaf) -and (Get-Sha256Hex $target) -eq (Get-Sha256Hex $Source)) { return }
    Save-InstallOriginal $Transaction $Relative
    $parent = Split-Path -Parent $target
    if ($parent -ne $Transaction.Game) { New-ManagedInstallDirectory $Transaction $parent }
    $temporary = $target + '.speedmod-' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        Copy-Item -LiteralPath $Source -Destination $temporary
        if (Test-Path -LiteralPath $target) { [IO.File]::Replace($temporary, $target, [System.Management.Automation.Language.NullString]::Value) }
        else { [IO.File]::Move($temporary, $target) }
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
}

function Undo-InstallTransaction($Transaction) {
    $entries = $Transaction.Entries.ToArray()
    $problems = @()
    for ($i = $entries.Length - 1; $i -ge 0; $i--) {
        $entry = $entries[$i]
        try {
            $target = Get-SafeGamePath $Transaction.Game $entry.path
            if ($entry.existed) {
                $copy = Get-SafeGamePath (Join-Path $Transaction.Backup 'files') $entry.path
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item -LiteralPath $copy -Destination $target -Force
                if ((Get-Sha256Hex $copy) -ne (Get-Sha256Hex $target)) { throw 'restored hash mismatch' }
            } elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
        } catch { $problems += "$($entry.path): $($_.Exception.Message)" }
    }
    # An empty leftover mods/SpeedMod directory can itself break an existing EML install.
    # Remove only directories created by this transaction, non-recursively and only while empty.
    $directories = $Transaction.Directories.ToArray()
    for ($i = $directories.Length - 1; $i -ge 0; $i--) {
        $path = Get-SafeGamePath $Transaction.Game $directories[$i]
        if ([IO.Directory]::Exists($path) -and [IO.Directory]::GetFileSystemEntries($path).Length -eq 0) {
            try { [IO.Directory]::Delete($path, $false) } catch [IO.IOException] { }
        }
    }
    $Transaction.Status = if ($problems.Count) { 'rollback_incomplete' } else { 'rolled_back' }
    Write-TransactionJournal $Transaction
    if ($problems.Count) { throw ("일부 파일을 복구하지 못했습니다. 백업: {0}. {1}" -f $Transaction.Backup, ($problems -join '; ')) }
}

function Set-SpeedModChain($Plan, $Transaction, [string]$Scratch) {
    $f = Read-ChainFile $Plan.Game
    $lines = @($f.Lines)
    $extra = @()
    if ($Plan.MovePlan -and -not $Plan.MovePlan.RunsAs -and @(Get-ChainEntries $Plan.Game) -notcontains $Plan.MovePlan.Name) { $extra += $Plan.MovePlan.Name }
    $chainIndexes = @(Get-ChainLineIndexes $lines)
    $remove = @{}
    foreach ($i in $chainIndexes) {
        $name = $lines[$i].Trim()
        if (-not $name -or $name.StartsWith(';') -or $name.StartsWith('#') -or $name.StartsWith('[')) { continue }
        $at = Get-SafeGamePath $Plan.Game $name
        $isEml = (Test-Path -LiteralPath $at -PathType Leaf) -and (Get-DllToolName $at $Plan.Tools) -eq 'eml'
        $superseded = $Plan.MovePlan -and $Plan.MovePlan.SwitchOff -contains $name
        if ($name -ieq 'speedmod_bootstrap.dll' -or $name -ieq 'speedmod_eml.dll') { $remove[$i] = $true }
        elseif ($isEml -or $superseded) {
            $lines[$i] = '; SpeedMod replaced: ' + $lines[$i]
        }
    }
    $lines = @(for ($i = 0; $i -lt $lines.Count; $i++) { if (-not $remove.ContainsKey($i)) { $lines[$i] } })
    $header = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -ieq '[Chain]') { $header = $i; break } }
    if ($header -ge 0) {
        $before = @($lines[0..$header]); $after = @()
        if ($header + 1 -lt $lines.Count) { $after = @($lines[($header + 1)..($lines.Count - 1)]) }
        $newLines = $before + @('speedmod_bootstrap.dll', 'speedmod_eml.dll') + $extra + $after
    } else {
        $newLines = @('[Chain]', 'speedmod_bootstrap.dll', 'speedmod_eml.dll') + $extra + $lines
    }
    $output = Join-Path $Scratch 'chain.ini'
    [IO.File]::WriteAllText($output, (($newLines -join "`r`n") + "`r`n"), $f.Encoding)
    Copy-InstallFile $Transaction 'dinput8-chain.ini' $output
}

function Invoke-DataPatch([string]$Cli, [string]$Game, [string]$LogFile) {
    $running = Get-GameRunning $Game
    if ($running) { throw '설치 도중 게임이 실행됐습니다. 게임을 종료한 뒤 다시 설치해 주세요.' }
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $Cli
    $psi.Arguments = 'run -g "' + $Game + '" --patch --force'
    $psi.WorkingDirectory = $Game
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $psi
    try {
        $null = $process.Start()
        $outTask = $process.StandardOutput.ReadToEndAsync(); $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(600000)) { $process.Kill(); $process.WaitForExit(); throw '아이템 데이터 생성 시간이 초과됐습니다.' }
        $text = $outTask.Result + "`r`n" + $errTask.Result
        [IO.File]::WriteAllText($LogFile, $text, (New-Object Text.UTF8Encoding $false))
        if ($process.ExitCode -ne 0) { throw "아이템 데이터 생성에 실패했습니다. 로그: $LogFile" }
    } finally { $process.Dispose() }
}

function Invoke-SpeedModInstall($Plan, [string]$Scratch, [string]$DependencyDirectory = '', [bool]$Offline = $false, [scriptblock]$FailureInjector = $null) {
    Write-InstallStatus 10 '필수 구성요소를 준비합니다. 처음 설치할 때 인터넷 연결이 필요합니다.'
    $prepared = Get-PreparedDependencies $Plan (Join-Path $Scratch 'dependencies') $DependencyDirectory $Offline
    Write-InstallStatus 35 '기존 설정과 게임 데이터를 백업합니다.'
    $tx = New-InstallTransaction $Plan.Game
    try {
        foreach ($path in @('enshrouded.kfc', 'enshrouded.kfc_resources', 'enshrouded.kfc.bak', '.cache\files.json', '.cache\types.json', 'dinput8-chain.ini')) {
            Save-InstallOriginal $tx $path
        }
        $saveCount = 0
        foreach ($save in @(Find-SaveFiles (Get-SaveRoots))) {
            $destination = Get-SafeGamePath (Join-Path $tx.Backup 'saves') $save.Path
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            $checked = Copy-SaveFileChecked $save.Source $destination
            if (-not $checked.Ok) { throw "저장 파일 백업 실패: $($save.Name)" }
            $saveCount++
        }
        $tx.Status = 'installing'; Write-TransactionJournal $tx
        Write-InstallStatus 50 '모드와 실행 구성을 설치합니다.'
        if ($Plan.MovePlan -and -not $Plan.MovePlan.Identical) {
            Copy-InstallFile $tx $Plan.MovePlan.Name (Join-Path $Plan.Game 'dinput8.dll')
        }
        # EML's dbghelp proxy must not initialize a second copy next to the canonical chain loader.
        $dbghelp = Join-Path $Plan.Game 'dbghelp.dll'
        if ((Test-Path -LiteralPath $dbghelp) -and (Get-DllToolName $dbghelp $Plan.Tools) -eq 'eml') {
            Save-InstallOriginal $tx 'dbghelp.dll'
            Copy-InstallFile $tx 'SpeedModInstaller\disabled-loaders\eml-dbghelp.dll' $dbghelp
            Remove-Item -LiteralPath (Get-SafeGamePath $Plan.Game 'dbghelp.dll')
        }
        foreach ($entry in $Plan.Manifest.Dependencies.emf_files) {
            Copy-InstallFile $tx $entry.path (Get-SafeGamePath $prepared.Emf $entry.path)
        }
        Copy-InstallFile $tx 'speedmod_eml.dll' $prepared.Eml
        Copy-InstallFile $tx 'SpeedModInstaller\tools\emm.exe' $prepared.Cli
        foreach ($entry in $Plan.Manifest.Mod.files) {
            Copy-InstallFile $tx $entry.path (Get-SafeGamePath (Join-Path $Plan.Package 'mod') $entry.path) ([bool]$entry.preserve_existing)
        }
        foreach ($license in Get-ChildItem -LiteralPath (Join-Path $Plan.Package 'licenses') -File -Recurse) {
            $relative = $license.FullName.Substring((Join-Path $Plan.Package 'licenses').Length + 1)
            Copy-InstallFile $tx ('SpeedModInstaller\licenses\' + $relative) $license.FullName
        }
        Copy-InstallFile $tx 'SpeedModInstaller\README.txt' (Join-Path $Plan.Package 'README.txt')
        Set-SpeedModChain $Plan $tx $Scratch
        if ($FailureInjector) { & $FailureInjector 'after-files' }
        Write-InstallStatus 80 '신발 데이터를 생성합니다. 이 과정은 잠시 걸릴 수 있습니다.'
        Invoke-DataPatch (Join-Path $Plan.Game 'SpeedModInstaller\tools\emm.exe') $Plan.Game (Join-Path $tx.Backup 'patch.log')
        if ($FailureInjector) { & $FailureInjector 'after-patch' }
        $state = [ordered]@{ installer_version = $Plan.Manifest.Dependencies.installer_version; mod_version = $Plan.Manifest.Dependencies.mod_version
            installed_at = (Get-Date).ToString('o'); backup = $tx.Backup; save_files_backed_up = $saveCount
            game_sha256 = $Plan.Manifest.Dependencies.game_sha256; note = 'Do not remove item data before restoring every modified shoe.' }
        $statePath = Join-Path $Scratch 'install-state.json'
        [IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
        Copy-InstallFile $tx 'SpeedModInstaller\install-state.json' $statePath
        $tx.Status = 'completed'; Write-TransactionJournal $tx
        Write-InstallStatus 100 '설치가 완료됐습니다. 게임을 실행해 확인하세요.'
        return [pscustomobject]@{ success = $true; game = $Plan.Game; backup = $tx.Backup; version = $Plan.Manifest.Dependencies.mod_version; save_files_backed_up = $saveCount }
    } catch {
        $originalError = $_
        Write-Host '설치를 중단하고 기존 파일을 복구합니다.'
        Undo-InstallTransaction $tx
        throw ("설치에 실패해 기존 파일을 복구했습니다. {0} 백업: {1}" -f $originalError.Exception.Message, $tx.Backup)
    }
}
