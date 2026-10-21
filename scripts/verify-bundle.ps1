param([switch]$SkipModels)
$ErrorActionPreference = 'Stop'
$bundleRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifestPath = Join-Path $bundleRoot 'offline-bundle.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'This is not a complete offline bundle: offline-bundle.json is missing.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema -ne 1) { throw 'Unsupported offline bundle manifest.' }
$count = 0
$bundleHasher = [Security.Cryptography.SHA256]::Create()
try {
foreach ($file in $manifest.files) {
    if ($SkipModels -and $file.path.StartsWith('models/')) { continue }
    if ([IO.Path]::IsPathRooted($file.path) -or $file.path -match '(^|[/\\])\.\.([/\\]|$)|:') { throw 'Invalid bundle path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $bundleRoot $file.path))
    if (-not $path.StartsWith($bundleRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Bundle path escapes installation.' }
    $bundleFile = New-Object IO.FileInfo($path)
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
