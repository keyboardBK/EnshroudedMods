param([string]$Game = 'D:\SteamLibrary\steamapps\common\Enshrouded')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Game).TrimEnd('\')
$scratch = Join-Path $root ('ModWork\SpeedMod\server-validation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
foreach ($name in @('enshrouded.exe','enshrouded.kfc','enshrouded.kfc_resources')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $scratch
}
# Data archives are only read by EML. Its patched .kfc files are independent copies.
foreach ($archive in Get-ChildItem -LiteralPath $root -Filter 'enshrouded_*.dat' -File) {
    New-Item -ItemType HardLink -Path (Join-Path $scratch $archive.Name) -Target $archive.FullName | Out-Null
}
$mods = Join-Path $scratch 'mods'
New-Item -ItemType Directory -Path $mods | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'effects') -Destination (Join-Path $mods 'SpeedModServerEffects') -Recurse
$cli = Join-Path $root 'EnshroudedMods\emm.exe'
$log = Join-Path $scratch 'patch.log'
New-Item -ItemType Directory -Path (Join-Path $scratch 'exports') | Out-Null
& $cli run --patch --export --force --game-directory $scratch --export-directory (Join-Path $scratch 'exports') *> $log
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $log -Tail 50 -Encoding UTF8; throw 'EML validation failed.' }
Get-Content -LiteralPath $log -Tail 18 -Encoding UTF8
[pscustomobject]@{success=$true; shadow=$scratch; log=$log} | ConvertTo-Json
