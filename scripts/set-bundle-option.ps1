param(
    [ValidateSet('huihui','gsq','huihui-vision')][string]$Model,
    [ValidateSet('answer','carry-over')][string]$PenaltyScope,
    [ValidateSet('nvfp4','rk4v4')][string]$KVDType,
    [ValidateSet('default','small')][string]$PoolPreset
)
. (Join-Path $PSScriptRoot 'common.ps1')
Initialize-LocalEnvironment
$active = @(Get-DeploymentState | Where-Object { $_ -and (Test-OwnedProcess $_) })
if ($active.Count) { throw 'Run stop.cmd first. Settings were not changed.' }
$config = Read-LocalConfig
if ($Model) {
    $manifest = Get-Content -LiteralPath (Join-Path $script:ProjectRoot 'offline-bundle.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $selected = @($manifest.models | Where-Object { $_.id -eq $Model })
    if ($selected.Count -ne 1 -or -not (Test-Path -LiteralPath (Resolve-ProjectPath $selected[0].path))) { throw 'Bundled model is missing.' }
    $config.ModelPath = $selected[0].path; $config.ModelId = $selected[0].model_id
    $config | Add-Member -NotePropertyName VisionCPU -NotePropertyValue ($selected[0].vision_cpu -eq $true) -Force
}
if ($PenaltyScope) { $config | Add-Member -NotePropertyName AnswerPenaltyScope -NotePropertyValue $PenaltyScope -Force }
if ($KVDType) { $config | Add-Member -NotePropertyName KVDType -NotePropertyValue $KVDType -Force }
if ($PoolPreset) {
    $config.DeviceKV = if ($PoolPreset -eq 'small') { 34560 } else { 36864 }
    $config.RetrieveTokens = if ($PoolPreset -eq 'small') { 1536 } else { 3840 }
    $config.SinkTokens = 12288; $config.RecentTokens = 19456
    Write-Host 'Pool preset changes retrieval capacity. Logical context stays unchanged; validate recall before long tasks.'
}
Write-LocalJson (Join-Path $script:ProjectRoot 'config/local.json') $config
Write-Host 'Saved. Use start.cmd to start with the new setting. No service was launched.'
