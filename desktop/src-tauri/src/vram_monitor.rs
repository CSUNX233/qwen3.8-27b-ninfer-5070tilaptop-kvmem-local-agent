use serde::{Deserialize, Serialize};
use std::process::Command;
use std::sync::Mutex;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

static GPU_STATS_CACHE: Mutex<Option<(Instant, GpuStats)>> = Mutex::new(None);
static VRAM_BREAKDOWN_CACHE: Mutex<Option<(Instant, VramBreakdown)>> = Mutex::new(None);

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GpuStats {
    pub total_mb: u64,
    pub used_mb: u64,
    pub free_mb: u64,
    pub gpu_util: u32,
    pub temp_c: u32,
    #[serde(default)]
    pub gpu_name: String,
    #[serde(default)]
    pub compute_cap: String,
    #[serde(default)]
    pub driver_version: String,
}

#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct GpuTopology {
    pub is_hybrid_supported: bool,
    pub dgpu_name: String,
    pub dgpu_driver: String,
    pub igpu_name: String,
    pub igpu_driver: String,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct ProcessVramInfo {
    pub pid: u32,
    pub name: String,
    pub exe_path: String,
    pub vram_mb: f64,
    pub vram_bytes: u64,
    pub category: String, // "engine" | "browser" | "im" | "media" | "dev" | "system" | "driver" | "other"
    pub is_critical_system: bool,
    pub gpu_target: String, // "dgpu" | "igpu"
    pub preference: u32,    // 0: 默认, 1: 核显节能, 2: 独显高性能
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct VramBreakdown {
    pub total_mb: u64,
    pub used_mb: u64,
    pub free_mb: u64,
    pub topology: GpuTopology,
    pub dgpu_processes: Vec<ProcessVramInfo>,
    pub igpu_processes: Vec<ProcessVramInfo>,
    pub processes: Vec<ProcessVramInfo>, // 向后兼容
    pub timestamp: u64,
}

#[derive(Deserialize, Debug)]
struct RawScriptOutput {
    topology: Option<GpuTopology>,
    dgpu_processes: Option<Vec<RawProcessInfo>>,
    igpu_processes: Option<Vec<RawProcessInfo>>,
}

#[derive(Deserialize, Debug)]
struct RawProcessInfo {
    pid: u32,
    name: String,
    #[serde(default)]
    exe_path: String,
    vram_bytes: u64,
    vram_mb: f64,
    #[serde(default)]
    category: String,
    #[serde(default)]
    is_system: bool,
    #[serde(default)]
    gpu_target: String,
    #[serde(default)]
    preference: u32,
}

pub fn query_gpu_stats() -> Option<GpuStats> {
    if let Ok(guard) = GPU_STATS_CACHE.lock() {
        if let Some((timestamp, ref stats)) = *guard {
            if timestamp.elapsed().as_millis() < 1800 {
                return Some(stats.clone());
            }
        }
    }

    #[cfg(windows)]
    use std::os::windows::process::CommandExt;

    let mut cmd = Command::new("nvidia-smi");
    cmd.args([
        "--query-gpu=memory.total,memory.used,memory.free,utilization.gpu,temperature.gpu,name,compute_cap,driver_version",
        "--format=csv,noheader,nounits",
    ]);

    #[cfg(windows)]
    cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW

    let output = cmd.output().ok()?;
    if !output.status.success() {
        return None;
    }

    let text = String::from_utf8_lossy(&output.stdout);
    let first_line = text.lines().next().unwrap_or(text.trim());
    let parts: Vec<&str> = first_line.trim().split(',').map(|s| s.trim()).collect();
    if parts.len() < 5 {
        return None;
    }

    let total_mb = parts[0].parse::<u64>().unwrap_or(0);
    let used_mb = parts[1].parse::<u64>().unwrap_or(0);
    let free_mb = parts[2].parse::<u64>().unwrap_or(0);
    let gpu_util = parts[3].parse::<u32>().unwrap_or(0);
    let temp_c = parts[4].parse::<u32>().unwrap_or(0);
    let gpu_name = parts.get(5).copied().unwrap_or("").to_string();
    let compute_cap = parts.get(6).copied().unwrap_or("").to_string();
    let driver_version = parts.get(7).copied().unwrap_or("").to_string();

    let stats = GpuStats {
        total_mb,
        used_mb,
        free_mb,
        gpu_util,
        temp_c,
        gpu_name,
        compute_cap,
        driver_version,
    };

    if let Ok(mut guard) = GPU_STATS_CACHE.lock() {
        *guard = Some((Instant::now(), stats.clone()));
    }

    Some(stats)
}

fn map_raw_to_info(raw: RawProcessInfo) -> ProcessVramInfo {
    let lower = raw.name.to_lowercase();
    let my_pid = std::process::id();
    let is_critical = raw.is_system
        || raw.pid == my_pid
        || raw.pid <= 4
        || lower.contains("pmod")
        || lower.contains("qwen-desktop")
        || lower.contains("ninfer-serve")
        || matches!(
            lower.as_str(),
            "dwm.exe"
                | "dwm"
                | "csrss.exe"
                | "csrss"
                | "explorer.exe"
                | "explorer"
                | "system"
                | "smss.exe"
                | "services.exe"
                | "lsass.exe"
        );

    let category = if !raw.category.is_empty() && raw.category != "other" {
        raw.category
    } else if lower.contains("ninfer") || (lower.contains("python") && lower.contains("qwen")) {
        "engine".to_string()
    } else if lower.contains("msedge") || lower.contains("chrome") || lower.contains("firefox") {
        "browser".to_string()
    } else if lower.contains("qq") || lower.contains("weixin") || lower.contains("wechat") {
        "im".to_string()
    } else if lower.contains("cloudmusic") || lower.contains("spotify") || lower.contains("bilibili") {
        "media".to_string()
    } else if lower.contains("code") || lower.contains("idea") {
        "dev".to_string()
    } else if is_critical {
        "system".to_string()
    } else {
        "other".to_string()
    };

    ProcessVramInfo {
        pid: raw.pid,
        name: raw.name,
        exe_path: raw.exe_path,
        vram_mb: raw.vram_mb,
        vram_bytes: raw.vram_bytes,
        category,
        is_critical_system: is_critical,
        gpu_target: raw.gpu_target,
        preference: raw.preference,
    }
}

pub fn query_vram_breakdown() -> Result<VramBreakdown, String> {
    if let Ok(guard) = VRAM_BREAKDOWN_CACHE.lock() {
        if let Some((timestamp, ref breakdown)) = *guard {
            if timestamp.elapsed().as_millis() < 4000 {
                return Ok(breakdown.clone());
            }
        }
    }

    #[cfg(windows)]
    use std::os::windows::process::CommandExt;

    // 1. Get base GPU metrics from nvidia-smi
    let gpu = query_gpu_stats().unwrap_or(GpuStats {
        total_mb: 12288,
        used_mb: 0,
        free_mb: 12288,
        gpu_util: 0,
        temp_c: 0,
        gpu_name: String::new(),
        compute_cap: String::new(),
        driver_version: String::new(),
    });

    // 2. Locate or create PowerShell script
    let mut script_candidates = Vec::new();
    let repo_root = crate::config_manager::get_repo_root();
    script_candidates.push(repo_root.join("scripts").join("get-gpu-process-vram.ps1"));
    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(parent) = exe_path.parent() {
            script_candidates.push(parent.join("scripts").join("get-gpu-process-vram.ps1"));
            script_candidates.push(parent.join("../scripts").join("get-gpu-process-vram.ps1"));
            script_candidates.push(parent.join("../../scripts").join("get-gpu-process-vram.ps1"));
        }
    }

    let final_script_path = script_candidates
        .into_iter()
        .find(|p| p.exists())
        .ok_or_else(|| "找不到 get-gpu-process-vram.ps1 脚本".to_string())?;

    let mut cmd = Command::new("powershell");
    cmd.args([
        "-NoProfile",
        "-NonInteractive",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        &final_script_path.to_string_lossy(),
    ]);

    #[cfg(windows)]
    cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW

    let output = cmd.output().map_err(|e| format!("PowerShell 执行失败: {}", e))?;
    let stdout_str = String::from_utf8_lossy(&output.stdout);
    let trimmed = stdout_str.trim();

    let parsed: RawScriptOutput = serde_json::from_str(trimmed)
        .or_else(|_| {
            // 兼容返回单一数组的情况
            serde_json::from_str::<Vec<RawProcessInfo>>(trimmed).map(|list| RawScriptOutput {
                topology: None,
                dgpu_processes: Some(list),
                igpu_processes: Some(Vec::new()),
            })
        })
        .unwrap_or(RawScriptOutput {
            topology: None,
            dgpu_processes: Some(Vec::new()),
            igpu_processes: Some(Vec::new()),
        });

    let topology = parsed.topology.unwrap_or(GpuTopology {
        is_hybrid_supported: false,
        dgpu_name: "NVIDIA GPU".to_string(),
        dgpu_driver: String::new(),
        igpu_name: "Integrated GPU".to_string(),
        igpu_driver: String::new(),
    });

    let dgpu_processes: Vec<ProcessVramInfo> = parsed
        .dgpu_processes
        .unwrap_or_default()
        .into_iter()
        .map(map_raw_to_info)
        .collect();

    let igpu_processes: Vec<ProcessVramInfo> = parsed
        .igpu_processes
        .unwrap_or_default()
        .into_iter()
        .map(map_raw_to_info)
        .collect();

    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    let res = VramBreakdown {
        total_mb: gpu.total_mb,
        used_mb: gpu.used_mb,
        free_mb: gpu.free_mb,
        topology,
        dgpu_processes: dgpu_processes.clone(),
        igpu_processes,
        processes: dgpu_processes, // 兼容现有代码
        timestamp,
    };

    if let Ok(mut guard) = VRAM_BREAKDOWN_CACHE.lock() {
        *guard = Some((Instant::now(), res.clone()));
    }

    Ok(res)
}

/// 设置 Windows 注册表图形首选项
/// target: "igpu" (节能，GpuPreference=1;) | "dgpu" (高性能，GpuPreference=2;) | "default" (恢复系统默认)
pub fn set_process_gpu_preference(exe_path: &str, target: &str) -> Result<(), String> {
    #[cfg(windows)]
    use std::os::windows::process::CommandExt;

    if exe_path.is_empty() {
        return Err("可执行文件路径不能为空".to_string());
    }

    let reg_key = r"HKCU\Software\Microsoft\DirectX\UserGpuPreferences";

    let mut cmd = Command::new("reg");
    if target == "default" {
        cmd.args(["delete", reg_key, "/v", exe_path, "/f"]);
    } else {
        let pref_val = if target == "igpu" {
            "GpuPreference=1;"
        } else {
            "GpuPreference=2;"
        };
        cmd.args(["add", reg_key, "/v", exe_path, "/t", "REG_SZ", "/d", pref_val, "/f"]);
    }

    #[cfg(windows)]
    cmd.creation_flags(0x08000000);

    let output = cmd.output().map_err(|e| format!("写入注册表失败: {}", e))?;
    if !output.status.success() {
        // 如果是恢复默认操作 (target == "default")，若该应用原本就不在注册表自定义名单中，
        // reg delete 会返回错误代码 1（系统找不到指定的注册表项或值），这本就代表已经是默认状态，应视作成功。
        if target == "default" {
            return Ok(());
        }
        let err = String::from_utf8_lossy(&output.stderr);
        return Err(format!("注册表操作失败: {}", err));
    }

    Ok(())
}

/// 迁移应用到目标显卡（方案 A：设置注册表并平滑自动重启）
pub fn migrate_process_gpu(
    pid: u32,
    exe_path: Option<String>,
    target: &str,
    restart: bool,
) -> Result<String, String> {
    #[cfg(windows)]
    use std::os::windows::process::CommandExt;

    if pid <= 4 {
        return Err("禁止迁移系统内核进程 (PID <= 4)".to_string());
    }

    let my_pid = std::process::id();
    if pid == my_pid {
        return Err("禁止迁移当前工作台主程序".to_string());
    }

    // 1. 确定 exe 路径
    let real_exe_path = if let Some(p) = exe_path {
        if !p.is_empty() {
            p
        } else {
            query_exe_path_by_pid(pid)?
        }
    } else {
        query_exe_path_by_pid(pid)?
    };

    let exe_name = std::path::Path::new(&real_exe_path)
        .file_name()
        .and_then(|f| f.to_str())
        .unwrap_or("应用")
        .to_string();

    let lower_name = exe_name.to_lowercase();
    if lower_name.contains("dwm")
        || lower_name.contains("explorer")
        || lower_name.contains("csrss")
        || lower_name.contains("ninfer-serve")
    {
        return Err(format!("系统核心应用或模型服务 [{}] 禁止迁移", exe_name));
    }

    // 2. 写入注册表图形首选项
    set_process_gpu_preference(&real_exe_path, target)?;

    // 3. 若要求平滑重启 (方案 A)
    if restart {
        // 先尝试通过优雅关闭主窗口 (WM_CLOSE)
        let graceful_close_script = format!(
            r#"
            try {{
                $p = Get-Process -Id {} -ErrorAction SilentlyContinue
                if ($p) {{
                    $p.CloseMainWindow() | Out-Null
                    Start-Sleep -Milliseconds 800
                }}
            }} catch {{}}
            "#,
            pid
        );

        let mut ps_cmd = Command::new("powershell");
        ps_cmd.args(["-NoProfile", "-Command", &graceful_close_script]);
        #[cfg(windows)]
        ps_cmd.creation_flags(0x08000000);
        let _ = ps_cmd.output();

        // 确保清理旧进程
        let mut kill_cmd = Command::new("taskkill");
        kill_cmd.args(["/F", "/T", "/PID", &pid.to_string()]);
        #[cfg(windows)]
        kill_cmd.creation_flags(0x08000000);
        let _ = kill_cmd.output();

        // 等待 300 毫秒后拉起新进程
        std::thread::sleep(std::time::Duration::from_millis(300));

        let mut spawn_cmd = Command::new(&real_exe_path);
        let spawn_res = spawn_cmd.spawn();
        if let Err(e) = spawn_res {
            return Ok(format!(
                "已成功写入注册表首选项，但自动拉起新进程失败: {}。请手动重新打开 {}",
                e, exe_name
            ));
        }

        let target_name = if target == "igpu" {
            "核显 (节能)"
        } else if target == "dgpu" {
            "独显 (高性能)"
        } else {
            "系统默认"
        };

        Ok(format!(
            "已成功将 [{}] 调度至 {} 并平滑重启生效！",
            exe_name, target_name
        ))
    } else {
        Ok(format!(
            "已写入图形首选项为 {}，将在下次手动启动 [{}] 时生效。",
            target, exe_name
        ))
    }
}

/// 一键批量将所有常驻后台应用（聊天、媒体、浏览器）迁移至核显
pub fn batch_migrate_to_igpu(categories: Vec<String>) -> Result<Vec<String>, String> {
    let breakdown = query_vram_breakdown()?;
    let mut migrated_apps = Vec::new();

    for proc in breakdown.dgpu_processes {
        if proc.is_critical_system || proc.pid <= 4 {
            continue;
        }

        let cat = &proc.category;
        let should_migrate = categories.iter().any(|c| c == cat || c == "all");
        if should_migrate && !proc.exe_path.is_empty() {
            if let Ok(_) = migrate_process_gpu(proc.pid, Some(proc.exe_path), "igpu", true) {
                migrated_apps.push(proc.name);
            }
        }
    }

    Ok(migrated_apps)
}

/// 一键批量将核显中的第三方应用切回独显
pub fn batch_migrate_to_dgpu(categories: Vec<String>) -> Result<Vec<String>, String> {
    let breakdown = query_vram_breakdown()?;
    let mut migrated_apps = Vec::new();

    for proc in breakdown.igpu_processes {
        if proc.is_critical_system || proc.pid <= 4 {
            continue;
        }

        let cat = &proc.category;
        let should_migrate = categories.iter().any(|c| c == cat || c == "all");
        if should_migrate && !proc.exe_path.is_empty() {
            if let Ok(_) = migrate_process_gpu(proc.pid, Some(proc.exe_path), "dgpu", true) {
                migrated_apps.push(proc.name);
            }
        }
    }

    Ok(migrated_apps)
}

fn query_exe_path_by_pid(pid: u32) -> Result<String, String> {
    #[cfg(windows)]
    use std::os::windows::process::CommandExt;

    let ps_cmd_str = format!(
        r#"(Get-CimInstance Win32_Process -Filter "ProcessId = {}").ExecutablePath"#,
        pid
    );

    let mut cmd = Command::new("powershell");
    cmd.args(["-NoProfile", "-Command", &ps_cmd_str]);
    #[cfg(windows)]
    cmd.creation_flags(0x08000000);

    let output = cmd.output().map_err(|e| format!("查询进程路径失败: {}", e))?;
    let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if path.is_empty() {
        Err(format!("无法定位 PID {} 的可执行文件完整路径", pid))
    } else {
        Ok(path)
    }
}

pub fn terminate_process(pid: u32, name: Option<String>) -> Result<(), String> {
    #[cfg(windows)]
    use std::os::windows::process::CommandExt;

    if pid <= 4 {
        return Err("禁止终止 Windows 核心内核进程 (PID <= 4)".to_string());
    }

    let my_pid = std::process::id();
    if pid == my_pid {
        return Err("禁止终止当前工作台主程序".to_string());
    }

    // 1. First attempt: Tree kill with /T to kill parent & all child subprocesses
    let mut cmd = Command::new("taskkill");
    cmd.args(["/F", "/T", "/PID", &pid.to_string()]);

    #[cfg(windows)]
    cmd.creation_flags(0x08000000);

    let _ = cmd.output();

    // 2. If name is a known third-party app (not a system critical name), ensure all sibling worker processes are cleaned
    if let Some(n) = name {
        let lower = n.to_lowercase();
        let safe_to_kill_by_name = !matches!(
            lower.as_str(),
            "dwm"
                | "explorer"
                | "csrss"
                | "lsass"
                | "services"
                | "smss"
                | "system"
                | "textinputhost"
                | "applicationframehost"
                | "searchhost"
                | "startmenuexperiencehost"
                | "shellexperiencehost"
                | "taskmgr"
                | "pmod"
                | "pmod.exe"
                | "qwen-desktop"
                | "msedgewebview2"
                | "conhost"
                | "svchost"
        );
        if safe_to_kill_by_name {
            let img_name = if lower.ends_with(".exe") {
                n
            } else {
                format!("{}.exe", n)
            };
            let mut img_cmd = Command::new("taskkill");
            img_cmd.args(["/F", "/IM", &img_name]);
            #[cfg(windows)]
            img_cmd.creation_flags(0x08000000);
            let _ = img_cmd.output();
        }
    }

    Ok(())
}
