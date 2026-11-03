param(
    [Parameter(Mandatory = $true)][string]$Game,
    [string]$Package = (Split-Path -Parent $PSScriptRoot),
    [string]$Report = '',
    [string]$DependencyDirectory = '',
    [switch]$CheckOnly,
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Installer.Core.ps1')
$exitCode = 0
$result = $null
$mutex = $null
$owned = $false
try {
    $root = [IO.Path]::GetFullPath($Game).TrimEnd('\')
    $bytes = [Text.Encoding]::Unicode.GetBytes($root.ToLowerInvariant())
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $key = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') } finally { $sha.Dispose() }
    $mutex = New-Object Threading.Mutex($false, ('Local\SpeedModInstaller.' + $key))
    try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { throw '다른 SpeedMod 설치가 진행 중입니다.' }
    Write-InstallStatus 1 '게임 버전과 설치 폴더를 확인합니다.'
    $plan = Get-InstallPlan $root $Package
    if ($CheckOnly) {
        $result = [pscustomobject]@{ success = $true; check_only = $true; game = $root; version = $plan.Manifest.Dependencies.mod_version; required_bytes = $plan.RequiredBytes }
    } else {
        $scratch = Join-Path $Package 'work'
        New-Item -ItemType Directory -Force -Path $scratch | Out-Null
        $result = Invoke-SpeedModInstall $plan $scratch $DependencyDirectory ([bool]$Offline)
    }
} catch {
    $exitCode = 1
    $errorText = $_.Exception.Message
    $needsElevation = $errorText -match 'UnauthorizedAccess|access.*denied|액세스.*거부|권한'
    $result = [pscustomobject]@{ success = $false; error = $errorText; needs_elevation = [bool]$needsElevation }
    Write-Host $errorText
} finally {
    if ($owned) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
if ($Report) { [IO.File]::WriteAllText([IO.Path]::GetFullPath($Report), ($result | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding $false)) }
if (-not $Report) { $result | ConvertTo-Json -Depth 5 }
exit $exitCode
