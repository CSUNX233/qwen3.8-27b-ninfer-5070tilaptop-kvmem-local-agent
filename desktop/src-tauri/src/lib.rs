mod config_manager;
mod job_object;
mod model_inspector;
mod supervisor;
mod telemetry;
mod vram_monitor;

use config_manager::{load_config, save_config, AppConfig};
use std::sync::Arc;
use supervisor::{ServiceState, Supervisor};
use tauri::{AppHandle, Emitter, Manager, State};
use vram_monitor::{query_gpu_stats, GpuStats};

struct AppState {
    supervisor: Arc<Supervisor>,
}

#[tauri::command]
fn get_service_state(state: State<'_, AppState>) -> ServiceState {
    state.supervisor.get_state()
}

#[tauri::command]
async fn start_service(state: State<'_, AppState>) -> Result<(), String> {
    Arc::clone(&state.supervisor).start().await
}

#[tauri::command]
fn stop_service(state: State<'_, AppState>) {
    state.supervisor.stop();
}

#[tauri::command]
fn get_gpu_stats() -> Option<GpuStats> {
    query_gpu_stats()
}

#[tauri::command]
async fn get_gpu_process_breakdown() -> Result<vram_monitor::VramBreakdown, String> {
    tokio::task::spawn_blocking(vram_monitor::query_vram_breakdown)
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn kill_process_by_pid(pid: u32, name: Option<String>) -> Result<(), String> {
    tokio::task::spawn_blocking(move || vram_monitor::terminate_process(pid, name))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn migrate_process_gpu(
    pid: u32,
    exe_path: Option<String>,
    target: String,
    restart: bool,
) -> Result<String, String> {
    tokio::task::spawn_blocking(move || {
        vram_monitor::migrate_process_gpu(pid, exe_path, &target, restart)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn set_process_gpu_preference(exe_path: String, target: String) -> Result<(), String> {
    tokio::task::spawn_blocking(move || {
        vram_monitor::set_process_gpu_preference(&exe_path, &target)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn batch_migrate_to_igpu(categories: Vec<String>) -> Result<Vec<String>, String> {
    tokio::task::spawn_blocking(move || {
        vram_monitor::batch_migrate_to_igpu(categories)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn batch_migrate_to_dgpu(categories: Vec<String>) -> Result<Vec<String>, String> {
    tokio::task::spawn_blocking(move || {
        vram_monitor::batch_migrate_to_dgpu(categories)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn get_app_config() -> Result<AppConfig, String> {
    load_config()
}

#[tauri::command]
fn update_app_config(config: AppConfig) -> Result<(), String> {
    save_config(&config)
}

#[tauri::command]
fn pick_model_file() -> Result<Option<String>, String> {
    let file = rfd::FileDialog::new()
        .set_title("选择 NInfer 本地大模型权重制品 (*.ninfer)")
        .add_filter("NInfer 大模型权重制品 (*.ninfer)", &["ninfer"])
        .pick_file();

    Ok(file.map(|p| p.to_string_lossy().to_string()))
}

#[tauri::command]
fn pick_engine_file() -> Result<Option<String>, String> {
    let file = rfd::FileDialog::new()
        .set_title("选择对应显卡架构的 NInfer 推理引擎 (*.exe)")
        .add_filter("NInfer 引擎可执行文件 (*.exe)", &["exe"])
        .pick_file();

    Ok(file.map(|p| p.to_string_lossy().to_string()))
}

#[tauri::command]
fn inspect_model_metadata(path: String) -> model_inspector::ModelMetadata {
    model_inspector::inspect_model_path(&path)
}

#[tauri::command]
fn get_telemetry_records() -> Vec<telemetry::TelemetryRecord> {
    telemetry::load_telemetry_records()
}

#[tauri::command]
fn save_telemetry_record(record: telemetry::TelemetryRecord) -> Result<(), String> {
    telemetry::append_telemetry_record(&record)
}

#[tauri::command]
fn clear_telemetry_records() -> Result<(), String> {
    telemetry::reset_telemetry_records()
}

#[tauri::command]
fn open_in_browser(app: AppHandle, url: String) -> Result<(), String> {
    use tauri_plugin_opener::OpenerExt;
    app.opener().open_url(&url, None::<&str>).map_err(|e| e.to_string())
}

fn append_log(msg: &str) {
    use std::io::Write;
    let repo_root = config_manager::get_repo_root();
    let log_dir = repo_root.join("runtime").join("logs");
    let _ = std::fs::create_dir_all(&log_dir);
    let log_file = log_dir.join("desktop_startup.log");
    if let Ok(mut f) = std::fs::OpenOptions::new().append(true).create(true).open(log_file) {
        let _ = writeln!(f, "[{}] {}", chrono_now(), msg);
    }
}

fn chrono_now() -> String {
    let now = std::time::SystemTime::now();
    let duration = now.duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    format!("{}.{:03}", duration.as_secs(), duration.subsec_millis())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    append_log("2. run() entered, initializing supervisor...");
    let mut supervisor = match Supervisor::new() {
        Ok(s) => {
            append_log("3. supervisor created successfully");
            s
        }
        Err(e) => {
            append_log(&format!("FATAL: Failed to initialize supervisor: {}", e));
            panic!("Failed to initialize supervisor: {}", e);
        }
    };

    let builder = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            append_log("4. setup callback started");
            let handle = app.handle().clone();
            supervisor.set_app_handle(handle.clone());
            let supervisor_arc = Arc::new(supervisor);

            app.manage(AppState {
                supervisor: Arc::clone(&supervisor_arc),
            });

            // Explicitly show, unminimize and focus the main window so it is guaranteed visible
            if let Some(main_win) = app.get_webview_window("main") {
                let _ = main_win.show();
                let _ = main_win.unminimize();
                let _ = main_win.set_focus();
                append_log("4.1 main window explicitly shown and focused");
            }

            // Start VRAM polling task
            let vram_handle = handle.clone();
            std::thread::spawn(move || {
                loop {
                    if let Some(stats) = query_gpu_stats() {
                        let _ = vram_handle.emit("gpu-stats", stats);
                    }
                    std::thread::sleep(std::time::Duration::from_secs(2));
                }
            });

            append_log("5. setup callback finished");
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_service_state,
            start_service,
            stop_service,
            get_gpu_stats,
            get_gpu_process_breakdown,
            kill_process_by_pid,
            migrate_process_gpu,
            set_process_gpu_preference,
            batch_migrate_to_igpu,
            batch_migrate_to_dgpu,
            get_app_config,
            update_app_config,
            open_in_browser,
            pick_model_file,
            pick_engine_file,
            inspect_model_metadata,
            get_telemetry_records,
            save_telemetry_record,
            clear_telemetry_records,
        ]);

    append_log("6. building tauri application context...");
    let app = match builder.build(tauri::generate_context!()) {
        Ok(a) => {
            append_log("7. tauri app built successfully");
            a
        }
        Err(e) => {
            append_log(&format!("FATAL: error while building tauri application: {}", e));
            #[cfg(windows)]
            unsafe {
                use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_ICONWARNING, MB_OK};
                let title: Vec<u16> = "本地大模型工作台 提示\0".encode_utf16().collect();
                let msg_str = format!("工作台初始化提示：\n{}\n\n若已有实例在后台运行，请通过任务栏或任务管理器切换。", e);
                let msg: Vec<u16> = msg_str.encode_utf16().collect();
                MessageBoxW(std::ptr::null_mut(), msg.as_ptr(), title.as_ptr(), MB_OK | MB_ICONWARNING);
            }
            std::process::exit(0);
        }
    };

    append_log("8. running tauri app loop...");
    app.run(move |app_handle, event| {
        append_log(&format!("run event: {:?}", event));
        if let tauri::RunEvent::Exit = event {
            if let Some(state) = app_handle.try_state::<AppState>() {
                state.supervisor.stop();
            }
        }
    });
}
