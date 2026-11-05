use serde::{Deserialize, Serialize};
use std::path::PathBuf;

fn deserialize_null_as_false<'de, D>(deserializer: D) -> Result<bool, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let opt = Option::<bool>::deserialize(deserializer)?;
    Ok(opt.unwrap_or(false))
}

fn default_gpu_arch() -> String {
    "sm120a".to_string()
}

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "PascalCase")]
pub struct AppConfig {
    pub engine_path: String,
    pub model_path: String,
    pub model_id: String,
    #[serde(default = "default_gpu_arch")]
    pub gpu_arch: String,
    pub engine_port: u16,
    pub guard_port: u16,
    pub web_port: u16,
    pub max_context: u64,
    pub device_k_v: u64,
    pub recent_tokens: u64,
    pub sink_tokens: u64,
    pub retrieve_tokens: u64,
    pub host_k_v_mi_b: u64,
    pub reasoning_effort: String,
    pub k_v_d_type: String,
    pub answer_penalty_scope: String,
    #[serde(default, deserialize_with = "deserialize_null_as_false")]
    pub vision_c_p_u: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_grounded_execution: Option<bool>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub edit_guard: Option<bool>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub task_ledger: Option<bool>,
    #[serde(flatten)]
    pub extra: std::collections::BTreeMap<String, serde_json::Value>,
}

pub fn get_repo_root() -> PathBuf {
    // 1. Check parent of current executable (when installed in Program Files or portable folder)
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            if dir.join("runtime").join("engine").exists() {
                return dir.to_path_buf();
            }
        }
    }

    // 2. Check current working directory or ancestors
    let curr = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    if curr.join("runtime").join("engine").exists() || curr.join("config").join("local.json").exists() {
        return curr;
    }
    if let Some(p) = curr.parent() {
        if p.join("runtime").join("engine").exists() || p.join("config").join("local.json").exists() {
            return p.to_path_buf();
        }
        if let Some(gp) = p.parent() {
            if gp.join("runtime").join("engine").exists() || gp.join("config").join("local.json").exists() {
                return gp.to_path_buf();
            }
        }
    }
    curr
}

pub fn load_config() -> Result<AppConfig, String> {
    let repo_root = get_repo_root();
    let config_path = repo_root.join("config").join("local.json");
    let example_path = repo_root.join("config").join("example.json");

    if !config_path.exists() {
        if example_path.exists() {
            let _ = std::fs::copy(&example_path, &config_path);
        } else {
            // Default initial configuration
            let default_cfg = AppConfig {
                engine_path: "runtime/engine/ninfer-serve.exe".to_string(),
                model_path: "".to_string(),
                model_id: "huihui-qwen3.8-27b-swift1.5-iq3-xxs".to_string(),
                gpu_arch: "sm120a".to_string(),
                engine_port: 1234,
                guard_port: 1236,
                web_port: 3080,
                max_context: 262144,
                device_k_v: 32768,
                recent_tokens: 18432,
                sink_tokens: 12288,
                retrieve_tokens: 1536,
                host_k_v_mi_b: 7168,
                reasoning_effort: "xhigh".to_string(),
                k_v_d_type: "rk4v4".to_string(),
                answer_penalty_scope: "answer".to_string(),
                vision_c_p_u: false,
                tool_grounded_execution: None,
                edit_guard: None,
                task_ledger: None,
                extra: std::collections::BTreeMap::new(),
            };
            save_config(&default_cfg)?;
            return Ok(default_cfg);
        }
    }

    let data = std::fs::read_to_string(&config_path)
        .map_err(|e| format!("Failed to read config: {}", e))?;
    let cfg: AppConfig = serde_json::from_str(&data)
        .map_err(|e| format!("Failed to parse config JSON: {}", e))?;
    Ok(cfg)
}

pub fn save_config(cfg: &AppConfig) -> Result<(), String> {
    let repo_root = get_repo_root();
    let config_dir = repo_root.join("config");
    let _ = std::fs::create_dir_all(&config_dir);
    let config_path = config_dir.join("local.json");
    let json_str = serde_json::to_string_pretty(cfg)
        .map_err(|e| format!("Failed to serialize config: {}", e))?;
    std::fs::write(&config_path, json_str)
        .map_err(|e| format!("Failed to write config: {}", e))?;
    Ok(())
}
