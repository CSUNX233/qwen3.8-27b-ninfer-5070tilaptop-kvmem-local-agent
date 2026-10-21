param([ValidateSet('huihui','gsq')][string]$Model = 'huihui', [switch]$SkipDsh)
. (Join-Path $PSScriptRoot 'common.ps1')
Initialize-LocalEnvironment
$manifest = Get-Content -LiteralPath (Join-Path $script:ProjectRoot 'offline-bundle.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$selected = @($manifest.models | Where-Object { $_.id -eq $Model })
if ($selected.Count -ne 1) { throw 'Selected bundled model is missing.' }
$active = @(Get-DeploymentState | Where-Object { $_ -and (Test-OwnedProcess $_) })
if ($active.Count) { throw 'Use stop.cmd before installing or switching this bundle.' }
$configPath = Join-Path $script:ProjectRoot 'config/local.json'
if (Test-Path -LiteralPath $configPath) { $config = Read-LocalConfig }
else { $config = Get-Content -LiteralPath (Join-Path $script:ProjectRoot 'config/example.json') -Raw | ConvertFrom-Json }
$config.ModelPath = $selected[0].path
$config.ModelId = $selected[0].model_id
Write-LocalJson $configPath $config
& (Join-Path $script:ProjectRoot 'setup.ps1') -ModelPath $selected[0].path -Offline -SkipDsh:$SkipDsh
Write-Host 'Offline setup complete. Run 02-start.cmd. Driver and Microsoft Edge are system prerequisites; neither was changed.'
