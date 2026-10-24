param([Parameter(Mandatory=$true)][string]$ModelPath, [switch]$SkipDsh, [switch]$Offline)
. (Join-Path $PSScriptRoot 'scripts/common.ps1')
Initialize-LocalEnvironment
if ($Offline) {
    & (Join-Path $PSScriptRoot 'scripts/verify-bundle.ps1')
}
$model = Resolve-ProjectPath $ModelPath
if (-not (Test-Path -LiteralPath $model -PathType Leaf)) { throw ('Model not found: ' + $model) }
if ([IO.Path]::GetExtension($model) -ne '.ninfer') { throw 'The server needs a converted v3 .ninfer artifact. See docs/MODELS.md.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config/downloads.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$engineDir = Join-Path $script:RuntimeRoot 'engine'
New-Item -ItemType Directory -Path $engineDir -Force | Out-Null
foreach ($spec in $manifest.archives) {
    $needed = @($spec.files | Where-Object { -not (Test-ExpectedFile (Join-Path $engineDir $_.name) $_.sha256) })
    if (-not $needed.Count) { Write-Host ('Verified existing ' + $spec.name); continue }
    if ($Offline) { throw ('Bundled runtime file missing or corrupt: ' + $spec.id + '. Extract a fresh complete bundle; no network download was attempted.') }
    $archive = Get-PinnedArchive $spec
    $expanded = Join-Path $script:RuntimeRoot ('cache/extracted-' + $spec.id)
    Expand-Archive -LiteralPath $archive -DestinationPath $expanded -Force
    foreach ($file in $spec.files) {
        $matches = @(Get-ChildItem -LiteralPath $expanded -File -Recurse -Filter $file.name)
        if ($matches.Count -ne 1 -or -not (Test-ExpectedFile $matches[0].FullName $file.sha256)) { throw ('Archive content mismatch: ' + $file.name) }
        Copy-Item -LiteralPath $matches[0].FullName -Destination (Join-Path $engineDir $file.name) -Force
    }
}
$configFile = Join-Path $PSScriptRoot 'config/local.json'
if (Test-Path -LiteralPath $configFile) { $config = Read-LocalConfig } else {
    $config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config/example.json') -Raw -Encoding UTF8 | ConvertFrom-Json
}
$config.ModelPath = $model
Write-LocalJson $configFile $config
$help = & (Join-Path $engineDir 'ninfer-serve.exe') --help 2>&1
if ($LASTEXITCODE -ne 0 -or -not ($help -match 'Usage|usage')) { throw 'Engine dependency check failed; install the x64 Microsoft VC++ runtime and a CUDA 13 compatible NVIDIA driver. See docs/INSTALL.md.' }
if (-not $SkipDsh) {
    $node = (Get-Command node.exe -ErrorAction Stop).Source
    $nodeVersion = (& $node --version | Select-Object -Last 1)
    if ($LASTEXITCODE -ne 0 -or $nodeVersion -notmatch '^v(\d+)\.') { throw 'Unable to read the Node.js version.' }
    if ([int]$Matches[1] -lt 24) { throw 'Install Node.js 24 or newer, then run setup.ps1 again.' }
    $npm = (Get-Command npm.cmd -ErrorAction Stop).Source
    Push-Location (Join-Path $PSScriptRoot 'integrations/dsh')
    try {
        if (-not $Offline) {
            & $npm ci --no-audit --no-fund
            if ($LASTEXITCODE -ne 0) { throw 'dsh npm ci failed.' }
            & $npm ci --prefix local-web --no-audit --no-fund
            if ($LASTEXITCODE -ne 0) { throw 'Local web npm ci failed.' }
        }
        & $node (Join-Path $PSScriptRoot 'scripts/configure-dsh.mjs')
        if ($LASTEXITCODE -ne 0) { throw 'dsh configuration failed.' }
    } finally { Pop-Location }
}
Write-Host 'Setup complete. Run start.cmd (Agent UI) or start.ps1 -EngineOnly (API only).'
