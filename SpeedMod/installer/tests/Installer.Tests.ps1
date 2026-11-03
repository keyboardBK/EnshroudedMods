param([string]$Package, [string]$WorkRoot)
$ErrorActionPreference = 'Stop'
if (-not $Package -or -not $WorkRoot) { throw 'Specify extracted installer -Package and a writable -WorkRoot.' }
$work = [IO.Path]::GetFullPath((Join-Path $WorkRoot ('t-' + [guid]::NewGuid().ToString('N').Substring(0, 8))))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$env:EMF_TEST_NO_STEAM = '1'
$env:EMF_TEST_STEAM_ROOT = Join-Path $work 'fake-steam'
$env:EMF_TEST_SAVED_GAMES = Join-Path $work 'fake-local'
$env:EMF_TEST_APPDATA = Join-Path $work 'fake-appdata'
. (Join-Path $Package 'backend\Installer.Core.ps1')
$csc = Join-Path ([Environment]::GetFolderPath('Windows')) 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$fakeCli = Join-Path $work 'fake-emm.exe'
& $csc /nologo /target:exe "/out:$fakeCli" (Join-Path $PSScriptRoot 'FakeEmm.cs')
if ($LASTEXITCODE -ne 0) { throw 'Fake patcher build failed.' }
function Assert-True($Value, [string]$Message) { if (-not $Value) { throw "ASSERT: $Message" } }
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $thrown = $false
    try { & $Action | Out-Null } catch { $thrown = $true }
    Assert-True $thrown $Message
}
function Write-TestFile([string]$Path, [string]$Value) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Value, (New-Object Text.UTF8Encoding $false))
}
$fixtureNumber = 0
function New-Fixture {
    $script:fixtureNumber++
    $root = Join-Path $work ('g' + $script:fixtureNumber)
    $pkg = Join-Path $work ('p' + $script:fixtureNumber)
    $depsDir = Join-Path $work ('d' + $script:fixtureNumber)
    New-Item -ItemType Directory -Force -Path $root, $pkg, $depsDir | Out-Null
    Copy-Item -Path (Join-Path $Package '*') -Destination $pkg -Recurse
    Write-TestFile (Join-Path $root 'enshrouded.exe') 'known test executable'
    $originalKfc = 'KFC4' + (([string][char]0) * 24) + [char]1 + (([string][char]0) * 3) + 'original kfc'
    Write-TestFile (Join-Path $root 'enshrouded.kfc') $originalKfc
    Write-TestFile (Join-Path $root 'enshrouded.kfc_resources') 'original resources'
    Write-TestFile (Join-Path $root '.cache\files.json') 'original cache'
    Write-TestFile (Join-Path $root '.cache\types.json') 'original types'
    $deps = Get-Content -LiteralPath (Join-Path $pkg 'backend\dependencies.json') -Raw | ConvertFrom-Json
    $deps.game_sha256 = Get-Sha256Hex (Join-Path $root 'enshrouded.exe')
    $eml = Join-Path $depsDir 'eml-dinput8.dll'
    Write-TestFile $eml 'test EML_LOG_FILE_PATH loader'
    $deps.eml_dll.sha256 = Get-Sha256Hex $eml
    Copy-Item -LiteralPath $fakeCli -Destination (Join-Path $depsDir 'emm.exe')
    $deps.eml_cli.sha256 = Get-Sha256Hex (Join-Path $depsDir 'emm.exe')
    $zipRoot = Join-Path $work ('z' + $script:fixtureNumber)
    foreach ($entry in $deps.emf_files) {
        $value = if ($entry.path -eq 'dinput8.dll') { 'EMF-LOADER-7d3c9f test loader' } else { 'test file ' + $entry.path }
        $at = Join-Path $zipRoot ('wrapper\game\' + $entry.path)
        Write-TestFile $at $value
        $entry.sha256 = Get-Sha256Hex $at
    }
    $zip = Join-Path $depsDir $deps.emf_zip.file
    [IO.Compression.ZipFile]::CreateFromDirectory($zipRoot, $zip)
    $deps.emf_zip.sha256 = Get-Sha256Hex $zip
    [IO.File]::WriteAllText((Join-Path $pkg 'backend\dependencies.json'), ($deps | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
    return @{ Game = $root; Package = $pkg; Dependencies = $depsDir; Specs = $deps; OriginalKfc = $originalKfc; Scratch = (Join-Path $work ('s' + $script:fixtureNumber)) }
}
function Install-Fixture($Fixture, [scriptblock]$Injector = $null) {
    $plan = Get-InstallPlan $Fixture.Game $Fixture.Package
    New-Item -ItemType Directory -Force -Path $Fixture.Scratch | Out-Null
    Invoke-SpeedModInstall $plan $Fixture.Scratch $Fixture.Dependencies $true $Injector
}
function Assert-Originals($Fixture) {
    Assert-True ([IO.File]::ReadAllText((Join-Path $Fixture.Game 'enshrouded.kfc')) -eq $Fixture.OriginalKfc) 'KFC rollback'
    Assert-True ([IO.File]::ReadAllText((Join-Path $Fixture.Game 'enshrouded.kfc_resources')) -eq 'original resources') 'resource rollback'
    Assert-True ([IO.File]::ReadAllText((Join-Path $Fixture.Game '.cache\files.json')) -eq 'original cache') 'cache rollback'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $Fixture.Game 'enshrouded.kfc.bak'))) 'new KFC backup removed on rollback'
}

$fresh = New-Fixture
$result = Install-Fixture $fresh
Assert-True $result.success 'fresh install'
$chain = @(Get-ChainEntries $fresh.Game)
Assert-True ($chain.Count -eq 2 -and $chain[0] -eq 'speedmod_bootstrap.dll' -and $chain[1] -eq 'speedmod_eml.dll') 'canonical startup order'
Assert-True ((Get-Sha256Hex (Join-Path $fresh.Game 'speedmod_eml.dll')) -eq $fresh.Specs.eml_dll.sha256) 'verified EML copied'
Assert-True ([IO.File]::ReadAllText((Join-Path $fresh.Game 'enshrouded.kfc')) -eq ($fresh.OriginalKfc + '-patched')) 'patcher was run'
Assert-True (Test-Path -LiteralPath (Join-Path $result.backup 'files\enshrouded.kfc')) 'backup retained'
Write-Output 'PASS fresh install, dependency extraction, startup order, patch generation, backups'

$config = Join-Path $fresh.Game 'emf\mods\speedmod_boots\config.ini'
Write-TestFile $config 'custom settings must stay'
$chainBefore = [IO.File]::ReadAllText((Join-Path $fresh.Game 'dinput8-chain.ini'))
$null = Install-Fixture $fresh
Assert-True ([IO.File]::ReadAllText($config) -eq 'custom settings must stay') 'custom config preserved'
Assert-True ([IO.File]::ReadAllText((Join-Path $fresh.Game 'dinput8-chain.ini')) -eq $chainBefore) 'reinstall does not grow or duplicate chain'
Write-Output 'PASS repeat install preserves user configuration and keeps chain stable'

$foreign = New-Fixture
Write-TestFile (Join-Path $foreign.Game 'dinput8.dll') 'foreign loader preserve me'
Write-TestFile (Join-Path $foreign.Game 'dinput8-chain.ini') "[Chain]`r`nother.dll`r`n; keep comment`r`n[Other]`r`nvalue=stay`r`n"
Write-TestFile (Join-Path $foreign.Game 'mods\Unrelated\data.txt') 'unrelated mod'
$plan = Get-InstallPlan $foreign.Game $foreign.Package
$movedName = $plan.MovePlan.Name
$null = Install-Fixture $foreign
Assert-True ([IO.File]::ReadAllText((Join-Path $foreign.Game $movedName)) -eq 'foreign loader preserve me') 'foreign proxy retained'
Assert-True (@(Get-ChainEntries $foreign.Game) -contains $movedName) 'foreign proxy chained'
Assert-True (@(Get-ChainEntries $foreign.Game) -contains 'other.dll') 'old chain entry retained'
Assert-True ([IO.File]::ReadAllText((Join-Path $foreign.Game 'mods\Unrelated\data.txt')) -eq 'unrelated mod') 'other mods untouched'
Write-Output 'PASS existing foreign proxy, chain sections/comments, unrelated mods preserved'

$oldEml = New-Fixture
Write-TestFile (Join-Path $oldEml.Game 'dinput8.dll') 'old EML_LOG_FILE_PATH proxy'
Write-TestFile (Join-Path $oldEml.Game 'dbghelp.dll') 'old EML_LOG_FILE_PATH dbghelp'
Write-TestFile (Join-Path $oldEml.Game 'old-eml.dll') 'older EML_LOG_FILE_PATH proxy'
Write-TestFile (Join-Path $oldEml.Game 'dinput8-chain.ini') "[Chain]`r`nold-eml.dll`r`n"
$null = Install-Fixture $oldEml
Assert-True (-not (Test-Path -LiteralPath (Join-Path $oldEml.Game 'dbghelp.dll'))) 'auto-loaded duplicate proxy parked'
Assert-True (Test-Path -LiteralPath (Join-Path $oldEml.Game 'SpeedModInstaller\disabled-loaders\eml-dbghelp.dll')) 'old proxy preserved'
Assert-True (@(Get-ChainEntries $oldEml.Game).Count -eq 2) 'one EML instance remains'
Assert-True ([IO.File]::ReadAllText((Join-Path $oldEml.Game 'old-eml.dll')) -eq 'older EML_LOG_FILE_PATH proxy') 'older EML DLL not deleted'
Write-Output 'PASS old EML and duplicate dbghelp preserve bytes while preventing duplicate initialization'

foreach ($stage in @('after-files', 'after-patch')) {
    $fixture = New-Fixture
    Write-TestFile (Join-Path $fixture.Game 'dinput8.dll') 'original foreign proxy'
    Write-TestFile (Join-Path $fixture.Game 'dinput8-chain.ini') "[Chain]`r`nkeep.dll`r`n"
    $script:failureStage = $stage
    Assert-Throws { Install-Fixture $fixture { param($at) if ($at -eq $script:failureStage) { throw 'injected installer failure' } } } 'injected failure propagates'
    Assert-Originals $fixture
    Assert-True ([IO.File]::ReadAllText((Join-Path $fixture.Game 'dinput8.dll')) -eq 'original foreign proxy') 'foreign proxy rollback'
    Assert-True ([IO.File]::ReadAllText((Join-Path $fixture.Game 'dinput8-chain.ini')) -eq "[Chain]`r`nkeep.dll`r`n") 'chain rollback'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture.Game 'speedmod_bootstrap.dll'))) 'new mod files removed on rollback'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture.Game 'mods\SpeedMod'))) 'no empty mod folder left to break EML'
}
Write-Output 'PASS failures after copying and after patching restore original loader, KFC, resources, cache and chain'

$patchFailure = New-Fixture
Write-TestFile (Join-Path $patchFailure.Game 'fail-patch-test') 'yes'
Assert-Throws { Install-Fixture $patchFailure } 'patcher exit failure'
Assert-Originals $patchFailure
Write-Output 'PASS patcher nonzero exit restores original data'

$bad = New-Fixture
Write-TestFile (Join-Path $bad.Game 'enshrouded.exe') 'different game'
Assert-Throws { Get-InstallPlan $bad.Game $bad.Package } 'unsupported executable refused'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $bad.Game 'SpeedModInstaller'))) 'preflight refusal does not create install backup'

$bad = New-Fixture
Write-TestFile (Join-Path $bad.Game 'emf\emf_core.dll') 'different framework'
Assert-Throws { Get-InstallPlan $bad.Game $bad.Package } 'unsupported existing EMF refused'

$bad = New-Fixture
Write-TestFile (Join-Path $bad.Dependencies 'eml-dinput8.dll') 'tampered download'
Assert-Throws { Install-Fixture $bad } 'dependency hash mismatch refused offline'
Assert-Originals $bad

$bad = New-Fixture
Write-TestFile (Join-Path $bad.Package 'mod\speedmod_bootstrap.dll') 'tampered payload'
Assert-Throws { Get-InstallPlan $bad.Game $bad.Package } 'payload hash mismatch refused'
Assert-Throws { Get-SafeGamePath $bad.Game '..\outside.dll' } 'parent traversal refused'
Assert-Throws { Get-SafeGamePath $bad.Game 'C:\outside.dll' } 'absolute destination refused'
Write-Output 'PASS unsupported builds/frameworks, tampered dependencies/payloads and escaping paths are rejected'

$withSaves = New-Fixture
$save = Join-Path $env:EMF_TEST_STEAM_ROOT 'userdata\123\1203620\remote\character0.ksd'
Write-TestFile $save 'saved character bytes'
$saveHash = Get-Sha256Hex $save
$result = Install-Fixture $withSaves
Assert-True ($result.save_files_backed_up -gt 0) 'character save found'
Assert-True ((Get-Sha256Hex $save) -eq $saveHash) 'save source untouched'
Assert-True ((Get-Sha256Hex (Join-Path $result.backup 'saves\steam\123\character0.ksd')) -eq $saveHash) 'save copied byte-exactly'
Write-Output 'PASS saves copied and verified without changing originals'

function Remove-Fixture($Fixture, [scriptblock]$Injector = $null) {
    $plan = Get-SpeedModRemovalPlan $Fixture.Game $Fixture.Package $true
    Invoke-SpeedModRemove $plan $Fixture.Scratch $Fixture.Dependencies $true $Injector
}
function Get-FixtureHashes([string]$Root) {
    $hashes = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse -Force) {
        $name = $file.FullName.Substring($Root.Length + 1)
        if (-not $name.StartsWith('SpeedModInstaller\backups\')) { $hashes[$name] = Get-Sha256Hex $file.FullName }
    }
    return $hashes
}
function Assert-FixtureHashes($Before, $After) {
    Assert-True ($Before.Count -eq $After.Count) 'rollback leaves the exact same file set'
    foreach ($key in $Before.Keys) { Assert-True ($Before[$key] -eq $After[$key]) ('rollback byte identity: ' + $key) }
}
$remove = New-Fixture
$null = Install-Fixture $remove
Write-TestFile (Join-Path $remove.Game 'dinput8-chain.ini') "[Chain]`r`nspeedmod_bootstrap.dll`r`nspeedmod_eml.dll`r`nother.dll`r`n; keep me`r`n[Other]`r`nspeedmod_bootstrap.dll`r`n"
Write-TestFile (Join-Path $remove.Game 'mods\Other\mod.json') '{"id":"Other"}'
Write-TestFile (Join-Path $remove.Game 'mods\Other\src\mod.lua') 'other patch'
Write-TestFile (Join-Path $remove.Game 'emf\mods\other\config.ini') 'other runtime'
Write-TestFile (Join-Path $remove.Game 'mods\SpeedMod\src\config.lua') 'custom own setting'
# Removal must also work with an independently updated EMF; do not touch its files.
Write-TestFile (Join-Path $remove.Game 'emf\emf_core.dll') 'updated EMF preserve me'
$before = Get-FixtureHashes $remove.Game
Assert-Throws { Get-SpeedModRemovalPlan $remove.Game $remove.Package $false } 'no removal without explicit item restoration acknowledgement'
Assert-FixtureHashes $before (Get-FixtureHashes $remove.Game)
$removed = Remove-Fixture $remove
Assert-True $removed.removed 'remove succeeded'
foreach ($name in @('mods\SpeedMod', 'emf\mods\speedmod_boots', 'speedmod_bootstrap.dll')) {
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $remove.Game $name))) ('own content removed: ' + $name)
}
foreach ($name in @('dinput8.dll', 'speedmod_eml.dll', 'emf\emf_core.dll', 'mods\Other\mod.json', 'mods\Other\src\mod.lua', 'emf\mods\other\config.ini', 'enshrouded.exe')) {
    Assert-True ($before[$name] -eq (Get-Sha256Hex (Join-Path $remove.Game $name))) ('shared or unrelated file preserved: ' + $name)
}
$expectedChain = "[Chain]`r`nspeedmod_eml.dll`r`nother.dll`r`n; keep me`r`n[Other]`r`nspeedmod_bootstrap.dll`r`n"
Assert-True ([IO.File]::ReadAllText((Join-Path $remove.Game 'dinput8-chain.ini')) -eq $expectedChain) 'remove only own active bootstrap entry'
Assert-True (Test-Path -LiteralPath (Join-Path $removed.backup 'files\mods\SpeedMod\src\config.lua')) 'own custom config backed up'
Assert-True ([IO.File]::ReadAllText((Join-Path $remove.Game 'enshrouded.kfc')).EndsWith('-patched-patched')) 'remaining data mods repatched'
Assert-True ((Get-Sha256Hex $save) -eq $saveHash) 'remove never changes saves'
Write-Output 'PASS removal requires restoration acknowledgement and preserves EML, updated EMF, other mods, other chain sections and saves'

foreach ($stage in @('after-files', 'after-patch', 'patcher-error')) {
    $fixture = New-Fixture
    $null = Install-Fixture $fixture
    Write-TestFile (Join-Path $fixture.Game 'enshrouded_001.dat') 'existing other mod container'
    Write-TestFile (Join-Path $fixture.Game 'write-container-test') 'yes'
    if ($stage -eq 'patcher-error') { Write-TestFile (Join-Path $fixture.Game 'fail-patch-test') 'yes' }
    $before = Get-FixtureHashes $fixture.Game
    $script:failureStage = $stage
    Assert-Throws { Remove-Fixture $fixture { param($at) if ($at -eq $script:failureStage) { throw 'injected removal failure' } } } 'removal rollback trigger'
    Assert-FixtureHashes $before (Get-FixtureHashes $fixture.Game)
}
Write-Output 'PASS removal failure restores complete mod folders/config, chain, data/cache and overwritten other-mod containers; new containers removed'

$mismatch = New-Fixture
$null = Install-Fixture $mismatch
Write-TestFile (Join-Path $mismatch.Game 'mods\SpeedMod\mod.json') '{"id":"SomeoneElse"}'
$before = Get-FixtureHashes $mismatch.Game
Assert-Throws { Remove-Fixture $mismatch } 'unrelated mod in own folder is refused'
Assert-FixtureHashes $before (Get-FixtureHashes $mismatch.Game)
Write-Output 'PASS conflicting mod identity rejected without writes'
Write-Output ('ALL INSTALLER TESTS PASSED: ' + $work)
