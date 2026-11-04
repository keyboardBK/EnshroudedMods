param([string]$Game = 'D:\SteamLibrary\steamapps\common\Enshrouded')
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'build\SpeedMod-Server-0.3.0'
$payload=Join-Path $out 'payload'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
$emf=Join-Path $Game 'ModWork\SpeedMod\references\emf-0.4.1\manual\EMF-0.4.1-manual'
$map=@(
    @{source=(Join-Path $PSScriptRoot 'build\speedmod_server.dll');path='emf/mods/speedmod_server/speedmod_server.dll'},
    @{source=(Join-Path $PSScriptRoot 'mod.json');path='emf/mods/speedmod_server/mod.json'},
    @{source=(Join-Path $PSScriptRoot '..\runtime\config.ini');path='emf/mods/speedmod_server/config.ini';preserve=$true},
    @{source=(Join-Path $PSScriptRoot 'README.ko.md');path='emf/mods/speedmod_server/README.ko.md'},
    @{source=(Join-Path $PSScriptRoot 'effects\mod.json');path='mods/SpeedModServerEffects/mod.json'},
    @{source=(Join-Path $PSScriptRoot 'effects\src\mod.lua');path='mods/SpeedModServerEffects/src/mod.lua'},
    @{source=(Join-Path $emf 'game\dinput8.dll');path='dinput8.dll'},
    @{source=(Join-Path $emf 'game\emf\emf_core.dll');path='emf/emf_core.dll'},
    @{source=(Join-Path $emf 'game\emf\emf_content.dll');path='emf/emf_content.dll'},
    @{source=(Join-Path $emf 'game\emf\known_tools.txt');path='emf/known_tools.txt'}
)
foreach ($dir in @('vk','fonts','tools')) {
    $base=Join-Path $emf ('game\emf\'+$dir)
    foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        $relative=$file.FullName.Substring($base.Length).TrimStart('\')
        $map+=@{source=$file.FullName;path=('emf/'+$dir+'/'+$relative.Replace('\','/'))}
    }
}
$manifest=@()
foreach ($spec in $map) {
    $target=Join-Path $payload $spec.path
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $spec.source -Destination $target -Force
    $manifest+=@{path=$spec.path;sha256=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant();preserve_existing=[bool]$spec.preserve}
}
foreach ($name in @('install.ps1','README.ko.md')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $out -Force }
$data=@{version='0.3.0';game_sha256='ce6fdba63e27744943b695921383a29133285d359e07671f6ca33a47e2147618';base_kfc_sha256='ff05d077f42a53ca62bba06f1242739d531813ed7baedd2a78e15a668ba339a3';files=$manifest}
[IO.File]::WriteAllText((Join-Path $out 'manifest.json'),($data | ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding $false))
$cmd='@echo off' + "`r`n" + 'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -RemoveLegacyItems %*' + "`r`n" + 'pause' + "`r`n"
[IO.File]::WriteAllText((Join-Path $out 'install.cmd'),$cmd,[Text.Encoding]::ASCII)
New-Item -ItemType Directory -Force -Path (Join-Path $out 'licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $emf 'LICENSE.txt') -Destination (Join-Path $out 'licenses\EMF-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $emf 'licenses') -Destination (Join-Path $out 'licenses\EMF') -Recurse -Force
# GPL components remain separate DLLs. Preserve their notices and official source location.
[IO.File]::WriteAllText((Join-Path $out 'licenses\SOURCES.txt'),"EMF 0.4.1 official source: https://github.com/matenm/emf-enshrouded/releases/download/v0.4.1/EMF-0.4.1-source.zip`r`nEML: https://github.com/Brabb3l/kfc-parser`r`n",[Text.Encoding]::UTF8)
$zip=Join-Path $PSScriptRoot 'build\SpeedMod-Server-0.3.0.zip'
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($out,$zip)
Get-Item -LiteralPath $out,$zip | Select-Object FullName,Length
