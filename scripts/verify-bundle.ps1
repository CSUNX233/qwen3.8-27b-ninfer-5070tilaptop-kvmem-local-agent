param([switch]$CheckModels, [switch]$SkipModels)
$ErrorActionPreference = 'Stop'
if ($CheckModels -and $SkipModels) { throw 'Choose CheckModels or SkipModels, not both.' }
$bundleRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifestPath = Join-Path $bundleRoot 'offline-bundle.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'This is not a complete offline bundle: offline-bundle.json is missing.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema -ne 1) { throw 'Unsupported offline bundle manifest.' }
$count = 0
$modelsSkipped = 0
$bundleHasher = [Security.Cryptography.SHA256]::Create()
try {
foreach ($file in $manifest.files) {
    if ([IO.Path]::IsPathRooted($file.path) -or $file.path -match '(^|[/\\])\.\.([/\\]|$)|:') { throw 'Invalid bundle path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $bundleRoot $file.path))
    if (-not $path.StartsWith($bundleRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Bundle path escapes installation.' }
    $bundleFile = New-Object IO.FileInfo($path)
    # Model choices are optional. Setup/start validate the selected artifact separately.
    # SkipModels remains accepted for callers of older bundle versions.
    $isModel = $file.path.Replace('\', '/').StartsWith('models/', [StringComparison]::OrdinalIgnoreCase)
    if ($isModel -and (-not $CheckModels -or -not $bundleFile.Exists)) {
        $modelsSkipped++
        continue
    }
    if (-not $bundleFile.Exists) { throw ('Missing bundled file: ' + $file.path) }
    if ($bundleFile.Length -ne $file.bytes) { throw ('Bundle size mismatch: ' + $file.path + '. Extract a fresh copy; do not bypass verification.') }
    $bundleStream = [IO.File]::OpenRead($path)
    try { $bundleHash = [BitConverter]::ToString($bundleHasher.ComputeHash($bundleStream)).Replace('-', '') }
    finally { $bundleStream.Dispose() }
    if ($bundleHash -ne $file.sha256) {
        throw ('Bundle checksum mismatch: ' + $file.path + '. Extract a fresh copy; do not bypass verification.')
    }
    $count++
    if ($count % 3000 -eq 0) { Write-Host ('Verified ' + $count + ' files ...') }
}
} finally { $bundleHasher.Dispose() }
Write-Host ('Bundle integrity verified: ' + $count + ' files. No inference request was sent.')
Write-Host ('Optional model entries skipped: ' + $modelsSkipped + '. Setup/start require only the selected model. Use -CheckModels to also hash model files that are present.')
