[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$topologyCache = "$PSScriptRoot/../runtime/cache/gpu_topology.json"
$dgpuName = ""
$dgpuDriver = ""
$igpuName = ""
$igpuDriver = ""

if (Test-Path $topologyCache) {
    try {
        $cached = Get-Content $topologyCache -Raw -Encoding UTF8 | ConvertFrom-Json
        $dgpuName = [string]$cached.dgpuName
        $dgpuDriver = [string]$cached.dgpuDriver
        $igpuName = [string]$cached.igpuName
        $igpuDriver = [string]$cached.igpuDriver
    } catch {}
}

if (-not $dgpuName -and -not $igpuName) {
    $videoControllers = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue
    if ($videoControllers) {
        foreach ($vc in $videoControllers) {
            if ($vc.Status -eq 'OK' -and $vc.Name -notmatch 'Virtual|Remote|Oray|MuMu|RDP|Idd') {
                if ($vc.Name -match 'NVIDIA|GeForce|RTX') {
                    $dgpuName = [string]$vc.Name
                    $dgpuDriver = [string]$vc.DriverVersion
                } elseif ($vc.Name -match 'Intel|UHD|Iris|Radeon' -and $vc.Name -notmatch 'RTX|GeForce|RX') {
                    $igpuName = [string]$vc.Name
                    $igpuDriver = [string]$vc.DriverVersion
                }
            }
        }
    }
    try {
        $cacheDir = Split-Path $topologyCache
        if (-not (Test-Path $cacheDir)) { New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null }
        @{ dgpuName = $dgpuName; dgpuDriver = $dgpuDriver; igpuName = $igpuName; igpuDriver = $igpuDriver } | ConvertTo-Json | Set-Content -Path $topologyCache -Encoding UTF8
    } catch {}
}

$isHybrid = ($dgpuName -ne "") -and ($igpuName -ne "")

$regPath = "HKCU:\Software\Microsoft\DirectX\UserGpuPreferences"
$userPrefs = [System.Collections.Hashtable]::new()
if (Test-Path $regPath) {
    $regItem = Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue
    if ($regItem) {
        foreach ($prop in $regItem.PSObject.Properties) {
            if ($prop.Name -notmatch '^(PS|_)') {
                $userPrefs[$prop.Name.ToLower()] = [string]$prop.Value
            }
        }
    }
}

$procMap = [System.Collections.Hashtable]::new()
$allProcesses = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue
if ($allProcesses) {
    foreach ($procItem in $allProcesses) {
        $procId = [int]$procItem.ProcessId
        $pPath = if ($procItem.ExecutablePath) { [string]$procItem.ExecutablePath } else { "" }
        $pref = 0
        if ($pPath -ne "" -and $userPrefs.ContainsKey($pPath.ToLower())) {
            $val = [string]$userPrefs[$pPath.ToLower()]
            if ($val -match 'GpuPreference=1') { $pref = 1 }
            elseif ($val -match 'GpuPreference=2') { $pref = 2 }
        }
        $procMap[$procId] = @{
            Name = [string]$procItem.Name
            Path = $pPath
            Preference = $pref
        }
    }
}

$dgpuPids = [System.Collections.Hashtable]::new()
try {
    $smiOut = & nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>$null
    if ($smiOut) {
        foreach ($line in $smiOut) {
            $sp = $line.Split(',')
            if ($sp.Length -ge 1) {
                $dgpuPids[[int]$sp[0].Trim()] = $true
            }
        }
    }
} catch {}

$dedicatedSamples = (Get-Counter '\GPU Process Memory(*)\Dedicated Usage' -ErrorAction SilentlyContinue).CounterSamples
$sharedSamples = (Get-Counter '\GPU Process Memory(*)\Shared Usage' -ErrorAction SilentlyContinue).CounterSamples

$detectedDgpuLuid = $null
if ($dedicatedSamples) {
    foreach ($s in $dedicatedSamples) {
        if ($s.CookedValue -gt 1048576) {
            if ($s.InstanceName -match 'pid_(\d+)_luid_(0x[0-9a-fA-F]+_0x[0-9a-fA-F]+)') {
                $pidVal = [int]$matches[1]
                if ($dgpuPids.ContainsKey($pidVal)) {
                    $detectedDgpuLuid = $matches[2].ToLower()
                    break
                }
            }
        }
    }
}

$procDgpuMap = [System.Collections.Hashtable]::new()
$procIgpuMap = [System.Collections.Hashtable]::new()

if ($dedicatedSamples) {
    foreach ($s in $dedicatedSamples) {
        if ($s.CookedValue -gt 1048576) {
            if ($s.InstanceName -match 'pid_(\d+)_luid_(0x[0-9a-fA-F]+_0x[0-9a-fA-F]+)') {
                $pidVal = [int]$matches[1]
                $luid = $matches[2].ToLower()
                if ($null -ne $detectedDgpuLuid -and $luid -eq $detectedDgpuLuid) {
                    if (-not $procDgpuMap.ContainsKey($pidVal)) { $procDgpuMap[$pidVal] = [int64]0 }
                    $procDgpuMap[$pidVal] += [int64]$s.CookedValue
                }
            }
        }
    }
}

if ($sharedSamples) {
    foreach ($s in $sharedSamples) {
        if ($s.CookedValue -gt 5242880) {
            if ($s.InstanceName -match 'pid_(\d+)_luid_(0x[0-9a-fA-F]+_0x[0-9a-fA-F]+)') {
                $pidVal = [int]$matches[1]
                $luid = $matches[2].ToLower()
                if ($null -eq $detectedDgpuLuid -or $luid -ne $detectedDgpuLuid) {
                    if (-not $procIgpuMap.ContainsKey($pidVal)) { $procIgpuMap[$pidVal] = [int64]0 }
                    $procIgpuMap[$pidVal] += [int64]$s.CookedValue
                }
            }
        }
    }
}

function Classify-ProcessName([string]$procName) {
    $n = $procName.ToLower()
    if ($n -match 'msedge|chrome|firefox|brave|opera|safari') { return "browser" }
    if ($n -match 'qq|wechat|weixin|wxwork|dingtalk|feishu|telegram|discord|slack') { return "im" }
    if ($n -match 'cloudmusic|spotify|potplayer|vlc|bilibili|douyin|kugou|qqmusic') { return "media" }
    if ($n -match 'code|devenv|idea64|pycharm|git|cargo|rustc|powershell|cmd|node') { return "dev" }
    if ($n -match 'system|dwm|explorer|csrss|lsass|svchost|searchhost|shellexperiencehost|taskmgr|startmenuexperiencehost') { return "system" }
    return "other"
}

$dgpuList = [System.Collections.Generic.List[PSObject]]::new()
$igpuList = [System.Collections.Generic.List[PSObject]]::new()

foreach ($pidKey in $procDgpuMap.Keys) {
    $info = $procMap[$pidKey]
    $pName = if ($info) { $info.Name } else { "PID $pidKey" }
    $pPath = if ($info) { $info.Path } else { "" }
    $pref = if ($info) { $info.Preference } else { 0 }
    $vram = $procDgpuMap[$pidKey]
    $cat = Classify-ProcessName $pName
    $isSys = ($cat -eq "system") -or ($pName -match '^(dwm|csrss|system|explorer)\.exe$')

    $dgpuList.Add([PSCustomObject]@{
        pid = $pidKey
        name = $pName
        exe_path = $pPath
        vram_bytes = $vram
        vram_mb = [math]::Round($vram / 1MB, 1)
        category = $cat
        is_system = $isSys
        gpu_target = "dgpu"
        preference = $pref
    })
}

foreach ($pidKey in $procIgpuMap.Keys) {
    $info = $procMap[$pidKey]
    $pName = if ($info) { $info.Name } else { "PID $pidKey" }
    $pPath = if ($info) { $info.Path } else { "" }
    $pref = if ($info) { $info.Preference } else { 0 }
    $vram = $procIgpuMap[$pidKey]
    $cat = Classify-ProcessName $pName
    $isSys = ($cat -eq "system") -or ($pName -match '^(dwm|csrss|system|explorer)\.exe$')

    $igpuList.Add([PSCustomObject]@{
        pid = $pidKey
        name = $pName
        exe_path = $pPath
        vram_bytes = $vram
        vram_mb = [math]::Round($vram / 1MB, 1)
        category = $cat
        is_system = $isSys
        gpu_target = "igpu"
        preference = $pref
    })
}

$result = [PSCustomObject]@{
    topology = [PSCustomObject]@{
        is_hybrid_supported = $isHybrid
        dgpu_name = $dgpuName
        dgpu_driver = $dgpuDriver
        igpu_name = $igpuName
        igpu_driver = $igpuDriver
    }
    dgpu_processes = @($dgpuList | Sort-Object vram_bytes -Descending)
    igpu_processes = @($igpuList | Sort-Object vram_bytes -Descending)
}

$result | ConvertTo-Json -Depth 4 -Compress

