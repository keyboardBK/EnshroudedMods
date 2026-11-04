param(
    [string]$Game = '',
    [switch]$CheckOnly,
    [switch]$RemoveLegacyItems,
    [int]$TestFailAfterWrites = -1
)
$ErrorActionPreference = 'Stop'
if (-not $Game) {
    $steamRoots=New-Object 'Collections.Generic.List[string]'
    $steamProperty=Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue
    if ($steamProperty -and $steamProperty.SteamPath) { $steamRoots.Add($steamProperty.SteamPath) }
    if (${env:ProgramFiles(x86)}) { $steamRoots.Add((Join-Path ${env:ProgramFiles(x86)} 'Steam')) }
    $libraries=New-Object 'Collections.Generic.List[string]'
    foreach ($steamRoot in @($steamRoots | Select-Object -Unique)) {
        $libraries.Add($steamRoot)
        $folders=Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $folders) {
            $text=[IO.File]::ReadAllText($folders)
            foreach ($match in [regex]::Matches($text,'"path"\s+"([^"]+)"')) {
                $libraries.Add($match.Groups[1].Value.Replace('\\','\'))
            }
        }
    }
    $candidates=@($libraries | Select-Object -Unique | ForEach-Object {
        $candidate=Join-Path $_ 'steamapps\common\Enshrouded'
        if (Test-Path -LiteralPath (Join-Path $candidate 'enshrouded.exe')) { [IO.Path]::GetFullPath($candidate).TrimEnd('\') }
    } | Select-Object -Unique)
    if ($candidates.Count -ne 1) { throw 'Could not select one game installation. Use install.cmd -Game "full game folder".' }
    $Game=$candidates[0]
}
$root = [IO.Path]::GetFullPath($Game).TrimEnd('\')
$payload = Join-Path $PSScriptRoot 'payload'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
function SafePath([string]$Base,[string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative)) { throw 'Absolute relative path rejected.' }
    $path = [IO.Path]::GetFullPath((Join-Path $Base $Relative))
    if (-not $path.StartsWith($Base.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Path outside selected directory.' }
    $probe=$path
    while ($probe -and $probe.Length -ge $Base.Length) {
        if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked install path rejected: $probe" }
        $probe=[IO.Path]::GetDirectoryName($probe)
    }
    return $path
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'enshrouded.exe'))) { throw 'Game executable missing.' }
if ((Get-FileHash -LiteralPath (Join-Path $root 'enshrouded.exe')).Hash.ToLowerInvariant() -ne $manifest.game_sha256) { throw 'This package was checked against game build 1285601.' }
if (Get-Process enshrouded,enshrouded_server -ErrorAction SilentlyContinue) { throw 'Close the game/server first.' }
foreach ($spec in $manifest.files) {
    $source=SafePath $payload $spec.path
    $null=SafePath $root $spec.path
    if ((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $spec.sha256) { throw "Package checksum mismatch: $($spec.path)" }
}
# This install changes loaders/mods only. The game data must be the current unpatched base.
$stream=[IO.File]::OpenRead((Join-Path $root 'enshrouded.kfc'))
try { $head=New-Object byte[] 65536; $read=$stream.Read($head,0,$head.Length) } finally { $stream.Dispose() }
if ($read -lt 32 -or [BitConverter]::ToUInt32($head,0) -ne 0x3443464B) { throw 'Invalid game content header.' }
$at=16+[BitConverter]::ToUInt32($head,16);$len=[BitConverter]::ToUInt32($head,20)
if ($at+$len -gt $read) { throw 'Invalid content version.' }
$version=[Text.Encoding]::UTF8.GetString($head,$at,$len)
if (-not $version.StartsWith('1285601|') -or [BitConverter]::ToUInt32($head,28) -ne 55 -or
    (Get-FileHash -LiteralPath (Join-Path $root 'enshrouded.kfc')).Hash.ToLowerInvariant() -ne $manifest.base_kfc_sha256) {
    throw 'A current unpatched base is required; this installer will not restore an old game backup over an update.'
}
$legacy='mods\SpeedMod','emf\mods\speedmod_boots','speedmod_bootstrap.dll','speedmod_eml.dll','enshrouded.kfc.bak','.cache'
foreach ($relative in $legacy) { $null=SafePath $root $relative }
if ($CheckOnly) {
    [pscustomobject]@{success=$true;check_only=$true;version='0.3.0';files=$manifest.files.Count;legacy_cleanup=$true;game=$root} | ConvertTo-Json
    return
}
if (-not $RemoveLegacyItems) { throw 'This migration deletes old SM02 shoes when loaded. Pass -RemoveLegacyItems only after accepting that.' }
$backup=SafePath $root ('SpeedModInstaller\backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-server03')
New-Item -ItemType Directory -Path (Join-Path $backup 'files') -Force | Out-Null
$copied=New-Object 'Collections.Generic.List[string]'
$moved=New-Object 'Collections.Generic.List[string]'
$newFiles=New-Object 'Collections.Generic.List[string]'
$writes=0
function BackupFile([string]$Relative) {
    $path=SafePath $root $Relative
    if (-not (Test-Path -LiteralPath $path)) { $newFiles.Add($Relative); return }
    if ($copied.Contains($Relative)) { return }
    $target=SafePath (Join-Path $backup 'files') $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $path -Destination $target -Force
    $copied.Add($Relative)
}
try {
    # Snapshot saves into this game's backup folder before changing loaders. Read originals only.
    $saveSources=New-Object 'Collections.Generic.List[object]'
    $localRoot=Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Saved Games\keen games\Enshrouded'
    if (Test-Path -LiteralPath $localRoot) { $saveSources.Add(@{root=$localRoot;label='local'}) }
    $steamProperty=Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue
    if ($steamProperty) {
        $userdata=Join-Path $steamProperty.SteamPath 'userdata'
        foreach ($account in Get-ChildItem -LiteralPath $userdata -Directory -ErrorAction SilentlyContinue) {
            $remote=Join-Path $account.FullName '1203620\remote'
            if (Test-Path -LiteralPath $remote) { $saveSources.Add(@{root=$remote;label=('steam\'+$account.Name)}) }
        }
    }
    foreach ($source in $saveSources) {
        foreach ($file in Get-ChildItem -LiteralPath $source.root -Recurse -File | Where-Object { $_.Extension -eq '.ksd' -or $_.Name -match '^.+-index$|^character|^world|^global' }) {
            $relative=$file.FullName.Substring($source.root.Length).TrimStart('\')
            $target=SafePath (Join-Path $backup 'saves') ($source.label+'\'+$relative)
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target
            if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash) { throw 'Save backup verification failed.' }
        }
    }
    foreach ($relative in $legacy) {
        $source=SafePath $root $relative
        if (Test-Path -LiteralPath $source) {
            $target=SafePath (Join-Path $backup 'files') $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            # Both absolute paths were checked above; keep all legacy files for rollback.
            Move-Item -LiteralPath $source -Destination $target
            $moved.Add($relative)
        }
    }
    foreach ($spec in $manifest.files) {
        $relative=[string]$spec.path
        if ($spec.preserve_existing -and (Test-Path -LiteralPath (SafePath $root $relative))) { continue }
        BackupFile $relative
        $target=SafePath $root $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath (SafePath $payload $relative) -Destination $target -Force
        ++$writes
        if ($TestFailAfterWrites -ge 0 -and $writes -ge $TestFailAfterWrites) { throw 'Injected installer failure.' }
    }
    BackupFile 'dinput8-chain.ini'
    $chain=SafePath $root 'dinput8-chain.ini'
    $lines=if (Test-Path -LiteralPath $chain) { [IO.File]::ReadAllLines($chain) } else { @('[Chain]') }
    $filtered=@($lines | Where-Object { $_ -notmatch '^\s*;?\s*speedmod_(?:bootstrap|eml)\.dll\s*$' })
    [IO.File]::WriteAllLines($chain,$filtered,(New-Object Text.UTF8Encoding $false))
    BackupFile 'SpeedModInstaller\install-state.json'
    $state=@{mod_version='0.3.0';emf_version='0.4.1';game_sha256=$manifest.game_sha256;installed_at=(Get-Date).ToString('o');backup=$backup;legacy_items_authorized_for_deletion=$true;game_data_patched_in_place=$false}
    [IO.File]::WriteAllText((SafePath $root 'SpeedModInstaller\install-state.json'),($state | ConvertTo-Json),(New-Object Text.UTF8Encoding $false))
    $receipt=@{success=$true;version='0.3.0';game=$root;backup=$backup;copied=@($copied);moved=@($moved);new_files=@($newFiles)}
    [IO.File]::WriteAllText((Join-Path $backup 'transaction.json'),($receipt | ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding $false))
    $receipt | ConvertTo-Json -Depth 5
} catch {
    $failure=$_
    foreach ($relative in $newFiles) {
        $path=SafePath $root $relative
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
    }
    foreach ($relative in $copied) { Copy-Item -LiteralPath (SafePath (Join-Path $backup 'files') $relative) -Destination (SafePath $root $relative) -Force }
    foreach ($relative in $moved) {
        $source=SafePath (Join-Path $backup 'files') $relative;$target=SafePath $root $relative
        if (Test-Path -LiteralPath $target) { throw "Rollback target already exists: $target. Backup: $backup" }
        Move-Item -LiteralPath $source -Destination $target
    }
    throw "Installation rolled back: $($failure.Exception.Message). Backup: $backup"
}
