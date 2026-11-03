use crate::config_manager::get_repo_root;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs::{self, OpenOptions};
use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct TelemetryRecord {
    pub id: String,
    pub turn_number: usize,
    pub timestamp: u64,
    pub time_formatted: String,
    pub task_type: String,
    pub prompt_preview: String,
    pub ttft_ms: u64,
    pub thinking_duration_sec: f64,
    pub thinking_tokens: u64,
    pub output_duration_sec: f64,
    pub output_tokens: u64,
    pub generation_speed_tok_per_sec: f64,
    pub prompt_tokens: u64,
    pub total_tokens: u64,
    pub cache_hit_rate_pct: f64,
    pub status: String,
    #[serde(default)]
    pub steps: Option<u32>,
}

fn get_telemetry_path() -> PathBuf {
    let repo_root = get_repo_root();
    let dir = repo_root.join("runtime").join("logs");
    let _ = fs::create_dir_all(&dir);
    dir.join("telemetry.jsonl")
}

pub fn load_telemetry_records() -> Vec<TelemetryRecord> {
    let path = get_telemetry_path();
    let mut records: Vec<TelemetryRecord> = Vec::new();
    let mut known_ids: HashSet<String> = HashSet::new();
    let mut has_invalid_entries = false;

    // 1. Read existing records strictly from telemetry.jsonl (the verified source of truth)
    if path.exists() {
        if let Ok(file) = fs::File::open(&path) {
            let reader = BufReader::new(file);
            for line in reader.lines().flatten() {
                let trimmed = line.trim();
                if trimmed.is_empty() {
                    continue;
                }
                if let Ok(record) = serde_json::from_str::<TelemetryRecord>(trimmed) {
                    // Filter out zero-token placeholder/blank records
                    if record.output_tokens == 0 && record.total_tokens == 0 {
                        has_invalid_entries = true;
                        continue;
                    }
                    if !known_ids.contains(&record.id) {
                        known_ids.insert(record.id.clone());
                        records.push(record);
                    }
                }
            }
        }
    }

    // Sort chronologically ascending to re-index turns correctly
    records.sort_by_key(|r| r.timestamp);
    for (idx, r) in records.iter_mut().enumerate() {
        r.turn_number = idx + 1;
    }

    // Rewrite telemetry.jsonl if invalid entries were purged
    if has_invalid_entries {
        let _ = write_all_telemetry_records(&records);
    }

    // Newest first for UI presentation
    records.reverse();
    records
}

fn write_all_telemetry_records(records: &[TelemetryRecord]) -> Result<(), String> {
    let path = get_telemetry_path();
    let mut file = OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(&path)
        .map_err(|e| e.to_string())?;

    for r in records {
        if let Ok(json_line) = serde_json::to_string(r) {
            let _ = writeln!(file, "{}", json_line);
        }
    }
    Ok(())
}

pub fn append_telemetry_record(record: &TelemetryRecord) -> Result<(), String> {
    if record.output_tokens == 0 && record.total_tokens == 0 {
        return Ok(()); // Never persist empty records
    }
    let path = get_telemetry_path();
    let mut records = Vec::new();
    let mut found = false;
    if path.exists() {
        if let Ok(file) = fs::File::open(&path) {
            let reader = BufReader::new(file);
            for line in reader.lines().flatten() {
                let trimmed = line.trim();
                if trimmed.is_empty() {
                    continue;
                }
                if let Ok(r) = serde_json::from_str::<TelemetryRecord>(trimmed) {
                    if r.id == record.id {
                        records.push(record.clone());
                        found = true;
                    } else {
                        records.push(r);
                    }
                }
            }
        }
    }
    if !found {
        records.push(record.clone());
    }

    // Sort chronologically ascending
    records.sort_by_key(|r| r.timestamp);
    for (idx, r) in records.iter_mut().enumerate() {
        r.turn_number = idx + 1;
    }
    write_all_telemetry_records(&records)
}

pub fn reset_telemetry_records() -> Result<(), String> {
    let path = get_telemetry_path();
    if path.exists() {
        let _ = fs::remove_file(&path);
    }
    Ok(())
}

#[allow(dead_code)]
fn import_turns_from_dsh_sessions(existing_ids: &HashSet<String>) -> Vec<TelemetryRecord> {
    let repo_root = get_repo_root();
    let sessions_dir = repo_root
        .join("runtime")
        .join("dsh-home")
        .join("storages")
        .join("session_projcache")
        .join("sessions");

    if !sessions_dir.exists() {
        return Vec::new();
    }

    let mut imported = Vec::new();
    if let Ok(entries) = fs::read_dir(sessions_dir) {
        for entry in entries.flatten() {
            let p = entry.path();
            if p.extension().map_or(false, |ext| ext == "json") {
                let file_stem = p.file_stem().and_then(|s| s.to_str()).unwrap_or("session");
                if let Ok(content) = fs::read_to_string(&p) {
                    if let Ok(val) = serde_json::from_str::<serde_json::Value>(&content) {
                        let record_val = match val.get("record") {
                            Some(r) => r,
                            None => continue,
                        };
                        let rows = match record_val.get("rows") {
                            Some(r) => r,
                            None => continue,
                        };

                        let identity = record_val.get("identity");
                        let created_at = identity
                            .and_then(|i| i.get("createdAt"))
                            .and_then(|c| c.as_u64())
                            .unwrap_or(0);

                        let turns = match rows
                            .get("turnOutline")
                            .and_then(|to| to.get("val"))
                            .and_then(|v| v.get("turns"))
                            .and_then(|t| t.as_array())
                        {
                            Some(t) => t,
                            None => continue,
                        };

                        if turns.is_empty() {
                            continue; // Skip blank sessions
                        }

                        let stats = rows
                            .get("sessionStats")
                            .and_then(|s| s.get("val"));
                        let usage = rows
                            .get("tokenUsage")
                            .and_then(|u| u.get("val"))
                            .and_then(|v| v.get("totals"));

                        let decode_tokens = stats
                            .and_then(|s| s.get("decodeTokens"))
                            .and_then(|d| d.as_u64())
                            .unwrap_or(0);

                        let totals_output = usage
                            .and_then(|u| u.get("outputTokens"))
                            .and_then(|o| o.as_u64())
                            .unwrap_or(0);

                        let actual_output = decode_tokens.max(totals_output);
                        if actual_output == 0 {
                            continue; // No output generated
                        }

                        let decode_ms = stats
                            .and_then(|s| s.get("decodeMs"))
                            .and_then(|d| d.as_u64())
                            .unwrap_or(1000);
                        let llm_ms = stats
                            .and_then(|s| s.get("llmMs"))
                            .and_then(|d| d.as_u64())
                            .unwrap_or(decode_ms);
                        let ttft_steps = stats
                            .and_then(|s| s.get("ttftSteps"))
                            .and_then(|s| s.as_u64())
                            .unwrap_or(1);
                        let ttft_ms = stats
                            .and_then(|s| s.get("ttftMs"))
                            .and_then(|s| s.as_u64())
                            .unwrap_or(350);

                        let avg_ttft = if ttft_steps > 0 {
                            ttft_ms / ttft_steps
                        } else {
                            350
                        };

                        let uncached_input = usage
                            .and_then(|u| u.get("uncachedInputTokens"))
                            .and_then(|i| i.as_u64())
                            .unwrap_or(1000);
                        let cache_read = usage
                            .and_then(|u| u.get("cacheReadTokens"))
                            .and_then(|c| c.as_u64())
                            .unwrap_or(0);

                        let total_input = uncached_input + cache_read;
                        let hit_rate = if total_input > 0 {
                            ((cache_read as f64 / total_input as f64) * 1000.0).round() / 10.0
                        } else {
                            0.0
                        };

                        let num_turns = turns.len().max(1);
                        let total_response_len: usize = turns
                            .iter()
                            .map(|t| {
                                t.get("response")
                                    .and_then(|r| r.as_str())
                                    .map(|s| s.len())
                                    .unwrap_or(1)
                            })
                            .sum();

                        for (idx, turn_val) in turns.iter().enumerate() {
                            let turn_num = turn_val
                                .get("turn")
                                .and_then(|t| t.as_u64())
                                .unwrap_or((idx + 1) as u64);

                            let prompt = turn_val
                                .get("prompt")
                                .and_then(|p| p.as_str())
                                .unwrap_or("")
                                .trim();

                            if prompt.is_empty() {
                                continue;
                            }

                            let turn_id = format!("{}_t{}", file_stem, turn_num);
                            if existing_ids.contains(&turn_id) {
                                continue; // Already in store
                            }

                            let response = turn_val
                                .get("response")
                                .and_then(|r| r.as_str())
                                .unwrap_or("");

                            let weight = if total_response_len > 0 {
                                (response.len() as f64 / total_response_len as f64).max(0.05)
                            } else {
                                1.0 / num_turns as f64
                            };

                            let turn_output_tokens = ((actual_output as f64 * weight).round() as u64).max(1);
                            let turn_output_sec = ((decode_ms as f64 * weight / 1000.0) * 10.0).round() / 10.0;
                            let turn_output_sec = turn_output_sec.max(0.1);
                            let speed = ((turn_output_tokens as f64 / turn_output_sec) * 10.0).round() / 10.0;

                            let thinking_sec = ((llm_ms.saturating_sub(decode_ms) as f64 / 1000.0 / num_turns as f64) * 10.0).round() / 10.0;
                            let thinking_tokens = (thinking_sec * 25.0).round() as u64;

                            let turn_input_tokens = (uncached_input / num_turns as u64).max(10);

                            let mut task_type = "常规推理".to_string();
                            let prompt_lower = prompt.to_lowercase();
                            if prompt.contains("html")
                                || prompt.contains("代码")
                                || prompt.contains("编写")
                                || prompt.contains("实现")
                                || prompt.contains("demo")
                                || prompt.contains("组件")
                                || prompt_lower.contains("function")
                                || prompt_lower.contains("class")
                            {
                                task_type = "代码生成".to_string();
                            } else if prompt.contains("分析")
                                || prompt.contains("排查")
                                || prompt.contains("架构")
                                || prompt.contains("优化")
                                || prompt.contains("性能")
                            {
                                task_type = "架构分析".to_string();
                            } else if prompt.contains("测试") || prompt_lower.contains("test") {
                                task_type = "单元测试".to_string();
                            } else if prompt.contains("命令") || prompt.contains("终端") || prompt_lower.contains("git") {
                                task_type = "终端任务".to_string();
                            }

                            let turn_timestamp = created_at + (idx as u64 * 60_000);
                            let time_formatted = format_time_naive(turn_timestamp);

                            imported.push(TelemetryRecord {
                                id: turn_id,
                                turn_number: 0,
                                timestamp: turn_timestamp,
                                time_formatted,
                                task_type,
                                prompt_preview: prompt
                                    .replace('\r', " ")
                                    .replace('\n', " ")
                                    .chars()
                                    .take(120)
                                    .collect(),
                                ttft_ms: avg_ttft,
                                thinking_duration_sec: thinking_sec,
                                thinking_tokens,
                                output_duration_sec: turn_output_sec,
                                output_tokens: turn_output_tokens,
                                generation_speed_tok_per_sec: speed,
                                prompt_tokens: turn_input_tokens,
                                total_tokens: turn_input_tokens + turn_output_tokens,
                                cache_hit_rate_pct: hit_rate,
                                status: "success".to_string(),
                                steps: None,
                            });
                        }
                    }
                }
            }
        }
    }

    imported
}

fn format_time_naive(ts_ms: u64) -> String {
    let secs = ts_ms / 1000;
    let secs_cst = secs + 8 * 3600;
    let days_since_epoch = secs_cst / 86400;
    let rem_secs = secs_cst % 86400;
    let h = rem_secs / 3600;
    let m = (rem_secs % 3600) / 60;

    let mut year = 1970;
    let mut d = days_since_epoch;
    loop {
        let leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        let days_in_year = if leap { 366 } else { 365 };
        if d >= days_in_year {
            d -= days_in_year;
            year += 1;
        } else {
            break;
        }
    }
    let leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    let days_in_months = [31, if leap { 29 } else { 28 }, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut month = 1;
    for &dim in &days_in_months {
        if d >= dim {
            d -= dim;
            month += 1;
        } else {
            break;
        }
    }
    let day = d + 1;
    format!("{}/{} {:02}:{:02}", month, day, h, m)
}
