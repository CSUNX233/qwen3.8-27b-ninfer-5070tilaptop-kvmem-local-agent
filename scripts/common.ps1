$ErrorActionPreference = 'Stop'
$script:ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Resolve-ProjectPath([string]$Value) {
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath((Join-Path $script:ProjectRoot $Value))
}

function Initialize-LocalEnvironment {
    $script:RuntimeRoot = Join-Path $script:ProjectRoot 'runtime'
    $script:LogRoot = Join-Path $script:RuntimeRoot 'logs'
    foreach ($name in @('logs','cache','temp','dsh-home')) {
        New-Item -ItemType Directory -Path (Join-Path $script:RuntimeRoot $name) -Force | Out-Null
    }
    $env:TEMP = Join-Path $script:RuntimeRoot 'temp'
    $env:TMP = $env:TEMP
    $env:NPM_CONFIG_CACHE = Join-Path $script:RuntimeRoot 'cache/npm'
    $env:PLAYWRIGHT_BROWSERS_PATH = Join-Path $script:RuntimeRoot 'cache/playwright'
    $env:PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD = '1'
    $env:DSH_HOME = Join-Path $script:RuntimeRoot 'dsh-home'
    $env:NINFER_LOCAL_KEY = 'local'
    $bundledNode = Join-Path $script:RuntimeRoot 'node'
    if (Test-Path -LiteralPath (Join-Path $bundledNode 'node.exe') -PathType Leaf) {
        if (@($env:PATH -split ';') -notcontains $bundledNode) { $env:PATH = $bundledNode + ';' + $env:PATH }
    }
}

function Read-LocalConfig([string]$Path = 'config/local.json') {
    $resolved = Resolve-ProjectPath $Path
    if (-not (Test-Path -LiteralPath $resolved)) { throw 'Run setup.ps1 -ModelPath <your-model.ninfer> first.' }
    return Get-Content -LiteralPath $resolved -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-LocalJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 15), (New-Object Text.UTF8Encoding($false)))
}

function Enter-LaunchMutex {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $key = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($script:ProjectRoot.ToLowerInvariant()))).Replace('-','').Substring(0,24) }
    finally { $sha.Dispose() }
    $mutex = New-Object Threading.Mutex($false, ('Local\NInferPublic-' + $key))
    try { $owned = $mutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { $mutex.Dispose(); throw 'This checkout is already starting or stopping. Wait for that window to finish.' }
    return $mutex
}

function Get-PinnedArchive($Spec) {
    $target = Join-Path $script:RuntimeRoot ('cache/' + $Spec.name)
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Output ('Downloading ' + $Spec.name + ' ...') | Out-Host
        $partial = $target + '.part'
        Invoke-WebRequest -Uri $Spec.url -OutFile $partial -UseBasicParsing -TimeoutSec 1800
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Spec.sha256) {
            throw ('Download checksum mismatch: ' + $Spec.name)
        }
        Move-Item -LiteralPath $partial -Destination $target -Force
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $Spec.sha256) {
        throw ('Cached archive checksum mismatch: ' + $target)
    }
    return $target
}

function Test-ExpectedFile([string]$Path, [string]$Hash) {
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Hash)
}

function Assert-PortFree([int]$Port) {
    if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
        throw ('Port ' + $Port + ' is already in use. Stop the other server or change config/local.json. No existing process was stopped.')
    }
}

function Get-DeploymentState {
    $p = Join-Path $script:RuntimeRoot 'processes.json'
    if (Test-Path -LiteralPath $p) { return @(Get-Content -LiteralPath $p -Raw | ConvertFrom-Json) }
    return @()
}

function Test-OwnedProcess($Record) {
    $proc = Get-Process -Id $Record.id -ErrorAction SilentlyContinue
    if (-not $proc) { return $false }
    if ([string]$proc.StartTime.ToUniversalTime().Ticks -ne [string]$Record.startTicks) { return $false }
    $info = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$Record.id)
    return $info -and $info.ExecutablePath -eq $Record.executable -and $info.CommandLine.Contains([string]$Record.marker)
}

function Stop-OwnedProcess($Record) {
    if (-not (Test-OwnedProcess $Record)) { return }
    # Only descendants of our still-matching recorded process are eligible.
    $all = @(Get-CimInstance Win32_Process)
    $queue = New-Object 'System.Collections.Generic.List[int]'
    $queue.Add([int]$Record.id)
    for ($i = 0; $i -lt $queue.Count; $i++) {
        foreach ($child in @($all | Where-Object { $_.ParentProcessId -eq $queue[$i] })) {
            if (-not $queue.Contains([int]$child.ProcessId)) { $queue.Add([int]$child.ProcessId) }
        }
    }
    for ($i = $queue.Count - 1; $i -ge 0; $i--) {
        $proc = Get-Process -Id $queue[$i] -ErrorAction SilentlyContinue
        $info = $all | Where-Object { $_.ProcessId -eq $queue[$i] } | Select-Object -First 1
        if ($proc -and $info -and [math]::Abs(($proc.StartTime.ToUniversalTime() - $info.CreationDate.ToUniversalTime()).TotalMilliseconds) -lt 1) {
            Stop-Process -Id $proc.Id -ErrorAction SilentlyContinue
        }
    }
}

function Start-OwnedProcess([string]$Name, [string]$Exe, [string[]]$Arguments, [string]$Marker, [string]$WorkingDirectory) {
    # Start-Process flattens argument arrays on Windows; quote each path explicitly.
    $quoted = @($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + $_.Replace('"','\"') + '"' } else { $_ }
    })
    $proc = Start-Process -FilePath $Exe -ArgumentList $quoted -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $script:LogRoot ($Name + '.stdout.log')) `
        -RedirectStandardError (Join-Path $script:LogRoot ($Name + '.stderr.log'))
    $info = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $proc.Id)
    if (-not $info) { throw ($Name + ' exited. See runtime/logs/' + $Name + '.stderr.log') }
    return [pscustomobject]@{name=$Name;id=$proc.Id;startTicks=[string]$proc.StartTime.ToUniversalTime().Ticks;executable=$info.ExecutablePath;marker=$Marker}
}
