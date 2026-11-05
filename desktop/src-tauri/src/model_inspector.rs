use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModelMetadata {
    pub is_valid: bool,
    pub filename: String,
    pub format: String,
    pub is_engine_compatible: bool,
    pub compatibility_notice: String,
    pub architecture: String,
    pub param_size: String,
    pub quant_type: String,
    pub context_window: u64,
    pub layer_count: u32,
    pub hidden_size: u32,
    pub model_id: String,
    pub source_origin: String, // "binary_header" | "filename_fallback"
}

impl Default for ModelMetadata {
    fn default() -> Self {
        Self {
            is_valid: false,
            filename: String::new(),
            format: "UNKNOWN".to_string(),
            is_engine_compatible: false,
            compatibility_notice: String::new(),
            architecture: "Generic".to_string(),
            param_size: "--".to_string(),
            quant_type: "--".to_string(),
            context_window: 0,
            layer_count: 0,
            hidden_size: 0,
            model_id: String::new(),
            source_origin: "none".to_string(),
        }
    }
}

pub fn inspect_model_path(path_str: &str) -> ModelMetadata {
    let clean_path = path_str.trim();
    if clean_path.is_empty() {
        return ModelMetadata::default();
    }

    let p = Path::new(clean_path);
    let filename = p
        .file_name()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_else(|| clean_path.to_string());

    let lower_filename = filename.to_lowercase();

    if !p.exists() {
        let fmt = if lower_filename.ends_with(".ninfer") {
            "NINFER"
        } else if lower_filename.ends_with(".gguf") {
            "GGUF"
        } else if lower_filename.ends_with(".safetensors") {
            "SAFETENSORS"
        } else {
            "FILE_NOT_FOUND"
        };
        let mut meta = ModelMetadata {
            is_valid: false,
            filename: filename.clone(),
            format: fmt.to_string(),
            is_engine_compatible: false,
            compatibility_notice: format!("模型文件在本地磁盘不存在，请核对文件路径：{}", clean_path),
            source_origin: "fs_check".to_string(),
            ..Default::default()
        };
        fallback_from_filename(&filename, &mut meta);
        meta.is_valid = false;
        meta.is_engine_compatible = false;
        meta.format = fmt.to_string();
        meta.compatibility_notice = format!("模型文件在本地磁盘不存在，请核对文件路径：{}", clean_path);
        meta.source_origin = "fs_check".to_string();
        return meta;
    }

    // 1. Try reading real binary header
    if let Ok(mut file) = File::open(p) {
        let mut magic_buf = [0u8; 16];
        if file.read_exact(&mut magic_buf).is_ok() {
            // Check NINFER format
            if &magic_buf[0..6] == b"NINFER" {
                if let Some(parsed) = parse_ninfer_header(&mut file, &magic_buf, &filename) {
                    return parsed;
                }
            }

            // Check GGUF format
            if &magic_buf[0..4] == b"GGUF" {
                if let Some(parsed) = parse_gguf_header(&mut file, &magic_buf, &filename) {
                    return parsed;
                }
            }

            // Check SafeTensors format (starts with u64 header len, then JSON)
            let header_len = u64::from_le_bytes([
                magic_buf[0], magic_buf[1], magic_buf[2], magic_buf[3],
                magic_buf[4], magic_buf[5], magic_buf[6], magic_buf[7],
            ]);
            if header_len > 0 && header_len < 100 * 1024 * 1024 && (lower_filename.ends_with(".safetensors") || &magic_buf[8..10] == b"{\"" || &magic_buf[8..10] == b"{\n") {
                let mut meta = ModelMetadata {
                    is_valid: true,
                    filename: filename.clone(),
                    format: "SAFETENSORS".to_string(),
                    is_engine_compatible: false,
                    compatibility_notice: "检测到该文件为 SafeTensors 原始未量化权重，当前 NInfer 引擎无法直接加载，需转换为 .ninfer 格式。".to_string(),
                    source_origin: "binary_header".to_string(),
                    ..Default::default()
                };
                fallback_from_filename(&filename, &mut meta);
                meta.format = "SAFETENSORS".to_string();
                meta.is_engine_compatible = false;
                meta.source_origin = "binary_header".to_string();
                return meta;
            }
        }
    }

    // 2. If file ended with .ninfer but binary magic didn't match
    if lower_filename.ends_with(".ninfer") {
        return ModelMetadata {
            is_valid: false,
            filename: filename.clone(),
            format: "INVALID_NINFER".to_string(),
            is_engine_compatible: false,
            compatibility_notice: "文件头校验失败：该文件扩展名为 .ninfer，但缺少标准 NINFER 二进制签名（文件可能损坏、为空或被误改名）。".to_string(),
            source_origin: "corrupt_or_mismatched".to_string(),
            ..Default::default()
        };
    }

    // 3. Fallback to filename heuristic if binary parsing wasn't applicable
    let mut meta = ModelMetadata {
        is_valid: true,
        filename: filename.clone(),
        ..Default::default()
    };
    fallback_from_filename(&filename, &mut meta);
    meta
}

fn parse_ninfer_header(file: &mut File, magic_buf: &[u8; 16], filename: &str) -> Option<ModelMetadata> {
    let json_len = u64::from_le_bytes([
        magic_buf[8], magic_buf[9], magic_buf[10], magic_buf[11],
        magic_buf[12], magic_buf[13], magic_buf[14], magic_buf[15],
    ]);

    // Safety limit: up to 10MB JSON header
    if json_len == 0 || json_len > 10 * 1024 * 1024 {
        return None;
    }

    if file.seek(SeekFrom::Start(32)).is_err() {
        return None;
    }

    let mut json_buf = vec![0u8; json_len as usize];
    if file.read_exact(&mut json_buf).is_err() {
        return None;
    }

    let Ok(v) = serde_json::from_slice::<serde_json::Value>(&json_buf) else {
        return None;
    };

    let text_cfg = v.get("components").and_then(|c| c.get("text")).and_then(|t| t.get("config"));
    let metadata_obj = v.get("metadata");
    let provenance_obj = v.get("provenance");

    let raw_arch = text_cfg
        .and_then(|c| c.get("architectures"))
        .and_then(|a| a.as_array())
        .and_then(|arr| arr.get(0))
        .and_then(|s| s.as_str())
        .or_else(|| text_cfg.and_then(|c| c.get("model_type")).and_then(|s| s.as_str()))
        .unwrap_or("Qwen");

    let architecture = normalize_arch_name(raw_arch);

    let layer_count = text_cfg
        .and_then(|c| c.get("num_hidden_layers"))
        .and_then(|n| n.as_u64())
        .unwrap_or(0) as u32;

    let hidden_size = text_cfg
        .and_then(|c| c.get("hidden_size"))
        .and_then(|n| n.as_u64())
        .unwrap_or(0) as u32;

    let context_window = text_cfg
        .and_then(|c| c.get("max_position_embeddings"))
        .and_then(|n| n.as_u64())
        .unwrap_or(262144);

    let model_id = metadata_obj
        .and_then(|m| m.get("name"))
        .and_then(|s| s.as_str())
        .map(|s| s.to_string())
        .unwrap_or_else(|| {
            filename.trim_end_matches(".ninfer").to_string()
        });

    // Derive param size from layers and hidden size, or search metadata
    let param_size = derive_param_size(layer_count, hidden_size, &model_id, provenance_obj);

    // Derive quantization from name / provenance / filename
    let quant_type = extract_quantization_string(&model_id, provenance_obj, filename);

    Some(ModelMetadata {
        is_valid: true,
        filename: filename.to_string(),
        format: "NINFER".to_string(),
        is_engine_compatible: true,
        compatibility_notice: "与当前 NInfer C++ 高性能推理引擎完全兼容，已通过底层签名校验。".to_string(),
        architecture,
        param_size,
        quant_type,
        context_window,
        layer_count,
        hidden_size,
        model_id,
        source_origin: "binary_header".to_string(),
    })
}

fn parse_gguf_header(file: &mut File, _magic_buf: &[u8; 16], filename: &str) -> Option<ModelMetadata> {
    // Read the rest of GGUF header (up to 24 bytes)
    let mut header_ext = [0u8; 8];
    if file.read_exact(&mut header_ext).is_err() {
        return None;
    }

    let kv_count = u64::from_le_bytes(header_ext);
    if kv_count == 0 {
        return None;
    }

    // Read first 64KB for key-value dictionary scan
    let mut buf = vec![0u8; 65536];
    let n = file.read(&mut buf).unwrap_or(0);
    if n < 32 {
        return None;
    }

    let mut offset = 0;
    let mut architecture = String::from("Generic");
    let mut model_name = String::new();
    let mut context_len: u64 = 0;

    let scan_limit = (kv_count as usize).min(60);
    for _ in 0..scan_limit {
        if offset + 8 > n {
            break;
        }
        let key_len = u64::from_le_bytes(buf[offset..offset + 8].try_into().unwrap()) as usize;
        offset += 8;
        if offset + key_len + 4 > n {
            break;
        }
        let key = String::from_utf8_lossy(&buf[offset..offset + key_len]).to_string();
        offset += key_len;
        let val_type = u32::from_le_bytes(buf[offset..offset + 4].try_into().unwrap());
        offset += 4;

        if val_type == 8 {
            // String value
            if offset + 8 > n {
                break;
            }
            let val_len = u64::from_le_bytes(buf[offset..offset + 8].try_into().unwrap()) as usize;
            offset += 8;
            if offset + val_len > n {
                break;
            }
            let val = String::from_utf8_lossy(&buf[offset..offset + val_len]).to_string();
            offset += val_len;

            if key == "general.architecture" {
                architecture = normalize_arch_name(&val);
            } else if key == "general.name" {
                model_name = val;
            }
        } else if val_type == 4 {
            // UInt32
            if offset + 4 > n {
                break;
            }
            let val = u32::from_le_bytes(buf[offset..offset + 4].try_into().unwrap());
            offset += 4;
            if key.ends_with(".context_length") {
                context_len = val as u64;
            }
        } else if val_type == 10 {
            // UInt64
            if offset + 8 > n {
                break;
            }
            let val = u64::from_le_bytes(buf[offset..offset + 8].try_into().unwrap());
            offset += 8;
            if key.ends_with(".context_length") {
                context_len = val;
            }
        } else {
            // Skip other types gracefully
            break;
        }
    }

    let param_size = if !model_name.is_empty() {
        extract_param_size_from_str(&model_name)
    } else {
        extract_param_size_from_str(filename)
    };

    let quant_type = extract_quantization_string(&model_name, None, filename);

    Some(ModelMetadata {
        is_valid: true,
        filename: filename.to_string(),
        format: "GGUF".to_string(),
        is_engine_compatible: false,
        compatibility_notice: "当前引擎为 NInfer 原生推理器，仅支持运行 .ninfer 格式；检测到该文件为 GGUF (llama.cpp) 格式，无法直接启动。请选用已转换的 .ninfer 权重。".to_string(),
        architecture,
        param_size,
        quant_type,
        context_window: if context_len > 0 { context_len } else { 131072 },
        layer_count: 0,
        hidden_size: 0,
        model_id: if !model_name.is_empty() { model_name } else { filename.trim_end_matches(".gguf").to_string() },
        source_origin: "binary_header".to_string(),
    })
}

fn normalize_arch_name(raw: &str) -> String {
    let lower = raw.to_lowercase();
    if lower.contains("qwen") {
        "Qwen".to_string()
    } else if lower.contains("deepseek") {
        "DeepSeek".to_string()
    } else if lower.contains("llama") {
        "LLaMA".to_string()
    } else if lower.contains("mistral") || lower.contains("mixtral") {
        "Mistral".to_string()
    } else if lower.contains("gemma") {
        "Gemma".to_string()
    } else if lower.contains("phi") {
        "Phi".to_string()
    } else if lower.contains("glm") {
        "GLM".to_string()
    } else {
        "Generic".to_string()
    }
}

fn derive_param_size(
    layers: u32,
    hidden: u32,
    model_id: &str,
    provenance: Option<&serde_json::Value>,
) -> String {
    // 1. Precise architectural layer/hidden size fingerprint
    if layers == 64 && hidden == 5120 {
        return "27B".to_string();
    }
    if layers == 28 && hidden == 3584 {
        return "7B".to_string();
    }
    if layers == 48 && hidden == 5120 {
        return "14B".to_string();
    }
    if layers == 32 && hidden == 4096 {
        return "8B".to_string();
    }
    if layers == 80 && hidden == 8192 {
        return "70B".to_string();
    }
    if layers == 24 && hidden == 2048 {
        return "3B".to_string();
    }

    // 2. Check metadata string or provenance
    let from_id = extract_param_size_from_str(model_id);
    if from_id != "--" && from_id != "Adaptive" {
        return from_id;
    }

    if let Some(prov) = provenance {
        let prov_str = prov.to_string();
        let from_prov = extract_param_size_from_str(&prov_str);
        if from_prov != "--" && from_prov != "Adaptive" {
            return from_prov;
        }
    }

    "Adaptive".to_string()
}

fn extract_param_size_from_str(s: &str) -> String {
    // Match e.g. 27B, 27b, 1.5B, 7B, 70B
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i].is_ascii_digit() {
            let start = i;
            while i < bytes.len() && (bytes[i].is_ascii_digit() || bytes[i] == b'.') {
                i += 1;
            }
            if i < bytes.len() && (bytes[i] == b'b' || bytes[i] == b'B') {
                // Ensure boundary
                let next_is_char = i + 1 < bytes.len() && bytes[i + 1].is_ascii_alphabetic();
                if !next_is_char {
                    let num = &s[start..=i];
                    return num.to_uppercase();
                }
            }
        } else {
            i += 1;
        }
    }
    "--".to_string()
}

fn extract_quantization_string(
    model_id: &str,
    provenance: Option<&serde_json::Value>,
    filename: &str,
) -> String {
    let prov_str = provenance.map(|p| p.to_string()).unwrap_or_default();
    let candidates = [
        model_id,
        prov_str.as_str(),
        filename,
    ];

    for text in &candidates {
        let upper = text.to_uppercase();
        for pattern in &[
            "IQ1_S", "IQ1_M", "IQ2_XXS", "IQ2_XS", "IQ2_S", "IQ2_M",
            "IQ3_XXS", "IQ3_XS", "IQ3_S", "IQ3_M",
            "IQ4_XS", "IQ4_NL",
            "Q2_K", "Q3_K_S", "Q3_K_M", "Q3_K_L",
            "Q4_0", "Q4_1", "Q4_K_S", "Q4_K_M",
            "Q5_0", "Q5_1", "Q5_K_S", "Q5_K_M",
            "Q6_K", "Q8_0",
            "NVFP4", "RK4V4", "FP16", "FP8", "BF16", "INT8", "INT4",
        ] {
            if upper.contains(pattern) {
                return pattern.to_string();
            }
        }
    }

    "RK4V4".to_string()
}

fn fallback_from_filename(filename: &str, meta: &mut ModelMetadata) {
    let lower = filename.to_lowercase();
    meta.architecture = normalize_arch_name(&lower);
    meta.param_size = extract_param_size_from_str(filename);
    meta.quant_type = extract_quantization_string("", None, filename);
    meta.context_window = if lower.contains("256k") {
        262144
    } else if lower.contains("128k") {
        131072
    } else if lower.contains("64k") {
        65536
    } else {
        32768
    };

    if lower.ends_with(".ninfer") {
        meta.format = "NINFER".to_string();
        meta.is_engine_compatible = true;
        meta.compatibility_notice = "推测为 .ninfer 模型权重制品。".to_string();
    } else if lower.ends_with(".gguf") {
        meta.format = "GGUF".to_string();
        meta.is_engine_compatible = false;
        meta.compatibility_notice = "当前引擎为 NInfer 原生推理器，仅支持运行 .ninfer 格式；检测到该文件为 GGUF 格式，无法直接启动。".to_string();
    } else if lower.ends_with(".safetensors") {
        meta.format = "SAFETENSORS".to_string();
        meta.is_engine_compatible = false;
        meta.compatibility_notice = "检测到该文件为 SafeTensors 原始未量化权重，当前 NInfer 引擎无法直接加载，需转换为 .ninfer 格式。".to_string();
    } else {
        meta.format = "CUSTOM".to_string();
        meta.is_engine_compatible = false;
        meta.compatibility_notice = format!("未识别的文件类型（{}）。当前 NInfer 引擎仅接受 .ninfer 格式大模型制品。", filename);
    }

    meta.model_id = filename
        .trim_end_matches(".ninfer")
        .trim_end_matches(".gguf")
        .trim_end_matches(".safetensors")
        .trim_end_matches(".bin")
        .to_string();

    meta.source_origin = "filename_fallback".to_string();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_inspect_real_ninfer() {
        let path = "F:\\Pmod\\Huihui-Qwen3.8-27B-abliterated-GSQ-RCO-IQ3_XXS-mtp\\Huihui-Qwen3.8-27B-abliterated-Swift-1.5-GSQ-RCO-IQ3_XXS-mtp.ninfer";
        let meta = inspect_model_path(path);
        println!("Inspected meta: {:#?}", meta);
        assert!(meta.is_valid);
        assert!(meta.is_engine_compatible);
        assert_eq!(meta.format, "NINFER");
        assert_eq!(meta.architecture, "Qwen");
        assert_eq!(meta.param_size, "27B");
        assert_eq!(meta.layer_count, 64);
        assert_eq!(meta.hidden_size, 5120);
        assert_eq!(meta.context_window, 262144);
        assert_eq!(meta.source_origin, "binary_header");
    }

    #[test]
    fn test_inspect_incompatible_gguf() {
        let temp_dir = std::env::temp_dir();
        let gguf_path = temp_dir.join("test_dummy_model.gguf");
        let mut f = File::create(&gguf_path).unwrap();
        let mut header = [0u8; 32];
        header[0..4].copy_from_slice(b"GGUF");
        header[4..8].copy_from_slice(&3u32.to_le_bytes());
        header[8..16].copy_from_slice(&0u64.to_le_bytes());
        header[16..24].copy_from_slice(&0u64.to_le_bytes());
        std::io::Write::write_all(&mut f, &header).unwrap();
        drop(f);

        let meta = inspect_model_path(&gguf_path.to_string_lossy());
        let _ = std::fs::remove_file(gguf_path);

        assert!(!meta.is_engine_compatible);
        assert_eq!(meta.format, "GGUF");
        assert!(meta.compatibility_notice.contains("仅支持"));
    }
}
