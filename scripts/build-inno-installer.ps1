# SPDX-License-Identifier: Apache-2.0
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$dist = Join-Path $root "dist"
if (-not (Test-Path -LiteralPath $dist)) { New-Item -ItemType Directory -Path $dist | Out-Null }
$staging = Join-Path $dist "package_staging"
$isccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
    "C:\Users\14113\AppData\Local\Programs\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}
if (-not $iscc) {
    throw "ISCC.exe not found in standard paths or PATH. Please install Inno Setup 6."
}

Write-Host "[1/6] Preparing staging directory: $staging"
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "runtime/engine") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "runtime/node") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "runtime/logs") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "runtime/cache") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "runtime/temp") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "config") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "templates") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "scripts") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "integrations") -Force | Out-Null

Write-Host "[2/6] Copying Release desktop executable..."
$desktopExe = Join-Path $root "desktop/src-tauri/target/release/Pmod.exe"
if (-not (Test-Path -LiteralPath $desktopExe)) {
    $desktopExe = Join-Path $root "desktop/src-tauri/target/release/pmod.exe"
}
if (-not (Test-Path -LiteralPath $desktopExe)) {
    throw "Release desktop binary not found at $desktopExe. Run cargo build --release first."
}
Copy-Item -LiteralPath $desktopExe -Destination (Join-Path $staging "Pmod.exe") -Force

Write-Host "[3/6] Copying Ninfer engine and CUDA runtime DLLs..."
$engineDir = Join-Path $root "runtime/engine"
$engineFiles = @(
    "ninfer-serve.exe",
    "cublas64_13.dll",
    "cublasLt64_13.dll",
    "avcodec-63.dll",
    "avformat-63.dll",
    "avutil-61.dll",
    "swresample-7.dll",
    "swscale-10.dll"
)
foreach ($f in $engineFiles) {
    $src = Join-Path $engineDir $f
    if (Test-Path -LiteralPath $src) {
        Write-Host "  + $f"
        Copy-Item -LiteralPath $src -Destination (Join-Path $staging "runtime/engine/$f") -Force
    } else {
        throw "Missing required engine dependency: $f"
    }
}

Write-Host "[4/6] Copying portable Node.js runtime..."
$nodeSrc = (Get-Command node.exe -ErrorAction Stop).Source
Copy-Item -LiteralPath $nodeSrc -Destination (Join-Path $staging "runtime/node/node.exe") -Force

Write-Host "[5/6] Packaging DSH agent workspace..."
$dshArchive = Join-Path $staging "integrations/dsh.tar.gz"
tar --exclude=dsh/local-web/edge-profile --exclude=dsh/output -czf $dshArchive -C (Join-Path $root "integrations") dsh
Write-Host "  + dsh.tar.gz ($([math]::Round((Get-Item $dshArchive).Length / 1MB, 2)) MB)"

Copy-Item -LiteralPath (Join-Path $root "integrations/cache_guard.mjs") -Destination (Join-Path $staging "integrations/cache_guard.mjs") -Force
Copy-Item -LiteralPath (Join-Path $root "scripts/configure-dsh.mjs") -Destination (Join-Path $staging "scripts/configure-dsh.mjs") -Force
Copy-Item -LiteralPath (Join-Path $root "scripts/get-gpu-process-vram.ps1") -Destination (Join-Path $staging "scripts/get-gpu-process-vram.ps1") -Force
if (Test-Path -LiteralPath (Join-Path $root "scripts/common.ps1")) {
    Copy-Item -LiteralPath (Join-Path $root "scripts/common.ps1") -Destination (Join-Path $staging "scripts/common.ps1") -Force
}
foreach ($cmdFile in @("start.ps1", "stop.ps1", "start.cmd", "stop.cmd")) {
    if (Test-Path -LiteralPath (Join-Path $root $cmdFile)) {
        Copy-Item -LiteralPath (Join-Path $root $cmdFile) -Destination (Join-Path $staging $cmdFile) -Force
    }
}
Copy-Item -Path (Join-Path $root "templates/*") -Destination (Join-Path $staging "templates") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root "config/example.json") -Destination (Join-Path $staging "config/example.json") -Force
# Sanitize config/local.json for public installer distribution (clear build machine's absolute private paths)
$stagedLocalJson = Join-Path $staging "config/local.json"
$localCfgObj = Get-Content (Join-Path $root "config/local.json") -Raw | ConvertFrom-Json
if ($localCfgObj.ModelPath -and (-not (Test-Path -LiteralPath (Join-Path $staging $localCfgObj.ModelPath)))) {
    $localCfgObj.ModelPath = ""
}
$localCfgObj | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $stagedLocalJson -Encoding utf8
Copy-Item -LiteralPath (Join-Path $root "desktop/src-tauri/icons/icon.ico") -Destination (Join-Path $staging "icon.ico") -Force

Write-Host "[6/6] Generating Inno Setup script & compiling..."
$issFile = Join-Path $dist "setup.iss"

$issLines = [System.Collections.Generic.List[string]]::new()
$issLines.Add('#define MyAppName "Pmod"')
$issLines.Add('#define MyAppVersion "1.0.0"')
$issLines.Add('#define MyAppPublisher "复读机 (QQ: 785029497)"')
$issLines.Add('#define MyAppExeName "Pmod.exe"')
$issLines.Add('')
$issLines.Add('[Setup]')
$issLines.Add('AppId={{D387A654-7B09-4E61-8C37-A109E2B5A9C0}')
$issLines.Add('AppName={#MyAppName}')
$issLines.Add('AppVersion={#MyAppVersion}')
$issLines.Add('AppPublisher={#MyAppPublisher}')
$issLines.Add('DefaultDirName={localappdata}\Programs\Pmod')
$issLines.Add('DefaultGroupName={#MyAppName}')
$issLines.Add('OutputDir=' + $dist)
$issLines.Add('OutputBaseFilename=Pmod-Setup-v1.0.0')
$issLines.Add('SetupIconFile=' + (Join-Path $staging "icon.ico"))
$issLines.Add('Compression=lzma2/normal')
$issLines.Add('SolidCompression=yes')
$issLines.Add('ArchitecturesInstallIn64BitMode=x64compatible')
$issLines.Add('DisableProgramGroupPage=yes')
$issLines.Add('PrivilegesRequired=lowest')
$issLines.Add('CloseApplications=force')
$issLines.Add('UninstallDisplayIcon={app}\icon.ico')
$issLines.Add('ShowLanguageDialog=auto')
$issLines.Add('')
$issLines.Add('[Languages]')
$issLines.Add('Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"')
$issLines.Add('Name: "english"; MessagesFile: "compiler:Default.isl"')
$issLines.Add('')
$issLines.Add('[Tasks]')
$issLines.Add('Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"')
$issLines.Add('')
$issLines.Add('[Files]')
$issLines.Add('Source: "' + $staging + '\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs')
$issLines.Add('')
$issLines.Add('[Icons]')
$issLines.Add('Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\icon.ico"')
$issLines.Add('Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\icon.ico"; Tasks: desktopicon')
$issLines.Add('')
$issLines.Add('[Run]')
$issLines.Add('Filename: "tar.exe"; Parameters: "-xzf ""{app}\integrations\dsh.tar.gz"" -C ""{app}\integrations"""; Flags: runhidden waituntilterminated')
$issLines.Add('Filename: "cmd.exe"; Parameters: "/c del ""{app}\integrations\dsh.tar.gz"" & if not exist ""{app}\runtime\dsh-home\profiles\web"" mkdir ""{app}\runtime\dsh-home\profiles\web"""; Flags: runhidden waituntilterminated')
$issLines.Add('Filename: "{app}\runtime\node\node.exe"; Parameters: """{app}\scripts\configure-dsh.mjs"""; WorkingDir: "{app}"; Flags: runhidden waituntilterminated')
$issLines.Add('Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent')

$utf8WithBom = [System.Text.UTF8Encoding]::new($true)
[System.IO.File]::WriteAllLines($issFile, $issLines, $utf8WithBom)

Write-Host ">>> Compiling with Inno Setup (ISCC.exe)..."
& $iscc "/Qp" $issFile
if ($LASTEXITCODE -ne 0) {
    throw "ISCC compilation failed with exit code $LASTEXITCODE"
}

$outputExe = Join-Path $dist "Pmod-Setup-v1.0.0.exe"
if (Test-Path -LiteralPath $outputExe) {
    $info = Get-Item -LiteralPath $outputExe
    $sizeMb = [math]::Round($info.Length / 1MB, 2)
    Write-Host "=========================================================="
    Write-Host "SUCCESS! Standalone installer generated:"
    Write-Host "Path: $($info.FullName)"
    Write-Host "Size: $sizeMb MB"
    Write-Host "=========================================================="
} else {
    throw "Output file not found at $outputExe"
}

