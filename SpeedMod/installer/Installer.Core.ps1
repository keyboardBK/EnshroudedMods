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

function Get-InstallPlan([string]$Game, [string]$Package, [bool]$Removing = $false) {
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
    if (-not $Removing -and (Test-Path -LiteralPath $core) -and (Get-Sha256Hex $core) -ne $coreSpec.sha256) {
        throw '다른 버전의 EMF가 이미 설치돼 있습니다. 기존 모드 구성을 보호하기 위해 자동 교체를 중단했습니다. 이 설치 파일은 EMF 0.3.5를 사용합니다.'
    }
    $tools = Read-KnownTools (Join-Path $Package 'backend\vendor\known_tools.txt')
    $proxy = Join-Path $root 'dinput8.dll'
    $movePlan = $null
    if (-not $Removing -and (Test-Path -LiteralPath $proxy) -and -not (Test-EmfLoader $proxy) -and (Get-DllToolName $proxy $tools) -ne 'eml') {
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

function Save-GameSaves($Transaction) {
    $count = 0
    foreach ($save in @(Find-SaveFiles (Get-SaveRoots))) {
        $destination = Get-SafeGamePath (Join-Path $Transaction.Backup 'saves') $save.Path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        $checked = Copy-SaveFileChecked $save.Source $destination
        if (-not $checked.Ok) { throw "저장 파일 백업 실패: $($save.Name)" }
        $count++
    }
    return $count
}

function Get-GeneratedContainers([string]$Game, [int]$BaseCount) {
    foreach ($file in Get-ChildItem -LiteralPath $Game -File -Filter 'enshrouded_*.dat') {
        if ($file.Name -match '^enshrouded_(\d+)\.dat$' -and [int]$Matches[1] -ge $BaseCount) {
            $null = Get-SafeGamePath $Game $file.Name
            $file.Name
        }
    }
}

function Save-PatchState($Transaction) {
    foreach ($path in @('enshrouded.kfc', 'enshrouded.kfc_resources', 'enshrouded.kfc.bak', '.cache\files.json', '.cache\types.json', '.cache\lua\types.lua', '.cache\lua\base.lua')) {
        Save-InstallOriginal $Transaction $path
    }
    # EML may overwrite extra content containers belonging to OTHER data mods.
    # Original containers are immutable; only indices >= the original KFC count are writable.
    $base = Get-SafeGamePath $Transaction.Game 'enshrouded.kfc.bak'
    if (-not (Test-Path -LiteralPath $base)) { $base = Get-SafeGamePath $Transaction.Game 'enshrouded.kfc' }
    $reader = New-Object IO.BinaryReader([IO.File]::OpenRead($base))
    try {
        if ($reader.ReadUInt32() -ne 0x3443464B -or $reader.BaseStream.Length -lt 32) { throw '원본 KFC 형식을 확인할 수 없습니다.' }
        $reader.BaseStream.Position = 28
        $count = $reader.ReadUInt32()
        if ($count -lt 1 -or $count -gt 4096) { throw '원본 KFC 컨테이너 수가 올바르지 않습니다.' }
    } finally { $reader.Dispose() }
    $Transaction.BaseContainers = [int]$count
    $Transaction.ExistingContainers = @{}
    foreach ($path in @(Get-GeneratedContainers $Transaction.Game $count)) {
        $Transaction.ExistingContainers[$path] = $true
        Save-InstallOriginal $Transaction $path
    }
}

function Register-NewPatchContainers($Transaction) {
    if (-not $Transaction.PatchMayHaveRun) { return }
    foreach ($path in @(Get-GeneratedContainers $Transaction.Game $Transaction.BaseContainers)) {
        if (-not $Transaction.ExistingContainers.ContainsKey($path) -and -not $Transaction.Seen.ContainsKey($path)) {
            $Transaction.Entries.Add([pscustomobject]@{ path = $path; existed = $false })
            $Transaction.Seen[$path] = $true
        }
    }
    Write-TransactionJournal $Transaction
}

function Get-SpeedModRemovalPlan([string]$Game, [string]$Package, [bool]$ItemsRestored) {
    if (-not $ItemsRestored) { throw '제거 전 호스트 EMF 콘솔의 speedmod_boots.restore로 모든 캐릭터·월드·창고 신발을 복구하고 저장했는지 확인해야 합니다. 오프라인 캐릭터와 미로드 창고는 자동 복구되지 않습니다.' }
    $plan = Get-InstallPlan $Game $Package $true
    $plan.RemoveFiles = New-Object 'Collections.Generic.List[string]'
    $plan.RemoveDirectories = New-Object 'Collections.Generic.List[string]'
    foreach ($spec in @(@('mods\SpeedMod', 'SpeedMod'), @('emf\mods\speedmod_boots', 'speedmod_boots'))) {
        $root = Get-SafeGamePath $plan.Game $spec[0]
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $manifest = Get-SafeGamePath $plan.Game ($spec[0] + '\mod.json')
        if (-not (Test-Path -LiteralPath $manifest -PathType Leaf) -or
            (([IO.File]::ReadAllText($manifest) | ConvertFrom-Json).id -cne $spec[1])) {
            throw "다른 모드일 수 있어 제거를 중단했습니다: $($spec[0])"
        }
        # Explicit traversal validates each path BEFORE descending into it; never follow links.
        $pending = New-Object 'Collections.Generic.Queue[string]'
        $pending.Enqueue($spec[0])
        while ($pending.Count) {
            $relative = $pending.Dequeue()
            $at = Get-SafeGamePath $plan.Game $relative
            $plan.RemoveDirectories.Add($relative)
            foreach ($entry in Get-ChildItem -LiteralPath $at -Force) {
                $name = $relative + '\' + $entry.Name
                $null = Get-SafeGamePath $plan.Game $name
                if ($entry.PSIsContainer) { $pending.Enqueue($name) } else { $plan.RemoveFiles.Add($name) }
            }
        }
    }
    $bootstrap = Get-SafeGamePath $plan.Game 'speedmod_bootstrap.dll'
    if (Test-Path -LiteralPath $bootstrap -PathType Leaf) { $plan.RemoveFiles.Add('speedmod_bootstrap.dll') }
    if (-not $plan.RemoveFiles.Count) { throw '제거할 SpeedMod 파일을 찾지 못했습니다.' }
    # Keep the existing EML and all its chain entries, even when named speedmod_eml.dll.
    $null = Get-SafeGamePath $plan.Game 'dinput8-chain.ini'
    foreach ($name in @(Get-ChainEntries $plan.Game)) { $null = Get-SafeGamePath $plan.Game $name }
    return $plan
}

function Invoke-SpeedModRemove($Plan, [string]$Scratch, [string]$DependencyDirectory = '', [bool]$Offline = $false, [scriptblock]$FailureInjector = $null) {
    Write-InstallStatus 10 'EML 패치 도구를 확인합니다. EML·EMF와 다른 모드는 유지합니다.'
    New-Item -ItemType Directory -Force -Path $Scratch | Out-Null
    $spec = $Plan.Manifest.Dependencies.eml_cli
    $candidates = @((Join-Path $Plan.Game 'SpeedModInstaller\tools\emm.exe'), (Join-Path $Plan.Game 'EnshroudedMods\emm.exe'))
    if ($DependencyDirectory) { $candidates = @((Join-Path $DependencyDirectory $spec.file)) + $candidates }
    $cli = Get-VerifiedDownload $spec (Join-Path $Scratch 'remove-emm.exe') $candidates $Offline
    $tx = New-InstallTransaction $Plan.Game
    try {
        Write-InstallStatus 30 '현재 데이터와 SpeedMod 파일·설정·저장 파일을 백업합니다.'
        Save-PatchState $tx
        Save-InstallOriginal $tx 'dinput8-chain.ini'
        foreach ($path in $Plan.RemoveFiles) { Save-InstallOriginal $tx $path }
        $saveCount = Save-GameSaves $tx
        $tx.Status = 'removing'; Write-TransactionJournal $tx
        foreach ($path in $Plan.RemoveFiles) { Remove-Item -LiteralPath (Get-SafeGamePath $Plan.Game $path) }
        # Only the two validated SpeedMod folders, deepest first, and never recursively.
        for ($i = $Plan.RemoveDirectories.Count - 1; $i -ge 0; $i--) {
            [IO.Directory]::Delete((Get-SafeGamePath $Plan.Game $Plan.RemoveDirectories[$i]), $false)
        }
        $chain = Read-ChainFile $Plan.Game
        $indexes = @(Get-ChainLineIndexes @($chain.Lines))
        $lines = @(for ($i = 0; $i -lt $chain.Lines.Count; $i++) {
            if ($indexes -contains $i -and $chain.Lines[$i].Trim() -ieq 'speedmod_bootstrap.dll') { continue }
            $chain.Lines[$i]
        })
        $chainPath = Join-Path $Scratch 'remove-chain.ini'
        [IO.File]::WriteAllText($chainPath, (($lines -join "`r`n") + "`r`n"), $chain.Encoding)
        Copy-InstallFile $tx 'dinput8-chain.ini' $chainPath
        if ($FailureInjector) { & $FailureInjector 'after-files' }
        Write-InstallStatus 70 'SpeedMod를 제외한 나머지 EML 모드를 다시 적용합니다.'
        $tx.PatchMayHaveRun = $true
        Invoke-DataPatch $cli $Plan.Game (Join-Path $tx.Backup 'patch.log')
        Register-NewPatchContainers $tx
        if ($FailureInjector) { & $FailureInjector 'after-patch' }
        $statePath = Join-Path $Scratch 'removed-state.json'
        $state = @{ installer_version = $Plan.Manifest.Dependencies.installer_version; status = 'removed'; removed_at = (Get-Date).ToString('o'); backup = $tx.Backup; save_files_backed_up = $saveCount }
        [IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
        Copy-InstallFile $tx 'SpeedModInstaller\install-state.json' $statePath
        $tx.Status = 'completed'; Write-TransactionJournal $tx
        Write-InstallStatus 100 'SpeedMod만 제거했습니다. EML·EMF와 다른 모드는 유지했습니다.'
        return [pscustomobject]@{ success = $true; removed = $true; game = $Plan.Game; backup = $tx.Backup; save_files_backed_up = $saveCount }
    } catch {
        $originalError = $_
        Register-NewPatchContainers $tx
        Undo-InstallTransaction $tx
        throw ("제거에 실패해 기존 파일을 복구했습니다. {0} 백업: {1}" -f $originalError.Exception.Message, $tx.Backup)
    }
}

function Invoke-SpeedModInstall($Plan, [string]$Scratch, [string]$DependencyDirectory = '', [bool]$Offline = $false, [scriptblock]$FailureInjector = $null) {
    Write-InstallStatus 10 '필수 구성요소를 준비합니다. 처음 설치할 때 인터넷 연결이 필요합니다.'
    $prepared = Get-PreparedDependencies $Plan (Join-Path $Scratch 'dependencies') $DependencyDirectory $Offline
    Write-InstallStatus 35 '기존 설정과 게임 데이터를 백업합니다.'
    $tx = New-InstallTransaction $Plan.Game
    try {
        Save-PatchState $tx
        Save-InstallOriginal $tx 'dinput8-chain.ini'
        $saveCount = Save-GameSaves $tx
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
        $tx.PatchMayHaveRun = $true
        Invoke-DataPatch (Join-Path $Plan.Game 'SpeedModInstaller\tools\emm.exe') $Plan.Game (Join-Path $tx.Backup 'patch.log')
        Register-NewPatchContainers $tx
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
        Register-NewPatchContainers $tx
        Undo-InstallTransaction $tx
        throw ("설치에 실패해 기존 파일을 복구했습니다. {0} 백업: {1}" -f $originalError.Exception.Message, $tx.Backup)
    }
}
