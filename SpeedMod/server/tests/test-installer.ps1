param([string]$Game='D:\SteamLibrary\steamapps\common\Enshrouded')
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
$package=Join-Path $source 'build\SpeedMod-Server-0.3.0'
$fixture=Join-Path $Game ('ModWork\InstallerTests\Server03-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $fixture,(Join-Path $fixture 'mods\SpeedMod'),(Join-Path $fixture 'emf\mods\speedmod_boots'),(Join-Path $fixture '.cache') | Out-Null
foreach ($name in @('enshrouded.exe','enshrouded.kfc')) { Copy-Item -LiteralPath (Join-Path $Game $name) -Destination $fixture }
[IO.File]::WriteAllText((Join-Path $fixture 'mods\SpeedMod\mod.json'),'old mod')
[IO.File]::WriteAllText((Join-Path $fixture 'emf\mods\speedmod_boots\mod.json'),'old runtime')
[IO.File]::WriteAllText((Join-Path $fixture 'speedmod_bootstrap.dll'),'old bootstrap')
[IO.File]::WriteAllText((Join-Path $fixture 'speedmod_eml.dll'),'old eml')
[IO.File]::WriteAllText((Join-Path $fixture 'dinput8.dll'),'old emf loader')
[IO.File]::WriteAllText((Join-Path $fixture 'dinput8-chain.ini'),"[Chain]`r`nspeedmod_bootstrap.dll`r`nother-mod.dll`r`nspeedmod_eml.dll`r`n")
[IO.File]::WriteAllText((Join-Path $fixture '.cache\probe'),'old cache')
$hash=(Get-FileHash -LiteralPath (Join-Path $fixture 'enshrouded.kfc')).Hash
& (Join-Path $package 'install.ps1') -Game $fixture -CheckOnly | Out-Null
$failed=$false
try { & (Join-Path $package 'install.ps1') -Game $fixture -RemoveLegacyItems -TestFailAfterWrites 1 | Out-Null } catch { $failed=$true }
if (-not $failed -or (Get-Content -LiteralPath (Join-Path $fixture 'dinput8.dll') -Raw) -ne 'old emf loader' -or
    -not (Test-Path -LiteralPath (Join-Path $fixture 'mods\SpeedMod\mod.json')) -or
    -not (Test-Path -LiteralPath (Join-Path $fixture 'speedmod_bootstrap.dll')) -or
    -not (Test-Path -LiteralPath (Join-Path $fixture '.cache\probe'))) { throw 'Installer rollback failed.' }
& (Join-Path $package 'install.ps1') -Game $fixture -RemoveLegacyItems | Out-Null
if ((Get-FileHash -LiteralPath (Join-Path $fixture 'enshrouded.kfc')).Hash -ne $hash) { throw 'Original KFC changed.' }
if (Test-Path -LiteralPath (Join-Path $fixture 'mods\SpeedMod\mod.json')) { throw 'Legacy data stayed active.' }
if (Test-Path -LiteralPath (Join-Path $fixture 'speedmod_bootstrap.dll')) { throw 'Bootstrap stayed active.' }
$chain=Get-Content -LiteralPath (Join-Path $fixture 'dinput8-chain.ini') -Raw
if ($chain -notmatch 'other-mod.dll' -or $chain -match 'speedmod_') { throw 'Chain migration changed another mod.' }
if (-not (Test-Path -LiteralPath (Join-Path $fixture 'emf\emf_content.dll')) -or
    -not (Test-Path -LiteralPath (Join-Path $fixture 'emf\mods\speedmod_server\speedmod_server.dll'))) { throw 'New runtime missing.' }
[pscustomobject]@{success=$true;tests='preflight, injected failure rollback, legacy migration, other chain preserved, original KFC unchanged';fixture=$fixture} | ConvertTo-Json
