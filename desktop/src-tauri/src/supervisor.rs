use crate::config_manager::{get_repo_root, load_config};
use crate::job_object::ProcessJobGroup;
use serde::{Deserialize, Serialize};
use std::io::{BufRead, BufReader};
#[cfg(windows)]
use std::os::windows::io::AsRawHandle;
#[cfg(windows)]
use std::os::windows::process::CommandExt;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tauri::{AppHandle, Emitter};

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(tag = "status", content = "data")]
pub enum ServiceState {
    Stopped,
    Starting { step: String },
    Running { web_url: String },
    Error { message: String },
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct LogEntry {
    pub source: String,
    pub message: String,
    pub timestamp: String,
}

pub struct Supervisor {
    job: Arc<ProcessJobGroup>,
    state: Arc<Mutex<ServiceState>>,
    app_handle: Option<AppHandle>,
    engine_child: Arc<Mutex<Option<Child>>>,
    guard_child: Arc<Mutex<Option<Child>>>,
    dsh_child: Arc<Mutex<Option<Child>>>,
}

impl Supervisor {
    pub fn new() -> Result<Self, String> {
        let job = Arc::new(ProcessJobGroup::new()?);
        Ok(Self {
            job,
            state: Arc::new(Mutex::new(ServiceState::Stopped)),
            app_handle: None,
            engine_child: Arc::new(Mutex::new(None)),
            guard_child: Arc::new(Mutex::new(None)),
            dsh_child: Arc::new(Mutex::new(None)),
        })
    }

    pub fn set_app_handle(&mut self, handle: AppHandle) {
        self.app_handle = Some(handle);
    }

    pub fn get_state(&self) -> ServiceState {
        self.state.lock().unwrap().clone()
    }

    fn update_state(&self, new_state: ServiceState) {
        *self.state.lock().unwrap() = new_state.clone();
        if let Some(app) = &self.app_handle {
            let _ = app.emit("service-state-changed", new_state);
        }
    }

    fn emit_log(&self, source: &str, message: &str) {
        let entry = LogEntry {
            source: source.to_string(),
            message: message.to_string(),
            timestamp: chrono_now(),
        };
        if let Some(app) = &self.app_handle {
            let _ = app.emit("log-message", entry);
        }
    }

    pub fn stop(&self) {
        self.update_state(ServiceState::Stopped);
        self.emit_log("system", "Stopping all services...");
        self.job.terminate();

        // Kill any direct references
        if let Ok(mut c) = self.engine_child.lock() {
            if let Some(mut child) = c.take() {
                let _ = child.kill();
            }
        }
        if let Ok(mut c) = self.guard_child.lock() {
            if let Some(mut child) = c.take() {
                let _ = child.kill();
            }
        }
        if let Ok(mut c) = self.dsh_child.lock() {
            if let Some(mut child) = c.take() {
                let _ = child.kill();
            }
        }
        if let Ok(cfg) = load_config() {
            #[cfg(windows)]
            cleanup_stale_ports(cfg.engine_port, cfg.guard_port, cfg.web_port);
        }
        self.emit_log("system", "All processes stopped.");
    }

    pub async fn start(self: Arc<Self>) -> Result<(), String> {
        let current = self.get_state();
        if matches!(current, ServiceState::Starting { .. } | ServiceState::Running { .. }) {
            return Err("Service already active".to_string());
        }

        // 1. Immediately emit Starting state so frontend transitions without delay
        self.update_state(ServiceState::Starting {
            step: "正在核验硬件环境与准备加速资源...".to_string(),
        });
        self.emit_log("system", "Starting local model services...");

        // Clean up previous processes without resetting state to Stopped
        let _ = self.job.reset();
        if let Ok(mut c) = self.engine_child.lock() {
            if let Some(mut child) = c.take() {
                let _ = child.kill();
            }
        }
        if let Ok(mut c) = self.guard_child.lock() {
            if let Some(mut child) = c.take() {
                let _ = child.kill();
            }
        }
        if let Ok(mut c) = self.dsh_child.lock() {
            if let Some(mut child) = c.take() {
                let _ = child.kill();
            }
        }

        #[cfg(windows)]
        {
            // Terminate any orphaned ninfer-serve processes to reclaim dedicated GPU VRAM
            let kill_res = std::process::Command::new("taskkill")
                .args(["/F", "/IM", "ninfer-serve.exe"])
                .creation_flags(0x08000000)
                .output();
            if let Ok(output) = kill_res {
                if output.status.success() {
                    // If an orphan was actually killed, wait 500ms for WDDM to reclaim VRAM
                    tokio::time::sleep(Duration::from_millis(500)).await;
                }
            }
        }

        let cfg = load_config()?;
        let repo_root = get_repo_root();

        #[cfg(windows)]
        {
            // Clean up any stale/orphaned processes specifically listening on our allocated ports
            cleanup_stale_ports(cfg.engine_port, cfg.guard_port, cfg.web_port);
        }

        // 1. Resolve paths
        let raw_engine_path = std::path::PathBuf::from(&cfg.engine_path);
        let mut engine_exe = if raw_engine_path.is_absolute() {
            raw_engine_path
        } else {
            repo_root.join(&cfg.engine_path)
        };
        // If user selected sm89 or sm86 and kept default engine_path, auto-prefer arch-specific binary if present
        if cfg.engine_path == "runtime/engine/ninfer-serve.exe" && cfg.gpu_arch != "sm120a" && !cfg.gpu_arch.is_empty() {
            let arch_candidate = repo_root
                .join("runtime")
                .join("engine")
                .join(format!("ninfer-serve-{}.exe", cfg.gpu_arch));
            if arch_candidate.exists() {
                engine_exe = arch_candidate;
            }
        }
        let model_file = std::path::PathBuf::from(&cfg.model_path);
        let chat_template = repo_root.join("templates").join("chat_template_qwen3.8-enhanced.jinja");

        if !engine_exe.exists() {
            let msg = format!("Engine binary not found: {:?}", engine_exe);
            self.update_state(ServiceState::Error { message: msg.clone() });
            return Err(msg);
        }
        if !model_file.exists() {
            let msg = format!("Model file not found: {:?}", model_file);
            self.update_state(ServiceState::Error { message: msg.clone() });
            return Err(msg);
        }

        // Validate model format compatibility before engine launch
        let meta = crate::model_inspector::inspect_model_path(&cfg.model_path);
        if !meta.is_engine_compatible {
            let msg = format!(
                "模型文件格式错误：NInfer 引擎仅支持加载标准 .ninfer 权重制品，当前文件为 {} 格式。{} 请前往「模型」页面更换为 .ninfer 文件。",
                meta.format, meta.compatibility_notice
            );
            self.emit_log("system:err", &msg);
            self.update_state(ServiceState::Error { message: msg.clone() });
            return Err(msg);
        }

        // 2. Launch Engine (or detect existing running instance)
        let client = reqwest::Client::builder()
            .timeout(Duration::from_secs(2))
            .build()
            .unwrap();
        let health_url = format!("http://127.0.0.1:{}/health", cfg.engine_port);

        let mut already_running = false;
        if let Ok(resp) = client.get(&health_url).send().await {
            if resp.status().is_success() {
                already_running = true;
                self.emit_log("system", &format!("Detected existing healthy Ninfer engine on port {}, adopting...", cfg.engine_port));
            }
        }

        if !already_running {
            self.update_state(ServiceState::Starting {
                step: "正在启动 Ninfer 推理引擎并预热 CUDA 图...".to_string(),
            });
            self.emit_log("system", &format!("Launching Engine: {:?}", engine_exe));

            let mut engine_cmd = Command::new(&engine_exe);
            let bundled_engine_dir = repo_root.join("runtime").join("engine");
            // Strictly match start.ps1: Set engine working directory to runtime/engine to load adjacent DLLs
            engine_cmd.current_dir(if bundled_engine_dir.exists() {
                &bundled_engine_dir
            } else {
                engine_exe.parent().unwrap_or(&repo_root)
            });
            if bundled_engine_dir.exists() {
                let current_path = std::env::var("PATH").unwrap_or_default();
                engine_cmd.env("PATH", format!("{};{}", bundled_engine_dir.to_string_lossy(), current_path));
            }
            #[cfg(windows)]
            engine_cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW

            // Environment variables
            engine_cmd.env("CUDA_MODULE_LOADING", "LAZY");
            engine_cmd.env("NINFER_POST_THINKING_PENALTY_SCOPE", &cfg.answer_penalty_scope);
            engine_cmd.env("NINFER_KVMEM_INDEX_HOST", "1");
            engine_cmd.env("NINFER_KV_RING", "1");
            engine_cmd.env("NINFER_KV_WINDOW", cfg.recent_tokens.to_string());
            engine_cmd.env("NINFER_KV_RETRIEVE", cfg.retrieve_tokens.to_string());
            engine_cmd.env("NINFER_KV_SINK", cfg.sink_tokens.to_string());
            engine_cmd.env("NINFER_KV_REUSE_HOSTBACKED", "1");
            engine_cmd.env("NINFER_HOST_PAGEABLE", "1");
            engine_cmd.env("NINFER_KV_LEASE_RING", "1");
            engine_cmd.env("NINFER_KVMEM_HARVEST_TEXT", "1");
            engine_cmd.env("NINFER_KVMEM_INDEX_DECODE", "1");
            engine_cmd.env("NINFER_KVMEM_CACHE_QUERY", "1");
            engine_cmd.env("NINFER_C2_ROLLING_ANCHOR", "1");
            engine_cmd.env("NINFER_C2_CONSUME_ANCHOR", "1");
            engine_cmd.env("NINFER_KV_DEMOTE_BATCH", "32");
            // v1.0.0 Query layer stability
            engine_cmd.env("NINFER_KVMEM_QUERY_MODE", "related-tool-head");
            engine_cmd.env("NINFER_KVMEM_QUERY_ASSEMBLE", "1");
            engine_cmd.env("NINFER_KVMEM_SUFFIX_REFRESH", "tail");

            // Arguments
            engine_cmd.args([
                model_file.to_string_lossy().as_ref(),
                "--model-id", &cfg.model_id,
                "--chat-template", chat_template.to_string_lossy().as_ref(),
                "--max-context", &cfg.max_context.to_string(),
                "--kv-capacity", &cfg.device_k_v.to_string(),
                "--max-concurrency", "1",
                "--max-shared-prefixes", "0",
                "--kv-dtype", &cfg.k_v_d_type,
                "--host-kv-mib", &cfg.host_k_v_mi_b.to_string(),
                "--cuda-graph-allowance-mib", "64",
                "--prefill-chunk", "128",
                "--gdn-state-fp16",
                "--spec", "mtp",
                "--draft-tokens", "3",
                "--default-max-tokens", "0",
                "--kv-lease-growth",
                "--default-reasoning-effort", &cfg.reasoning_effort,
                "--presence-penalty", "1.0",
                "--post-thinking-sampler", "temp=0.7,top_p=0.95,presence=1.0",
                "--recover-invariant-failures",
                "--context-cache-policy", "rolling",
                "--release-diverged-checkpoints",
                "--recency-eviction",
                "--host-state-slots", "8",
                "--auto-long-anchors",
                "--long-anchor-spacing", "49152",
                "--max-long-anchors-per-continuation", "2",
                "--device-state-slots", "0",
                "--no-webui",
                "--log-stats-interval-ms", "5000",
                "--host", "127.0.0.1",
                "--port", &cfg.engine_port.to_string(),
            ]);

            if cfg.vision_c_p_u {
                engine_cmd.args(["--vision-cpu", "--vision-max-merged", "256"]);
            }

            engine_cmd.stdout(Stdio::piped());
            engine_cmd.stderr(Stdio::piped());

            let mut child = engine_cmd.spawn()
                .map_err(|e| format!("Failed to spawn engine: {}", e))?;

            #[cfg(windows)]
            let _ = self.job.assign_raw_handle(child.as_raw_handle());

            // Spawn logging tasks
            if let Some(stdout) = child.stdout.take() {
                let this = Arc::clone(&self);
                tokio::spawn(async move {
                    let reader = BufReader::new(stdout);
                    for line in reader.lines().flatten() {
                        this.emit_log("engine", &line);
                    }
                });
            }
            if let Some(stderr) = child.stderr.take() {
                let this = Arc::clone(&self);
                tokio::spawn(async move {
                    let reader = BufReader::new(stderr);
                    for line in reader.lines().flatten() {
                        this.emit_log("engine:err", &line);
                    }
                });
            }

            *self.engine_child.lock().unwrap() = Some(child);
        }

        // 3. Poll Engine Health
        let mut ready = false;
        for i in 0..120 {
            if let Ok(mut lock) = self.engine_child.lock() {
                if let Some(child) = lock.as_mut() {
                    if let Ok(Some(exit_status)) = child.try_wait() {
                        let msg = format!("Ninfer engine exited with code: {}. Check terminal logs.", exit_status);
                        self.emit_log("system:err", &msg);
                        self.update_state(ServiceState::Error { message: msg.clone() });
                        self.stop();
                        return Err(msg);
                    }
                }
            }

            if let Ok(resp) = client.get(&health_url).send().await {
                if resp.status().is_success() {
                    ready = true;
                    break;
                }
            }
            if i % 10 == 0 {
                self.emit_log("system", &format!("Waiting for Engine healthcheck... ({}/120s)", i));
            }
            tokio::time::sleep(Duration::from_secs(1)).await;
        }

        if !ready {
            let msg = "Engine startup timed out (120s). Check engine logs.".to_string();
            self.update_state(ServiceState::Error { message: msg.clone() });
            self.stop();
            return Err(msg);
        }
        self.emit_log("system", "Engine ready! Configuring DSH agent...");

        // 4. Configure DSH
        self.update_state(ServiceState::Starting {
            step: "正在同步 DSH 智能体环境配置...".to_string(),
        });

        let node_bin = get_node_path(&repo_root);
        self.emit_log("system", &format!("Using Node.js runtime: {:?}", node_bin));

        let dsh_home = repo_root.join("runtime").join("dsh-home");
        let temp_dir = repo_root.join("runtime").join("temp");
        let bundled_node_dir = repo_root.join("runtime").join("node");

        // Guarantee all essential runtime directories exist
        let _ = std::fs::create_dir_all(&temp_dir);
        let _ = std::fs::create_dir_all(repo_root.join("runtime").join("logs"));
        let _ = std::fs::create_dir_all(repo_root.join("runtime").join("cache").join("playwright"));
        let _ = std::fs::create_dir_all(&dsh_home);

        // Remove any legacy NTFS junctions in runtime/dsh-home so file watchers (readdirp/chokidar)
        // never encounter Windows realpath (-4094 UNKNOWN) errors on junctions.
        #[cfg(windows)]
        {
            for legacy_junc in [
                repo_root.join("runtime").join("node_modules"),
                repo_root.join("node_modules"),
                dsh_home.join("node_modules"),
                dsh_home.join("profiles").join("node_modules"),
                dsh_home.join("profiles").join("web").join("node_modules"),
            ] {
                if let Ok(meta) = std::fs::symlink_metadata(&legacy_junc) {
                    use std::os::windows::fs::MetadataExt;
                    if (meta.file_attributes() & 0x00000400) != 0 {
                        let _ = std::process::Command::new("cmd")
                            .args(["/c", "rmdir", &legacy_junc.to_string_lossy()])
                            .creation_flags(0x08000000)
                            .output();
                    } else if meta.is_dir() {
                        let _ = std::fs::remove_dir_all(&legacy_junc);
                    }
                }
            }
        }

        // 4. Configure DSH & heal profile module fallbacks
        self.emit_log("system", "Engine ready! Configuring DSH agent & module fallbacks...");
        let configure_script = repo_root.join("scripts").join("configure-dsh.mjs");
        let mut cfg_cmd = Command::new(&node_bin);
        cfg_cmd.arg(&configure_script);
        cfg_cmd.current_dir(&repo_root);
        #[cfg(windows)]
        cfg_cmd.creation_flags(0x08000000);
        cfg_cmd.env("DSH_HOME", &dsh_home);
        cfg_cmd.env("NODE_PATH", repo_root.join("integrations").join("dsh").join("node_modules"));
        if bundled_node_dir.exists() {
            let current_path = std::env::var("PATH").unwrap_or_default();
            let new_path = format!("{};{}", bundled_node_dir.to_string_lossy(), current_path);
            cfg_cmd.env("PATH", new_path);
        }
        let cfg_out = cfg_cmd.output();
        if let Ok(out) = cfg_out {
            if !out.status.success() {
                self.emit_log("system:err", &String::from_utf8_lossy(&out.stderr));
            }
        }

        // 5. Start Cache Guard
        self.update_state(ServiceState::Starting {
            step: "正在启动 Cache Guard 上下文守卫...".to_string(),
        });
        let guard_script = repo_root.join("integrations").join("cache_guard.mjs");
        let mut guard_cmd = Command::new(&node_bin);
        guard_cmd.arg(&guard_script);
        guard_cmd.current_dir(repo_root.join("integrations").join("dsh"));
        #[cfg(windows)]
        guard_cmd.creation_flags(0x08000000);

        guard_cmd.env("GUARD_PORT", cfg.guard_port.to_string());
        guard_cmd.env("GUARD_UPSTREAM", format!("http://127.0.0.1:{}", cfg.engine_port));
        guard_cmd.env("GUARD_LOG", repo_root.join("runtime").join("logs").join("cache_guard.log"));
        guard_cmd.env("GUARD_TELEMETRY_LOG", repo_root.join("runtime").join("logs").join("telemetry.jsonl"));
        guard_cmd.env("GUARD_MAX_TOKENS", "210000");
        guard_cmd.env("GUARD_STRICT", "1");
        guard_cmd.env("DSH_PRUNE_AT_OCCURRENCE", "1");

        guard_cmd.stdout(Stdio::piped());
        guard_cmd.stderr(Stdio::piped());

        let mut guard_child = guard_cmd.spawn()
            .map_err(|e| format!("Failed to spawn cache_guard: {}", e))?;

        #[cfg(windows)]
        let _ = self.job.assign_raw_handle(guard_child.as_raw_handle());

        if let Some(stdout) = guard_child.stdout.take() {
            let this = Arc::clone(&self);
            tokio::spawn(async move {
                let reader = BufReader::new(stdout);
                for line in reader.lines().flatten() {
                    this.emit_log("guard", &line);
                }
            });
        }

        *self.guard_child.lock().unwrap() = Some(guard_child);

        // 6. Start DSH Web
        self.update_state(ServiceState::Starting {
            step: "正在启动 DSH 网页端智能体服务...".to_string(),
        });

        let dsh_entry = repo_root
            .join("integrations")
            .join("dsh")
            .join("node_modules")
            .join("@deepseek-ai")
            .join("dsh")
            .join("lib")
            .join("bin.js");

        let mut dsh_cmd = Command::new(&node_bin);
        dsh_cmd.args([
            dsh_entry.to_string_lossy().as_ref(),
            "web",
            "--no-open",
            "--port",
            &cfg.web_port.to_string(),
        ]);
        dsh_cmd.current_dir(repo_root.join("integrations").join("dsh"));
        #[cfg(windows)]
        dsh_cmd.creation_flags(0x08000000);

        dsh_cmd.env("DSH_HOME", &dsh_home);
        dsh_cmd.env("NODE_PATH", repo_root.join("integrations").join("dsh").join("node_modules"));
        dsh_cmd.env("TEMP", &temp_dir);
        dsh_cmd.env("TMP", &temp_dir);
        dsh_cmd.env("PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD", "1");
        dsh_cmd.env("PLAYWRIGHT_BROWSERS_PATH", repo_root.join("runtime").join("cache").join("playwright"));
        dsh_cmd.env("NINFER_LOCAL_KEY", "local");
        dsh_cmd.env("DSH_PRUNE_AT_OCCURRENCE", "1");

        if let Ok(path_var) = std::env::var("PATH") {
            dsh_cmd.env("PATH", format!("{};{}", bundled_node_dir.display(), path_var));
        }

        let log_dir = repo_root.join("runtime").join("logs");
        let _ = std::fs::create_dir_all(&log_dir);
        let dsh_stdout_path = log_dir.join("dsh.stdout.log");
        let dsh_stderr_path = log_dir.join("dsh.stderr.log");

        // Strictly match start.ps1: Redirect standard output/error to physical log files to bypass Windows Pipe buffer lock
        let dsh_stdout_file = std::fs::OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&dsh_stdout_path)
            .map_err(|e| format!("无法创建 dsh.stdout.log: {}", e))?;

        let dsh_stderr_file = std::fs::OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&dsh_stderr_path)
            .map_err(|e| format!("无法创建 dsh.stderr.log: {}", e))?;

        dsh_cmd.stdout(Stdio::from(dsh_stdout_file));
        dsh_cmd.stderr(Stdio::from(dsh_stderr_file));

        let mut dsh_child = dsh_cmd.spawn()
            .map_err(|e| format!("Failed to spawn DSH: {}", e))?;

        #[cfg(windows)]
        let _ = self.job.assign_raw_handle(dsh_child.as_raw_handle());

        // Background log tailer to stream file lines to UI
        let this_tailer = Arc::clone(&self);
        let tail_stdout_path = dsh_stdout_path.clone();
        tokio::spawn(async move {
            let mut last_len = 0usize;
            for _ in 0..1200 { // 10 minutes tailing
                tokio::time::sleep(Duration::from_millis(500)).await;
                if let Ok(content) = std::fs::read_to_string(&tail_stdout_path) {
                    if content.len() > last_len {
                        let new_chunk = &content[last_len..];
                        for line in new_chunk.lines() {
                            if !line.trim().is_empty() {
                                this_tailer.emit_log("dsh", line);
                            }
                        }
                        last_len = content.len();
                    }
                }
            }
        });

        // Strictly match start.ps1 Token poll loop (up to 120s)
        let expected_port = cfg.web_port;
        let mut final_web_url: Option<String> = None;
        let http_client = reqwest::Client::builder()
            .timeout(Duration::from_millis(1500))
            .build()
            .unwrap_or_default();

        let start_time = std::time::Instant::now();
        let max_wait = Duration::from_secs(120);

        while start_time.elapsed() < max_wait {
            // 1. Fast fail check if DSH child exited prematurely
            if let Ok(Some(status)) = dsh_child.try_wait() {
                let raw_stderr = std::fs::read_to_string(&dsh_stderr_path).unwrap_or_default();
                self.emit_log("system:err", &format!("DSH stderr trace: {}", raw_stderr.trim()));
                let friendly_msg = format_friendly_dsh_error(&raw_stderr, status);
                self.emit_log("system:err", &format!("DSH 启动状态: {}", friendly_msg));
                self.update_state(ServiceState::Error { message: friendly_msg.clone() });
                self.stop();
                return Err(friendly_msg);
            }

            // 2. Read dsh.stdout.log and search for "dsh web: (http://127.0.0.1:PORT/?token=...)"
            if let Ok(content) = std::fs::read_to_string(&dsh_stdout_path) {
                for line in content.lines() {
                    if line.contains("dsh web:") && line.contains(&format!("http://127.0.0.1:{}", expected_port)) {
                        if let Some(start) = line.find("http://") {
                            let raw_url = &line[start..];
                            let clean_url = raw_url.split_whitespace().next().unwrap_or(raw_url).trim();
                            // 3. Match start.ps1: Probe URL to ensure socket is ready
                            if let Ok(_resp) = http_client.get(clean_url).send().await {
                                final_web_url = Some(clean_url.to_string());
                                break;
                            }
                        }
                    }
                }
            }

            if final_web_url.is_some() {
                break;
            }

            tokio::time::sleep(Duration::from_millis(500)).await;
        }

        if final_web_url.is_none() {
            let msg = "DSH 网页端启动超时 (120s)，请检查 runtime/logs/dsh.stderr.log".to_string();
            self.emit_log("system:err", &msg);
            self.update_state(ServiceState::Error { message: msg.clone() });
            self.stop();
            return Err(msg);
        }

        let final_url = final_web_url.unwrap();
        *self.dsh_child.lock().unwrap() = Some(dsh_child);
        self.emit_log("system", &format!("DSH Web ready at: {}", final_url));
        self.update_state(ServiceState::Running { web_url: final_url });

        Ok(())
    }
}

fn get_node_path(repo_root: &std::path::Path) -> std::path::PathBuf {
    let bundled = repo_root.join("runtime").join("node").join("node.exe");
    if bundled.exists() {
        bundled
    } else {
        std::path::PathBuf::from("node")
    }
}

fn chrono_now() -> String {
    // Lightweight timestamp without heavy chrono dep
    let now = std::time::SystemTime::now();
    let duration = now.duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = duration.as_secs();
    let hours = (secs / 3600) % 24;
    let mins = (secs / 60) % 60;
    let s = secs % 60;
    format!("{:02}:{:02}:{:02}", (hours + 8) % 24, mins, s) // UTC+8
}

#[cfg(windows)]
fn cleanup_stale_ports(engine_port: u16, guard_port: u16, web_port: u16) {
    let script = format!(
        "$ports = @({}, {}, {});\
        foreach ($p in $ports) {{\
            $conns = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue;\
            if ($conns) {{\
                foreach ($c in $conns) {{\
                    $proc = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $c.OwningProcess);\
                    if ($proc -and ($proc.ExecutablePath -like '*node.exe*' -or $proc.ExecutablePath -like '*ninfer*')) {{\
                        Stop-Process -Id $c.OwningProcess -Force -ErrorAction SilentlyContinue;\
                    }}\
                }}\
            }}\
        }}",
        engine_port, guard_port, web_port
    );
    let _ = Command::new("powershell")
        .args(["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", &script])
        .creation_flags(0x08000000)
        .output();
}


fn format_friendly_dsh_error(stderr_content: &str, status: std::process::ExitStatus) -> String {
    if stderr_content.contains("ERR_MODULE_NOT_FOUND") || stderr_content.contains("Cannot find package") || stderr_content.contains("Cannot find module") {
        let pkg = if let Some(pos) = stderr_content.find("Cannot find package '") {
            let sub = &stderr_content[pos + "Cannot find package '".len()..];
            sub.split('\'').next().filter(|s| !s.is_empty()).unwrap_or("依赖包")
        } else if let Some(pos) = stderr_content.find("Cannot find module '") {
            let sub = &stderr_content[pos + "Cannot find module '".len()..];
            sub.split('\'').next().filter(|s| !s.is_empty()).unwrap_or("依赖包")
        } else {
            "核心依赖"
        };
        return format!("智能体依赖包解析异常 (缺失: {})，已触发自动自愈，请点击「重试启动」", pkg);
    }
    if stderr_content.contains("-4094") || stderr_content.contains("mkdir") || stderr_content.contains("EPERM") || stderr_content.contains("EACCES") {
        return "运行时环境目录发生冲突，已自动清理冲突链接，请点击「重试启动」".to_string();
    }
    if stderr_content.contains("exists and is not a symlink") {
        return "检测到旧版智能体符号链接冲突，已自动完成自愈，请点击「重试启动」".to_string();
    }
    if stderr_content.contains("EADDRINUSE") {
        return "智能体网络端口被其他程序占用，请关闭冲突程序后重试".to_string();
    }
    if stderr_content.contains("out of memory") || stderr_content.contains("CUDA") {
        return "独显显存不足，请在左侧「显存管理」中将后台应用迁移至核显以释放空间".to_string();
    }

    for line in stderr_content.lines() {
        let t = line.trim();
        if t.starts_with("Error:") {
            let msg = t.trim_start_matches("Error:").trim();
            if msg.len() > 80 {
                return format!("智能体服务异常: {}...", &msg[..77]);
            }
            return format!("智能体服务异常: {}", msg);
        }
    }

    if stderr_content.trim().is_empty() {
        format!("智能体进程异常退出 (退出码: {})，请查看日志获取详细信息", status)
    } else {
        let clean = stderr_content
            .replace("Node.js", "")
            .replace("errno:", "")
            .replace("code:", "")
            .replace("syscall:", "")
            .replace("UNKNOWN", "");
        let trimmed = clean.trim();
        if trimmed.len() > 80 {
            format!("智能体启动异常: {}...", &trimmed[..77])
        } else {
            format!("智能体启动异常: {}", trimmed)
        }
    }
}
