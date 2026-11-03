$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$manifest = [IO.File]::ReadAllText((Join-Path $root 'mod.json')) | ConvertFrom-Json
$id = [string]$manifest.id
if ($id -notmatch '^speedmod_(formula|ledger|metadata)$') { throw 'Unexpected experiment id' }
$dll = Join-Path $root ('build\' + $id + '.dll')
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw 'Run build.cmd and its checks first.' }
$stage = Join-Path $root ('build\package-' + [guid]::NewGuid().ToString('N'))
$target = Join-Path $stage $id
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($source in @($dll, (Join-Path $root 'mod.json'), (Join-Path $root 'README.md'))) {
    Copy-Item -LiteralPath $source -Destination $target
}
$license = Join-Path $root '..\..\third_party\emf\LICENSE.txt'
Copy-Item -LiteralPath $license -Destination (Join-Path $target 'EMF-LICENSE.txt')
foreach ($tool in @('compare.py', 'audit_schema.py')) {
    $source = Join-Path $root $tool
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $target }
}
$distribution = Join-Path $root 'distribution'
New-Item -ItemType Directory -Force -Path $distribution | Out-Null
$zip = Join-Path $distribution ($id + '-experiment-0.0.1.zip')
Add-Type -AssemblyName System.IO.Compression.FileSystem
# A unique temporary ZIP then atomic replacement avoids publishing a partially written artifact.
$temporary = Join-Path $root ('build\artifact-' + [guid]::NewGuid().ToString('N') + '.zip')
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $temporary, [IO.Compression.CompressionLevel]::Optimal, $false)
if (Test-Path -LiteralPath $zip) { [IO.File]::Replace($temporary, $zip, [System.Management.Automation.Language.NullString]::Value) }
else { [IO.File]::Move($temporary, $zip) }
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zip + '.sha256'), ($hash + '  ' + [IO.Path]::GetFileName($zip) + [Environment]::NewLine), (New-Object Text.UTF8Encoding $false))
[pscustomobject]@{ artifact = $zip; sha256 = $hash; bytes = (Get-Item -LiteralPath $zip).Length } | ConvertTo-Json
