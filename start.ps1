param([switch]$EngineOnly, [switch]$NoOpen, [string]$Config = 'config/local.json')
. (Join-Path $PSScriptRoot 'scripts/common.ps1')
Initialize-LocalEnvironment
$cfg = Read-LocalConfig $Config
$engine = Resolve-ProjectPath $cfg.EnginePath
$model = Resolve-ProjectPath $cfg.ModelPath
if (-not (Test-Path -LiteralPath $engine) -or -not (Test-Path -LiteralPath $model)) { throw 'Engine/model missing. Run setup.ps1 first.' }
$launchMutex = Enter-LaunchMutex
try {
$oldState = @(Get-DeploymentState | Where-Object { $_ -and (Test-OwnedProcess $_) })
if ($oldState.Count) { throw 'This checkout already has running processes. Use stop.cmd before restarting.' }
Assert-PortFree $cfg.EnginePort
if (-not $EngineOnly) { Assert-PortFree $cfg.GuardPort; Assert-PortFree $cfg.WebPort }
$records = New-Object 'System.Collections.Generic.List[object]'
try {
    $env:CUDA_MODULE_LOADING='LAZY'
    $env:NINFER_KVMEM_INDEX_HOST='1'
    $env:NINFER_KV_RING='1'
    $env:NINFER_KV_WINDOW=[string]$cfg.RecentTokens
    $env:NINFER_KV_RETRIEVE=[string]$cfg.RetrieveTokens
    $env:NINFER_KV_SINK=[string]$cfg.SinkTokens
    $env:NINFER_KV_REUSE_HOSTBACKED='1'
    $env:NINFER_HOST_PAGEABLE='1'
    $env:NINFER_KV_LEASE_RING='1'
    $env:NINFER_KVMEM_HARVEST_TEXT='1'
    $engineArgs=@($model,'--model-id',$cfg.ModelId,'--chat-template',(Join-Path $PSScriptRoot 'templates/chat_template_qwen3.8-enhanced.jinja'),
        '--max-context',[string]$cfg.MaxContext,'--kv-capacity',[string]$cfg.DeviceKV,'--max-concurrency','1','--max-shared-prefixes','0',
        '--kv-dtype','rk4v4','--host-kv-mib',[string]$cfg.HostKVMiB,'--cuda-graph-allowance-mib','64','--prefill-chunk','128',
        '--gdn-state-fp16','--spec','mtp','--draft-tokens','3','--default-max-tokens','0','--kv-lease-growth',
        '--default-reasoning-effort',$cfg.ReasoningEffort,'--presence-penalty','1.0','--post-thinking-sampler','temp=0.7,top_p=0.95,presence=1.0',
        '--recover-invariant-failures','--context-cache-policy','rolling','--release-diverged-checkpoints','--recency-eviction',
        '--device-state-slots','0','--no-webui','--log-stats-interval-ms','5000','--host','127.0.0.1','--port',[string]$cfg.EnginePort)
    $records.Add((Start-OwnedProcess 'engine' $engine $engineArgs $model (Split-Path $engine -Parent)))
    Write-LocalJson (Join-Path $script:RuntimeRoot 'processes.json') @($records.ToArray())
    Write-Host 'Loading model and CUDA graphs ...'
    $ready=$false
    $deadline=[datetime]::UtcNow.AddSeconds(300)
    while ([datetime]::UtcNow -lt $deadline) {
        if (-not (Test-OwnedProcess $records[0])) { throw 'Engine exited. See runtime/logs/engine.stderr.log.' }
        try { $health=Invoke-RestMethod -Uri ('http://127.0.0.1:'+$cfg.EnginePort+'/health') -TimeoutSec 2; $ready=$true; break } catch {}
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'Engine startup timed out. See runtime/logs/engine.stderr.log.' }
    Write-Host ('API ready: http://127.0.0.1:'+$cfg.EnginePort+'/v1')
    if (-not $EngineOnly) {
        $node=(Get-Command node.exe -ErrorAction Stop).Source
        $dshDir=Join-Path $PSScriptRoot 'integrations/dsh'
        $dshEntry=Join-Path $dshDir 'node_modules/@deepseek-ai/dsh/lib/bin.js'
        if (-not (Test-Path -LiteralPath $dshEntry)) { throw 'Run setup.ps1 without -SkipDsh first.' }
        & $node (Join-Path $PSScriptRoot 'scripts/configure-dsh.mjs')
        if ($LASTEXITCODE -ne 0) { throw 'dsh configuration failed.' }
        $env:GUARD_PORT=[string]$cfg.GuardPort
        $env:GUARD_UPSTREAM='http://127.0.0.1:'+$cfg.EnginePort
        $env:GUARD_LOG=Join-Path $script:LogRoot 'cache_guard.log'
        $env:GUARD_MAX_TOKENS='210000'
        $env:GUARD_STRICT='1'
        $env:DSH_PRUNE_AT_OCCURRENCE='1'
        $guard=Join-Path $PSScriptRoot 'integrations/cache_guard.mjs'
        $records.Add((Start-OwnedProcess 'guard' $node @($guard) $guard $dshDir))
        $records.Add((Start-OwnedProcess 'dsh' $node @($dshEntry,'web','--no-open','--port',[string]$cfg.WebPort) $dshEntry $dshDir))
        Write-LocalJson (Join-Path $script:RuntimeRoot 'processes.json') @($records.ToArray())
        $webUrl=$null
        $deadline=[datetime]::UtcNow.AddSeconds(120)
        while ([datetime]::UtcNow -lt $deadline) {
            if (-not (Test-OwnedProcess $records[2])) { throw 'dsh exited. See runtime/logs/dsh.stderr.log.' }
            $log=[string](Get-Content -LiteralPath (Join-Path $script:LogRoot 'dsh.stdout.log') -Raw -ErrorAction SilentlyContinue)
            if ($log) {
                $m=[regex]::Match($log,('(?m)^dsh web: (http://127\.0\.0\.1:'+$cfg.WebPort+'/\?token=[A-Za-z0-9_-]+)'))
                if ($m.Success) {
                    try { $null=Invoke-WebRequest -Uri $m.Groups[1].Value -UseBasicParsing -TimeoutSec 2; $webUrl=$m.Groups[1].Value; break } catch {}
                }
            }
            Start-Sleep -Seconds 1
        }
        if (-not $webUrl) { throw 'dsh did not announce a working login URL. See runtime/logs.' }
        if (-not $NoOpen) { Start-Process -FilePath $webUrl | Out-Null }
        Write-Host ('Agent ready: http://127.0.0.1:'+$cfg.WebPort+' (private login URL is in runtime/logs/dsh.stdout.log)')
    }
    Write-Host 'Use stop.cmd to close only the processes started by this checkout.'
} catch {
    for ($i=$records.Count-1;$i -ge 0;$i--) { Stop-OwnedProcess $records[$i] }
    Write-LocalJson (Join-Path $script:RuntimeRoot 'processes.json') @()
    throw
}
} finally {
    $launchMutex.ReleaseMutex()
    $launchMutex.Dispose()
}
