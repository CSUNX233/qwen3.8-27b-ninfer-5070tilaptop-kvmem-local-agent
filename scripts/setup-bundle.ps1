param(
    [ValidateSet('huihui','gsq','huihui-vision')][string]$Model,
    [string]$ModelPath,
    [string]$ModelId,
    [switch]$SkipDsh
)
. (Join-Path $PSScriptRoot 'common.ps1')
Initialize-LocalEnvironment
$manifest = Get-Content -LiteralPath (Join-Path $script:ProjectRoot 'offline-bundle.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Model -and $ModelPath) { throw 'Choose a bundled Model or an external ModelPath, not both.' }
if (-not $Model -and -not $ModelPath) {
    if ($manifest.default_model) { $Model = $manifest.default_model }
    else { $ModelPath = (Read-Host 'Full path to your text+MTP v3 .ninfer model').Trim('"') }
}
$visionCPU = $false
if ($Model) {
    $selected = @($manifest.models | Where-Object { $_.id -eq $Model })
    if ($selected.Count -ne 1) { throw 'Selected bundled model is missing.' }
    $ModelPath = $selected[0].path
    $ModelId = $selected[0].model_id
    $visionCPU = $selected[0].vision_cpu -eq $true
}
if (-not $ModelPath -or -not (Test-Path -LiteralPath (Resolve-ProjectPath $ModelPath) -PathType Leaf)) { throw 'Model file is missing.' }
if ([IO.Path]::GetExtension($ModelPath) -ne '.ninfer') { throw 'Expected a v3 .ninfer model.' }
if (-not $ModelId) { $ModelId = [IO.Path]::GetFileNameWithoutExtension($ModelPath) }
$active = @(Get-DeploymentState | Where-Object { $_ -and (Test-OwnedProcess $_) })
if ($active.Count) { throw 'Use stop.cmd before installing or switching this bundle.' }
$configPath = Join-Path $script:ProjectRoot 'config/local.json'
if (Test-Path -LiteralPath $configPath) { $config = Read-LocalConfig }
else { $config = Get-Content -LiteralPath (Join-Path $script:ProjectRoot 'config/example.json') -Raw -Encoding UTF8 | ConvertFrom-Json }
$config.ModelPath = $ModelPath
$config.ModelId = $ModelId
$config | Add-Member -NotePropertyName VisionCPU -NotePropertyValue $visionCPU -Force
Write-LocalJson $configPath $config
& (Join-Path $script:ProjectRoot 'setup.ps1') -ModelPath $ModelPath -Offline -SkipDsh:$SkipDsh
Write-Host 'Offline setup complete. Run 02-start.cmd. Driver and Microsoft Edge are system prerequisites; neither was changed.'
