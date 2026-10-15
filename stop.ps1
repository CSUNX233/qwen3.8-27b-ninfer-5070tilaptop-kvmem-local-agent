. (Join-Path $PSScriptRoot 'scripts/common.ps1')
Initialize-LocalEnvironment
$launchMutex = Enter-LaunchMutex
try {
$records=@(Get-DeploymentState)
for ($i=$records.Count-1;$i -ge 0;$i--) { if ($records[$i]) { Stop-OwnedProcess $records[$i] } }
Write-LocalJson (Join-Path $script:RuntimeRoot 'processes.json') @()
Write-Host 'Stopped this checkout. Model files, sessions and application settings are retained.'
} finally {
    $launchMutex.ReleaseMutex()
    $launchMutex.Dispose()
}
