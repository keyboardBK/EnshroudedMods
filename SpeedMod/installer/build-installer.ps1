param([switch]$SkipNativeBuild)
$ErrorActionPreference = 'Stop'
$modRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repoRoot = [IO.Path]::GetFullPath((Join-Path $modRoot '..'))
if (-not $SkipNativeBuild) {
    & (Join-Path $modRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'Native build/tests failed.' }
}
$csc = Join-Path ([Environment]::GetFolderPath('Windows')) 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw '.NET Framework C# compiler not found.' }
$build = Join-Path $modRoot ('build\installer-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $build 'payload'
$backend = Join-Path $payload 'backend'
$files = Join-Path $payload 'mod'
foreach ($dir in @($backend, (Join-Path $backend 'vendor'), $files, (Join-Path $payload 'licenses\EMF'), (Join-Path $modRoot 'package'))) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
foreach ($name in @('Installer.Core.ps1', 'Install-SpeedMod.ps1')) {
    $content = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name))
    [IO.File]::WriteAllText((Join-Path $backend $name), $content, (New-Object Text.UTF8Encoding $true))
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Destination $backend
$dependencies = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'dependencies.json')) | ConvertFrom-Json
$hashes = @($dependencies.game_sha256, $dependencies.eml_dll.sha256, $dependencies.eml_cli.sha256, $dependencies.emf_zip.sha256) + @($dependencies.emf_files | ForEach-Object { $_.sha256 })
foreach ($hash in $hashes) { if ($hash -notmatch '^[a-f0-9]{64}$') { throw 'Invalid pinned SHA-256 in dependencies.json.' } }
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\emf\installer\emf_common.ps1'), (Join-Path $repoRoot 'third_party\emf\installer\known_tools.txt') -Destination (Join-Path $backend 'vendor')
$mapping = @(
    @('build\speedmod_bootstrap.dll', 'speedmod_bootstrap.dll'),
    @('build\speedmod_boots.dll', 'emf\mods\speedmod_boots\speedmod_boots.dll'),
    @('runtime\mod.json', 'emf\mods\speedmod_boots\mod.json'),
    @('runtime\config.ini', 'emf\mods\speedmod_boots\config.ini'),
    @('eml\mod.json', 'mods\SpeedMod\mod.json'),
    @('eml\src\mod.lua', 'mods\SpeedMod\src\mod.lua')
)
$manifest = @()
foreach ($pair in $mapping) {
    $source = Join-Path $modRoot $pair[0]; $target = Join-Path $files $pair[1]
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
    $manifest += [ordered]@{ path = $pair[1].Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant(); preserve_existing = $pair[0] -eq 'runtime\config.ini' }
}
[IO.File]::WriteAllText((Join-Path $backend 'mod-manifest.json'), (@{ files = $manifest } | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding $false))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.txt') -Destination $payload
Copy-Item -Path (Join-Path $PSScriptRoot 'licenses\*') -Destination (Join-Path $payload 'licenses')
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\emf\LICENSE.txt') -Destination (Join-Path $payload 'licenses\EMF\LICENSE.txt')
Copy-Item -Path (Join-Path $repoRoot 'third_party\emf\licenses\*') -Destination (Join-Path $payload 'licenses\EMF')
$zip = Join-Path $build 'payload.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($payload, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
$output = Join-Path $modRoot 'package\SpeedMod-Setup-0.2.1.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /warn:4 /warnaserror+ /codepage:65001 "/out:$output" "/win32manifest:$(Join-Path $PSScriptRoot 'app.manifest')" "/resource:$zip,SpeedMod.Payload.zip" /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll /reference:System.Web.Extensions.dll (Join-Path $PSScriptRoot 'Setup.cs')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
[IO.File]::WriteAllText((Join-Path $modRoot 'build\installer-last-build.json'), (@{ payload = $payload; setup = $output } | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
Get-Item -LiteralPath $output | Select-Object FullName, Length
