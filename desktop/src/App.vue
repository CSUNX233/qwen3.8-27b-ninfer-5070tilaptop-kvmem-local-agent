<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, nextTick, watch } from "vue";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import andsiImg from "./assets/img/andsi.jpg";
import gpuImg from "./assets/img/gpu.jpg";
import cpuImg from "./assets/img/cpu.jpg";

type TabType = "overview" | "chat" | "vram" | "model" | "logs" | "settings";
type SettingsTab = "general" | "about";
type ThemeType = "system" | "light" | "dark";
type ColorSchemeType = "default" | "crimson" | "emerald" | "ocean";

interface GpuStats {
  total_mb: number;
  used_mb: number;
  free_mb: number;
  gpu_util: number;
  temp_c: number;
  gpu_name?: string;
  compute_cap?: string;
  driver_version?: string;
}

interface GpuTopology {
  is_hybrid_supported: boolean;
  dgpu_name: string;
  dgpu_driver: string;
  igpu_name: string;
  igpu_driver: string;
}

interface ProcessVramInfo {
  pid: number;
  name: string;
  exe_path?: string;
  vram_mb: number;
  vram_bytes: number;
  category: string;
  is_critical_system: boolean;
  gpu_target?: string;
  preference?: number;
}

interface VramBreakdown {
  total_mb: number;
  used_mb: number;
  free_mb: number;
  topology?: GpuTopology;
  dgpu_processes?: ProcessVramInfo[];
  igpu_processes?: ProcessVramInfo[];
  processes: ProcessVramInfo[];
  timestamp: number;
}

interface AppConfig {
  EnginePath: string;
  ModelPath: string;
  ModelId: string;
  GpuArch?: string;
  EnginePort: number;
  GuardPort: number;
  WebPort: number;
  MaxContext: number;
  DeviceKV: number;
  RecentTokens: number;
  SinkTokens: number;
  RetrieveTokens: number;
  HostKVMiB: number;
  ReasoningEffort: string;
  KVDType: string;
  AnswerPenaltyScope: string;
  VisionCPU?: boolean;
}

interface ModelMetadata {
  is_valid: boolean;
  filename: string;
  format: string;
  is_engine_compatible: boolean;
  compatibility_notice: string;
  architecture: string;
  param_size: string;
  quant_type: string;
  context_window: number;
  layer_count: number;
  hidden_size: number;
  model_id: string;
  source_origin: string;
}

interface ServiceState {
  status: "Stopped" | "Starting" | "Running" | "Error";
  data?: any;
}

interface LogEntry {
  source: string;
  message: string;
  timestamp: string;
}

// Prestige Glass Modal System Interfaces & State
interface ModalOptions {
  title?: string;
  message: string;
  detail?: string;
  type?: "info" | "success" | "warning" | "danger";
  confirmText?: string;
  cancelText?: string;
}

interface ModalState {
  isOpen: boolean;
  title: string;
  message: string;
  detail: string;
  type: "info" | "success" | "warning" | "danger";
  showCancel: boolean;
  confirmText: string;
  cancelText: string;
  resolve: ((val: boolean) => void) | null;
}

const modalState = ref<ModalState>({
  isOpen: false,
  title: "",
  message: "",
  detail: "",
  type: "info",
  showCancel: false,
  confirmText: "",
  cancelText: "",
  resolve: null,
});

function showConfirm(options: ModalOptions | string): Promise<boolean> {
  return new Promise((resolve) => {
    const opts: ModalOptions = typeof options === "string" ? { message: options } : options;
    modalState.value = {
      isOpen: true,
      title: opts.title || t("确认操作", "Confirm Action"),
      message: opts.message,
      detail: opts.detail || "",
      type: opts.type || "warning",
      showCancel: true,
      confirmText: opts.confirmText || t("确定", "Confirm"),
      cancelText: opts.cancelText || t("取消", "Cancel"),
      resolve,
    };
  });
}

function showAlert(options: ModalOptions | string): Promise<void> {
  return new Promise((resolve) => {
    const opts: ModalOptions = typeof options === "string" ? { message: options } : options;
    modalState.value = {
      isOpen: true,
      title: opts.title || t("系统提示", "Notification"),
      message: opts.message,
      detail: opts.detail || "",
      type: opts.type || "info",
      showCancel: false,
      confirmText: opts.confirmText || t("我知道了", "Understood"),
      cancelText: "",
      resolve: () => resolve(),
    };
  });
}

function handleModalConfirm() {
  if (modalState.value.resolve) {
    modalState.value.resolve(true);
  }
  modalState.value.isOpen = false;
  modalState.value.resolve = null;
}

function handleModalCancel() {
  if (modalState.value.resolve) {
    modalState.value.resolve(false);
  }
  modalState.value.isOpen = false;
  modalState.value.resolve = null;
}

function handleModalKeydown(e: KeyboardEvent) {
  if (!modalState.value.isOpen) return;
  if (e.key === "Escape") {
    handleModalCancel();
  } else if (e.key === "Enter") {
    handleModalConfirm();
  }
}

// Language & Internationalization State
type LangType = "zh" | "en";

function detectInitialLanguage(): LangType {
  const saved = localStorage.getItem("app_lang");
  if (saved === "zh" || saved === "en") return saved;
  const sysLang = navigator.language || (navigator as any).userLanguage || "zh-CN";
  return sysLang.toLowerCase().startsWith("zh") ? "zh" : "en";
}

const currentLang = ref<LangType>(detectInitialLanguage());

function setLanguage(lang: LangType) {
  currentLang.value = lang;
  localStorage.setItem("app_lang", lang);
}

function toggleLanguage() {
  currentLang.value = currentLang.value === "zh" ? "en" : "zh";
  localStorage.setItem("app_lang", currentLang.value);
}

const t = (zh: string, en: string) => (currentLang.value === "zh" ? zh : en);

// Appearance Theme (System, Light, Dark)
const currentTheme = ref<ThemeType>((localStorage.getItem("app_theme") as ThemeType) || "system");
const isSystemDark = ref<boolean>(
  typeof window !== "undefined" && window.matchMedia ? window.matchMedia("(prefers-color-scheme: dark)").matches : false
);

const effectiveTheme = computed<"light" | "dark">(() => {
  if (currentTheme.value === "dark") return "dark";
  if (currentTheme.value === "light") return "light";
  return isSystemDark.value ? "dark" : "light";
});

function applyTheme(theme: ThemeType) {
  const isDark =
    theme === "dark" ||
    (theme === "system" && isSystemDark.value);
  if (isDark) {
    document.documentElement.setAttribute("data-theme", "dark");
  } else {
    document.documentElement.removeAttribute("data-theme");
  }
}

function setTheme(theme: ThemeType) {
  currentTheme.value = theme;
  localStorage.setItem("app_theme", theme);
  applyTheme(theme);
}

// Color Scheme (Default/White, Crimson, Emerald, Ocean)
const currentColorScheme = ref<ColorSchemeType>(
  (localStorage.getItem("app_color_scheme") as ColorSchemeType) || "default"
);

function applyColorScheme(scheme: ColorSchemeType) {
  document.documentElement.setAttribute("data-color-scheme", scheme);
}

function setColorScheme(scheme: ColorSchemeType) {
  currentColorScheme.value = scheme;
  localStorage.setItem("app_color_scheme", scheme);
  applyColorScheme(scheme);
}

// Settings State
const currentSettingsTab = ref<SettingsTab>("general");
const localRepoRoot = ref<string>("");

async function handleOpenFolder(folder: string) {
  try {
    await invoke("open_folder", { folderName: folder });
  } catch (e) {
    console.error("Failed to open folder:", e);
  }
}

async function openQqGroup() {
  const url =
    "https://qun.qq.com/universal-share/share?ac=1&authKey=O9lTkl%2B56Zf8U%2BMSk3UrLK7HmOIJZlhbfESHPi1PUSo2PBHzH6o003oDOf9UAJFg&busi_data=eyJncm91cENvZGUiOiIxMTI4MTMwMTk5IiwidG9rZW4iOiJ5a1M5T0dmeFBKVDg2bnRYaHJkcFhTYXh5NjJWcUtDdHlpNk1HZWdUT2FqZDJkTUMzci9EUFFPVU55b1ZCL01yIiwidWluIjoiNzg1MDI5NDk3In0%3D&data=qDBRJgG4lwWBURR5AYk5mFRku441wXmLM2AdJqE-E6ZIaL3X6ljAIp2woaaHgY6TQIqsfXnGPEhGUGW5m-KjXw&svctype=4&tempid=h5_group_info";
  try {
    await invoke("open_in_browser", { url });
  } catch (e) {
    window.open(url, "_blank");
  }
}

// Remote Version & Update Checker State
const isCheckingUpdate = ref(false);

interface RemoteVersionInfo {
  version: string;
  releaseDate?: string;
  title?: string;
  changelog?: string[];
  downloadUrl?: string;
  notice?: string;
}

function isNewerVersion(remote: string, current: string): boolean {
  const rParts = remote.replace(/^v/, "").split(".").map((n) => parseInt(n, 10) || 0);
  const cParts = current.replace(/^v/, "").split(".").map((n) => parseInt(n, 10) || 0);
  const len = Math.max(rParts.length, cParts.length);
  for (let i = 0; i < len; i++) {
    const r = rParts[i] || 0;
    const c = cParts[i] || 0;
    if (r > c) return true;
    if (r < c) return false;
  }
  return false;
}

async function checkAppUpdate() {
  if (isCheckingUpdate.value) return;
  isCheckingUpdate.value = true;

  const currentVer = "1.0.0";
  const mirrors = [
    "https://ghproxy.net/https://raw.githubusercontent.com/Q4446/Pmod/main/version.json",
    "https://fastly.jsdelivr.net/gh/Q4446/Pmod@main/version.json",
    "https://cdn.jsdelivr.net/gh/Q4446/Pmod@main/version.json",
    "https://raw.githubusercontent.com/Q4446/Pmod/main/version.json"
  ];

  let data: RemoteVersionInfo | null = null;
  for (const url of mirrors) {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 4000);
      const resp = await fetch(`${url}?_t=${Date.now()}`, {
        cache: "no-store",
        signal: controller.signal
      });
      clearTimeout(timeoutId);
      if (resp.ok) {
        data = (await resp.json()) as RemoteVersionInfo;
        if (data && data.version) break;
      }
    } catch (e) {
      // try next mirror
    }
  }

  isCheckingUpdate.value = false;

  if (!data || !data.version) {
    await showAlert({
      title: t("检查更新提示", "Check Update Notification"),
      message: t(
        "未能成功连接到版本更新服务器，请检查网络连接，或直接加入官方交流群（1128130199）获取最新安装包。",
        "Failed to connect to version server. You can join community group 1128130199 for latest packages."
      ),
      type: "warning",
      confirmText: t("知道了", "Understood"),
    });
    return;
  }

  if (isNewerVersion(data.version, currentVer)) {
    const changesText = (data.changelog || []).map((item) => `• ${item}`).join("\n");
    const msg = `${data.title || "Pmod 发现新版本 " + data.version} (${data.releaseDate || ""})\n\n${changesText}\n\n${data.notice || ""}`;
    const confirmed = await showConfirm({
      title: t(`🎉 发现新版本 v${data.version}`, `🎉 New Version v${data.version} Available`),
      message: msg,
      confirmText: t("前往下载更新", "Download Update"),
      cancelText: t("稍后提醒", "Later"),
      type: "success",
    });
    if (confirmed) {
      const downloadUrl =
        data.downloadUrl ||
        "https://qun.qq.com/universal-share/share?ac=1&authKey=O9lTkl%2B56Zf8U%2BMSk3UrLK7HmOIJZlhbfESHPi1PUSo2PBHzH6o003oDOf9UAJFg&busi_data=eyJncm91cENvZGUiOiIxMTI4MTMwMTk5IiwidG9rZW4iOiJ5a1M5T0dmeFBKVDg2bnRYaHJkcFhTYXh5NjJWcUtDdHlpNk1HZWdUT2FqZDJkTUMzci9EUFFPVU55b1ZCL01yIiwidWluIjoiNzg1MDI5NDk3In0%3D&data=qDBRJgG4lwWBURR5AYk5mFRku441wXmLM2AdJqE-E6ZIaL3X6ljAIp2woaaHgY6TQIqsfXnGPEhGUGW5m-KjXw&svctype=4&tempid=h5_group_info";
      try {
        await invoke("open_in_browser", { url: downloadUrl });
      } catch (e) {
        window.open(downloadUrl, "_blank");
      }
    }
  } else {
    await showAlert({
      title: t("已是最新版本", "Up to Date"),
      message: t(
        `当前运行的 Pmod (v${currentVer}) 已是最新正式版本，暂无可用更新。`,
        `Current Pmod (v${currentVer}) is already the latest release.`
      ),
      type: "info",
      confirmText: t("好的", "OK"),
    });
  }
}

function switchToSettings() {
  currentTab.value = "settings";
}

// Navigation & Layout State
const currentTab = ref<TabType>("chat");
const isSidebarCollapsed = ref<boolean>(false);
const modelNotConfiguredWarning = ref<boolean>(false);

// Service & System State
const serviceState = ref<ServiceState>({ status: "Stopped" });
const gpuStats = ref<GpuStats | null>(null);
const appConfig = ref<AppConfig | null>(null);
const modelMetadata = ref<ModelMetadata | null>(null);
const isInspectingModel = ref<boolean>(false);

async function refreshModelMetadata(path: string | undefined | null) {
  if (!path || !path.trim()) {
    modelMetadata.value = null;
    return;
  }
  isInspectingModel.value = true;
  try {
    const meta = await invoke<ModelMetadata>("inspect_model_metadata", { path: path.trim() });
    modelMetadata.value = meta;
    if (meta.is_valid && meta.model_id && appConfig.value && (!appConfig.value.ModelId || appConfig.value.ModelId === 'local-model')) {
      appConfig.value.ModelId = meta.model_id;
    }
  } catch (err) {
    console.error("Failed to inspect model metadata:", err);
  } finally {
    isInspectingModel.value = false;
  }
}

let modelPathInspectTimer: any = null;
function handleModelPathInput() {
  clearTimeout(modelPathInspectTimer);
  modelPathInspectTimer = setTimeout(() => {
    if (appConfig.value?.ModelPath) {
      refreshModelMetadata(appConfig.value.ModelPath);
    } else {
      modelMetadata.value = null;
    }
  }, 300);
}
const logs = ref<LogEntry[]>([]);
const activeLogFilter = ref<string>("all");
const autoScroll = ref<boolean>(true);
const logContainer = ref<HTMLDivElement | null>(null);
const iframeKey = ref<number>(0);

// High-FPS Viewport Scroll Throttling & Hover Disabling
const isViewportScrolling = ref<boolean>(false);
let viewportScrollTimer: any = null;
function handleViewportScroll() {
  if (!isViewportScrolling.value) {
    isViewportScrolling.value = true;
  }
  clearTimeout(viewportScrollTimer);
  viewportScrollTimer = setTimeout(() => {
    isViewportScrolling.value = false;
  }, 200);
}

// VRAM Inspector State
const isVramLoading = ref<boolean>(false);
const vramBreakdown = ref<VramBreakdown | null>(null);
const vramSearchQuery = ref<string>("");
const vramCategoryFilter = ref<string>("all");
const terminatingPid = ref<number | null>(null);
const copySuccessNotice = ref<string>("");

// Dual GPU & Hybrid Switch State
const selectedGpuTab = ref<"dgpu" | "igpu">("dgpu");
const isMigratingPid = ref<number | null>(null);
const batchMigrating = ref<boolean>(false);

const isHybridGpuSupported = computed(() => {
  return !!vramBreakdown.value?.topology?.is_hybrid_supported;
});

const dgpuName = computed(() => {
  const smiName = (gpuStats.value?.gpu_name || "").trim();
  if (smiName) return smiName;
  return (vramBreakdown.value?.topology?.dgpu_name || "").trim();
});

const dgpuDriver = computed(() => {
  const smiDrv = (gpuStats.value?.driver_version || "").trim();
  if (smiDrv) return smiDrv;
  const wmiDrv = (vramBreakdown.value?.topology?.dgpu_driver || "").trim();
  // Convert Windows WDDM version e.g. "32.0.15.9186" -> "591.86"
  const m = wmiDrv.match(/\.(\d)\.(\d{4})$/);
  if (m) {
    return `${m[1]}${m[2].slice(0, 2)}.${m[2].slice(2)}`;
  }
  return wmiDrv;
});

const detectedComputeCap = computed(() => {
  return gpuStats.value?.compute_cap || "";
});

// Detect actual GPU architecture family from compute capability or GPU name
const detectedHardwareArch = computed<"sm120a" | "sm89" | "sm86" | "unknown">(() => {
  const cc = (detectedComputeCap.value || "").trim();
  const name = (dgpuName.value || "").toUpperCase();
  if (cc.startsWith("12.") || /RTX\s*50\d{2}|BLACKWELL/i.test(name)) {
    return "sm120a";
  }
  if (cc === "8.9" || /RTX\s*40\d{2}|ADA\s*LOVELACE|\bL40/i.test(name)) {
    return "sm89";
  }
  if (cc === "8.6" || cc === "8.0" || /RTX\s*30\d{2}|AMPERE|\bA100|\bA40|\bA6000|\bA5000|\bA4000/i.test(name)) {
    return "sm86";
  }
  if (name.length > 0 && name !== "NVIDIA GPU" && name !== "INTEGRATED GPU") {
    return "unknown";
  }
  return "sm120a";
});

const isUsingDefaultBuiltInDriver = computed<boolean>(() => {
  const p = (appConfig.value?.EnginePath || "").trim().replace(/\\/g, "/");
  return !p || p === "runtime/engine/ninfer-serve.exe";
});

const isCudaDriverOutdated = computed<boolean>(() => {
  const drv = (dgpuDriver.value || "").trim();
  if (!drv) return false;
  const ver = parseFloat(drv);
  return !isNaN(ver) && ver >= 100 && ver < 570;
});

// True ONLY when auto-detection finds the user's GPU is NOT RTX 50 series (and no custom engine driver is configured yet)
const isGpuArchMismatch = computed<boolean>(() => {
  const hasDetectedGpu = !!(dgpuName.value || detectedComputeCap.value);
  if (!hasDetectedGpu) return false;
  if (detectedHardwareArch.value !== "sm120a" && isUsingDefaultBuiltInDriver.value) {
    return true;
  }
  return false;
});

const detectedArchFullLabel = computed<string>(() => {
  const arch = detectedHardwareArch.value;
  const cc = detectedComputeCap.value ? `SM ${detectedComputeCap.value}` : "";
  if (arch === "sm120a") return `Blackwell · sm_120a${cc ? " (" + cc + ")" : ""}`;
  if (arch === "sm89") return `Ada Lovelace · sm_89${cc ? " (" + cc + ")" : ""}`;
  if (arch === "sm86") return `Ampere · sm_86${cc ? " (" + cc + ")" : ""}`;
  return cc || t("非 50 系架构", "Non-50 Series");
});

const hasPromptedDriverCheck = ref<boolean>(false);

async function checkHardwareDriverOnStartup() {
  if (hasPromptedDriverCheck.value) return;
  const hasDetectedGpu = !!(dgpuName.value || detectedComputeCap.value);
  if (!hasDetectedGpu) return;
  hasPromptedDriverCheck.value = true;

  // Automatically sync GpuArch & KVDType when non-50 series is detected
  if (appConfig.value && detectedHardwareArch.value !== "sm120a" && detectedHardwareArch.value !== "unknown") {
    appConfig.value.GpuArch = detectedHardwareArch.value;
    if (appConfig.value.KVDType === "nvfp4") {
      appConfig.value.KVDType = "rk4v4";
    }
  }
}

async function handlePickEngine() {
  try {
    const selected = await invoke<string | null>("pick_engine_file");
    if (!selected || !appConfig.value) return;
    appConfig.value.EnginePath = selected;
    if (detectedHardwareArch.value !== "unknown") {
      appConfig.value.GpuArch = detectedHardwareArch.value;
    }
    if (detectedHardwareArch.value !== "sm120a" && appConfig.value.KVDType === "nvfp4") {
      appConfig.value.KVDType = "rk4v4";
    }
    await invoke("update_app_config", { config: appConfig.value });
    setSaveNotice(t(`✓ 已成功配置引擎驱动文件：${selected}`, `✓ Configured engine driver binary: ${selected}`));
  } catch (e: any) {
    await showAlert({
      title: t("配置引擎驱动失败", "Failed to Select Engine Driver"),
      message: t("选择引擎驱动文件时发生错误：\n", "Failed to select engine executable:\n") + e,
      type: "danger",
    });
  }
}

async function resetEnginePath() {
  if (!appConfig.value) return;
  appConfig.value.EnginePath = "runtime/engine/ninfer-serve.exe";
  appConfig.value.GpuArch = "sm120a";
  try {
    await invoke("update_app_config", { config: appConfig.value });
    setSaveNotice(t("✓ 已恢复系统默认内置 50 系列引擎驱动 (runtime/engine/ninfer-serve.exe)", "✓ Restored default built-in RTX 50 series engine driver"));
  } catch {}
}

const igpuName = computed(() => {
  return vramBreakdown.value?.topology?.igpu_name || "Intel(R) Graphics";
});

const currentProcessList = computed(() => {
  if (!vramBreakdown.value) return [];
  if (selectedGpuTab.value === "igpu") {
    return vramBreakdown.value.igpu_processes || [];
  }
  return vramBreakdown.value.dgpu_processes || vramBreakdown.value.processes || [];
});

const dgpuAppsCount = computed(() => {
  if (!vramBreakdown.value) return 0;
  return (vramBreakdown.value.dgpu_processes || vramBreakdown.value.processes || []).length;
});

const igpuAppsCount = computed(() => {
  if (!vramBreakdown.value) return 0;
  return (vramBreakdown.value.igpu_processes || []).length;
});

// GPU percent calculation
const vramPercent = computed(() => {
  if (!gpuStats.value || gpuStats.value.total_mb === 0) return 0;
  return Math.min(100, Math.round((gpuStats.value.used_mb / gpuStats.value.total_mb) * 100));
});

const vramStatusColor = computed(() => {
  const pct = vramPercent.value;
  if (pct > 95) return "#ef4444";
  if (pct > 85) return "#f59e0b";
  return "#10b981";
});

// Universal Parsed Model Metadata
const parsedModelMeta = computed(() => {
  if (modelMetadata.value && modelMetadata.value.is_valid) {
    const isComp = modelMetadata.value.is_engine_compatible && modelMetadata.value.format === "NINFER";
    return {
      name: modelMetadata.value.filename.replace(/\.[^/.]+$/, ""),
      filename: modelMetadata.value.filename,
      arch: modelMetadata.value.architecture || "Generic",
      params: modelMetadata.value.param_size || "--",
      quant: modelMetadata.value.quant_type || "--",
      format: modelMetadata.value.format || "NINFER",
      isConfigured: true,
      isCompatible: isComp,
      layer_count: modelMetadata.value.layer_count,
      hidden_size: modelMetadata.value.hidden_size,
      context_window: modelMetadata.value.context_window,
      source_origin: modelMetadata.value.source_origin,
      compatibility_notice: modelMetadata.value.compatibility_notice,
    };
  }

  const path = appConfig.value?.ModelPath || "";
  if (!path) {
    return {
      name: t("未配置本地模型", "No Model Specified"),
      filename: "",
      arch: "None",
      params: "--",
      quant: "--",
      format: "--",
      isConfigured: false,
      isCompatible: true,
      layer_count: 0,
      hidden_size: 0,
      context_window: 0,
      source_origin: "",
      compatibility_notice: "",
    };
  }

  const parts = path.replace(/\\/g, "/").split("/");
  const filename = parts[parts.length - 1] || path;
  const extMatch = filename.match(/\.(ninfer|gguf|safetensors|onnx|bin)$/i);
  const format = extMatch ? extMatch[1].toUpperCase() : "NINFER";
  const isCompatible = format === "NINFER";

  let arch = "Generic";
  if (/qwen/i.test(filename)) arch = "Qwen";
  else if (/deepseek/i.test(filename)) arch = "DeepSeek";
  else if (/llama/i.test(filename)) arch = "LLaMA";
  else if (/mistral|mixtral|ministral/i.test(filename)) arch = "Mistral";
  else if (/gemma/i.test(filename)) arch = "Gemma";
  else if (/phi[-_]?[0-9]/i.test(filename)) arch = "Phi";

  const paramMatch = filename.match(/([0-9]+(?:\.[0-9]+)?)[bB]/);
  const params = paramMatch ? paramMatch[0].toUpperCase() : "27B";

  const quantMatch = filename.match(/(IQ[0-9]_[A-Z0-9]+|Q[0-9]_[A-Z0-9_]+|FP16|FP8|BF16|INT8|INT4|RK4V4|NVFP4)/i);
  const quant = quantMatch
    ? quantMatch[0].toUpperCase()
    : appConfig.value?.KVDType
    ? appConfig.value.KVDType.toUpperCase()
    : "IQ3_XXS";

  const cleanName = filename.replace(/\.(ninfer|gguf|safetensors|onnx|bin)$/i, "");

  return {
    name: cleanName,
    filename,
    arch,
    params,
    quant,
    format,
    isConfigured: true,
    isCompatible,
    layer_count: 64,
    hidden_size: 5120,
    context_window: 262144,
    source_origin: "filename",
    compatibility_notice: isCompatible ? "" : t("当前 NInfer 引擎仅支持 .ninfer 模型", "NInfer engine only accepts .ninfer artifacts"),
  };
});

// Dynamic Model Display Name
const detectedModelDisplayName = computed(() => {
  if (!appConfig.value || !appConfig.value.ModelPath) return "";
  const meta = parsedModelMeta.value;
  if (meta.isConfigured && meta.arch !== "Generic" && meta.arch !== "None") {
    return `${meta.arch} ${meta.params !== "--" ? meta.params : ""}`.trim();
  }
  return meta.isConfigured ? meta.name : t("本地大模型", "Local Model");
});

// Universal Engine Configuration State
const selectedHwTier = ref<string>("auto");
const showAdvancedConfig = ref<boolean>(true);
const configSaveNotice = ref<string>("");
let saveNoticeTimer: any = null;

function setSaveNotice(msg: string) {
  configSaveNotice.value = msg;
  if (saveNoticeTimer) clearTimeout(saveNoticeTimer);
  saveNoticeTimer = setTimeout(() => {
    configSaveNotice.value = "";
  }, 4000);
}

// Watch DeviceKV to keep ring buffer parameters strictly balanced
watch(() => appConfig.value?.DeviceKV, (newVal) => {
  if (!newVal || !appConfig.value) return;
  appConfig.value.RecentTokens = Math.round(newVal * 0.5);
  appConfig.value.SinkTokens = Math.round(newVal * 0.428);
  appConfig.value.RetrieveTokens = Math.max(1024, Math.round(newVal * 0.053));
});

// Dynamic VRAM Estimation Computeds
const estimatedKvVramGb = computed(() => {
  const tokens = appConfig.value?.DeviceKV || 32768;
  const is4bit = appConfig.value?.KVDType === "rk4v4" || appConfig.value?.KVDType === "nvfp4";
  const bytesPerToken = is4bit ? 36864 : appConfig.value?.KVDType === "q8_0" ? 65536 : 131072;
  return ((tokens * bytesPerToken) / (1024 * 1024 * 1024)).toFixed(2);
});

function autoDetectHardwareTier(): string {
  const gb = Number(detectedTotalVramGb.value) || 0;
  if (gb <= 0) return "cpu-mode";
  if (gb < 9) return "entry-8g";
  if (gb < 14) return "main-12g";
  if (gb < 20) return "perf-16g";
  return "flagship-24g";
}

function applyHardwarePreset(tier: string) {
  if (!appConfig.value) return;
  selectedHwTier.value = tier;

  if (tier === "entry-8g") {
    appConfig.value.DeviceKV = 16384;
    appConfig.value.HostKVMiB = 4096;
    appConfig.value.KVDType = "rk4v4";
    appConfig.value.MaxContext = 65536;
    appConfig.value.RecentTokens = 9216;
    appConfig.value.SinkTokens = 6144;
    appConfig.value.RetrieveTokens = 1024;
    setSaveNotice(t("已载入【6G~8G 移动端档位】配置", "Applied 6G~8G Entry GPU preset"));
  } else if (tier === "main-12g") {
    appConfig.value.DeviceKV = 28672;
    appConfig.value.HostKVMiB = 7168;
    appConfig.value.KVDType = "rk4v4";
    appConfig.value.MaxContext = 262144;
    appConfig.value.RecentTokens = 14336;
    appConfig.value.SinkTokens = 12288;
    appConfig.value.RetrieveTokens = 1536;
    setSaveNotice(t("已载入【10G~12G 档位】配置", "Applied 10G~12G Mainstream GPU preset"));
  } else if (tier === "perf-16g") {
    appConfig.value.DeviceKV = 49152;
    appConfig.value.HostKVMiB = 12288;
    appConfig.value.KVDType = "nvfp4";
    appConfig.value.MaxContext = 262144;
    appConfig.value.RecentTokens = 24576;
    appConfig.value.SinkTokens = 18432;
    appConfig.value.RetrieveTokens = 2048;
    setSaveNotice(t("已载入【16G 档位】配置", "Applied 16G Performance GPU preset"));
  } else if (tier === "flagship-24g") {
    appConfig.value.DeviceKV = 65536;
    appConfig.value.HostKVMiB = 16384;
    appConfig.value.KVDType = "nvfp4";
    appConfig.value.MaxContext = 262144;
    appConfig.value.RecentTokens = 32768;
    appConfig.value.SinkTokens = 24576;
    appConfig.value.RetrieveTokens = 4096;
    setSaveNotice(t("已载入【24G+ 档位】配置", "Applied 24G+ Flagship GPU preset"));
  } else if (tier === "cpu-mode") {
    appConfig.value.DeviceKV = 8192;
    appConfig.value.HostKVMiB = 16384;
    appConfig.value.KVDType = "rk4v4";
    appConfig.value.MaxContext = 32768;
    appConfig.value.RecentTokens = 4096;
    appConfig.value.SinkTokens = 3072;
    appConfig.value.RetrieveTokens = 512;
    setSaveNotice(t("已载入【CPU/系统内存模式】配置", "Applied CPU/Shared Memory preset"));
  }
}

function autoTuneForCurrentHardware() {
  if (!appConfig.value) return;
  const gb = Number(detectedTotalVramGb.value) || 0;
  const gpuName = vramBreakdown.value?.topology?.dgpu_name || detectedGpuShortName.value || "通用计算设备";
  applyHardwarePreset(autoDetectHardwareTier());
  setSaveNotice(t(
    `已依据实测硬件 [${gpuName} · ${gb > 0 ? gb + 'GB 显存' : '系统内存模式'}] 自动调优引擎参数`,
    `Auto-tuned parameters for [${gpuName} · ${gb > 0 ? gb + 'GB VRAM' : 'CPU Mode'}]`
  ));
}

function handleResetDefaults() {
  if (!appConfig.value) return;
  autoTuneForCurrentHardware();
  appConfig.value.GpuArch = detectedHardwareArch.value !== "unknown" ? detectedHardwareArch.value : "sm120a";
  appConfig.value.ReasoningEffort = "xhigh";
  appConfig.value.AnswerPenaltyScope = "answer";
  setSaveNotice(t("已恢复出厂硬件自适应基准参数", "Reset to hardware adaptive baseline"));
}

// Dynamic GPU Short Name
const detectedGpuShortName = computed(() => {
  const full = dgpuName.value || "";
  if (!full) return "";
  const m = full.match(/(RTX\s+[0-9]{4}(\s*Ti)?|GTX\s+[0-9]{4}|Radeon\s+[A-Za-z0-9]+|Arc\s+[A-Za-z0-9]+)/i);
  if (m) return m[0].trim();
  return full.replace(/NVIDIA\s+|GeForce\s+|Laptop\s+|GPU/gi, "").trim();
});

// Dynamic Total VRAM GB
const detectedTotalVramGb = computed(() => {
  if (gpuStats.value?.total_mb && gpuStats.value.total_mb > 0) {
    return (gpuStats.value.total_mb / 1024).toFixed(1);
  }
  if (vramBreakdown.value?.total_mb && vramBreakdown.value.total_mb > 0) {
    return (vramBreakdown.value.total_mb / 1024).toFixed(1);
  }
  return "";
});

// Dynamic Combined GPU & VRAM Badge
const detectedGpuBadge = computed(() => {
  const gpu = detectedGpuShortName.value;
  const gb = detectedTotalVramGb.value;
  if (!gpu) return "";
  if (gb) return `${gpu} ${gb}GB`;
  return gpu;
});

// Extract clean model file description (strictly no model branding leaks)
const modelFileName = computed(() => {
  if (!appConfig.value || !appConfig.value.ModelPath) return t("未配置模型文件", "No model configured");
  const parts = appConfig.value.ModelPath.replace(/\\/g, "/").split("/");
  const raw = parts[parts.length - 1] || appConfig.value.ModelPath;
  if (/qwen/i.test(raw)) {
    return t("本地大模型权重文件 (.ninfer)", "Local Model Weights (.ninfer)");
  }
  return raw;
});

// Dynamic Standby DSH Hero Title & Status (accurately reflects stopped/starting/error)
const dshHeroTitle = computed(() => {
  if (serviceState.value.status === 'Starting') {
    return t('本地大模型 · 正在启动...', 'Local Model · Starting...');
  }
  if (serviceState.value.status === 'Error') {
    return t('本地大模型 · 启动异常', 'Local Model · Startup Issue');
  }
  if (!isModelConfigured.value) {
    return t('本地大模型 · 待配置', 'Local Model · Not Configured');
  }
  return t('本地大模型工作台', 'Local AI Studio');
});

const dshHeroChipText = computed(() => {
  if (serviceState.value.status === 'Starting') {
    return t('正在初始化大模型引擎与显存分配，请稍候...', 'Starting model engine and allocating VRAM, please wait...');
  }
  if (serviceState.value.status === 'Error') {
    return t('服务遇到异常 · 请检查上方提示或点击重试启动', 'Startup issue · Please check notice above or retry');
  }
  if (!isModelConfigured.value) {
    return t('尚未配置本地模型权重 (.ninfer) · 请点击下方按钮指定模型', 'Model not configured · Please click button below to select .ninfer file');
  }
  return t('轻量化端侧大模型推理引擎已就绪 · 点击下方按钮一键载入', 'Local inference engine ready · Click button below to launch');
});

// Smart Human-friendly Startup Error Formatter
const friendlyErrorMessage = computed(() => {
  const raw = serviceState.value.data?.message;
  if (!raw) return t('启动遇到错误，请检查显存占用或模型文件。', 'Startup encountered an error.');

  if (raw.includes('ERR_MODULE_NOT_FOUND') || raw.includes('Cannot find package') || raw.includes('Cannot find module') || raw.includes('依赖包解析异常')) {
    return t('智能体依赖组件初始化异常，系统已就地自愈修复，请点击右侧「重试启动」', 'Agent package initialization failed. Environment auto-repaired, please click Retry.');
  }
  if (raw.includes('-4094') || raw.includes('mkdir') || raw.includes('EPERM') || raw.includes('EACCES') || raw.includes('目录发生冲突')) {
    return t('运行时缓存目录发生访问冲突，系统已完成自愈清理，请点击右侧「重试启动」', 'Runtime cache path conflict detected and healed. Please click Retry.');
  }
  if (raw.includes('exists and is not a symlink') || raw.includes('符号链接冲突')) {
    return t('检测到旧版智能体符号链接冲突，已自动完成自愈清理，请点击右侧「重试启动」', 'Legacy symlink conflict resolved automatically. Please click Retry.');
  }
  if (raw.includes('EADDRINUSE') || raw.includes('端口')) {
    return t('智能体服务端口被其他应用占用，请关闭冲突程序后重试', 'Port is occupied by another application. Please close it and retry.');
  }
  if (raw.includes('out of memory') || raw.includes('CUDA') || raw.includes('显存不足')) {
    return t('独显显存空间不足，请点击右侧「排查显存占用」释放空间', 'VRAM space insufficient. Please click Inspect VRAM to release memory.');
  }
  if (raw.includes('only .ninfer artifacts') || raw.includes('模型文件格式错误') || raw.includes('NInfer accepts only .ninfer') || raw.includes('格式不匹配') || raw.includes('格式错误')) {
    return t('模型文件格式错误：当前 NInfer 高性能引擎仅支持加载标准 .ninfer 格式权重制品。当前文件格式不兼容，请前往「模型」页面更换为有效 .ninfer 模型。', 'Incompatible model format: NInfer engine only accepts .ninfer artifacts. Please switch to a valid .ninfer file in Model settings.');
  }

  // Clean raw traces if any
  let clean = raw.replace(/Node\.js\s+v\d+(\.\d+)*/g, '')
                 .replace(/\|\s*code:\s*'[^']+',?/g, '')
                 .replace(/\|\s*errno:\s*-?\d+,?/g, '')
                 .replace(/\|\s*syscall:\s*'[^']+',?/g, '')
                 .replace(/\[\s*:\s*:\s*\]/g, '')
                 .replace(/\\{2,}/g, '\\')
                 .trim();
  if (clean.endsWith('|') || clean.endsWith(']')) {
    clean = clean.replace(/[|\]\s]+$/, '').trim();
  }
  return clean || t('启动遇到错误，请查看日志或重试。', 'Startup error occurred. Please check logs or retry.');
});

// Check if model is properly configured and verified on disk
const isModelConfigured = computed(() => {
  if (!appConfig.value?.ModelPath || appConfig.value.ModelPath.trim().length === 0) {
    return false;
  }
  if (modelMetadata.value) {
    return modelMetadata.value.is_valid && modelMetadata.value.is_engine_compatible;
  }
  return true;
});

// Filtered logs
const filteredLogs = computed(() => {
  if (activeLogFilter.value === "all") return logs.value;
  return logs.value.filter((l) => l.source.toLowerCase().includes(activeLogFilter.value));
});

// ==============================================================
// TELEMETRY & CONVERSATION INFERENCE ANALYTICS
// ==============================================================
export interface TelemetryRecord {
  id: string;
  turnNumber: number;
  timestamp: number;
  timeFormatted: string;
  taskType: string;
  promptPreview: string;
  ttftMs: number;
  thinkingDurationSec: number;
  thinkingTokens: number;
  outputDurationSec: number;
  outputTokens: number;
  generationSpeedTokPerSec: number;
  promptTokens: number;
  totalTokens: number;
  cacheHitRatePct: number;
  steps?: number;
  status: "success" | "generating" | "error";
}

function buildSmoothPath(points: { x: number; y: number }[]): string {
  if (points.length === 0) return "";
  if (points.length === 1) return `M ${points[0].x.toFixed(1)} ${points[0].y.toFixed(1)}`;
  let d = `M ${points[0].x.toFixed(1)} ${points[0].y.toFixed(1)}`;
  for (let i = 0; i < points.length - 1; i++) {
    const p0 = points[i === 0 ? 0 : i - 1];
    const p1 = points[i];
    const p2 = points[i + 1];
    const p3 = points[i + 2] || p2;

    const cp1x = p1.x + (p2.x - p0.x) / 6;
    const cp1y = p1.y + (p2.y - p0.y) / 6;
    const cp2x = p2.x - (p3.x - p1.x) / 6;
    const cp2y = p2.y - (p3.y - p1.y) / 6;

    d += ` C ${cp1x.toFixed(1)} ${cp1y.toFixed(1)}, ${cp2x.toFixed(1)} ${cp2y.toFixed(1)}, ${p2.x.toFixed(1)} ${p2.y.toFixed(1)}`;
  }
  return d;
}

function getDemoTelemetryRecords(): TelemetryRecord[] {
  const now = Date.now();
  const formatTime = (ts: number) => {
    const d = new Date(ts);
    return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
  };

  return [
    {
      id: "demo_10",
      turnNumber: 10,
      timestamp: now - 3 * 60 * 1000,
      timeFormatted: formatTime(now - 3 * 60 * 1000),
      taskType: "代码生成",
      promptPreview: "为本地 Ninfer 引擎编写多层页表预取与异步反序列化校验器...",
      ttftMs: 295,
      thinkingDurationSec: 13.8,
      thinkingTokens: 1420,
      outputDurationSec: 15.2,
      outputTokens: 1060,
      generationSpeedTokPerSec: 69.7,
      promptTokens: 4680,
      totalTokens: 7160,
      cacheHitRatePct: 91.5,
      status: "success",
    },
    {
      id: "demo_9",
      turnNumber: 9,
      timestamp: now - 12 * 60 * 1000,
      timeFormatted: formatTime(now - 12 * 60 * 1000),
      taskType: "架构分析",
      promptPreview: "分析当前硬件显存分配与 KV 缓存池调度策略，定位瓶颈并给出优化建议...",
      ttftMs: 340,
      thinkingDurationSec: 14.5,
      thinkingTokens: 1560,
      outputDurationSec: 18.0,
      outputTokens: 1220,
      generationSpeedTokPerSec: 67.8,
      promptTokens: 6800,
      totalTokens: 9580,
      cacheHitRatePct: 88.0,
      status: "success",
    },
    {
      id: "demo_8",
      turnNumber: 8,
      timestamp: now - 25 * 60 * 1000,
      timeFormatted: formatTime(now - 25 * 60 * 1000),
      taskType: "终端任务",
      promptPreview: "自动化执行终端命令与生成精准代码 Diff，核查依赖完整性与测试覆盖率...",
      ttftMs: 310,
      thinkingDurationSec: 11.2,
      thinkingTokens: 1180,
      outputDurationSec: 12.5,
      outputTokens: 890,
      generationSpeedTokPerSec: 71.2,
      promptTokens: 3450,
      totalTokens: 5520,
      cacheHitRatePct: 84.5,
      status: "success",
    },
    {
      id: "demo_7",
      turnNumber: 7,
      timestamp: now - 42 * 60 * 1000,
      timeFormatted: formatTime(now - 42 * 60 * 1000),
      taskType: "性能调优",
      promptPreview: "评估 MTP3 推测解码在不同 batch 尺寸下的显存带宽消耗与命中率对照...",
      ttftMs: 280,
      thinkingDurationSec: 12.0,
      thinkingTokens: 1260,
      outputDurationSec: 16.4,
      outputTokens: 1180,
      generationSpeedTokPerSec: 72.0,
      promptTokens: 5200,
      totalTokens: 7640,
      cacheHitRatePct: 94.2,
      status: "success",
    },
    {
      id: "demo_6",
      turnNumber: 6,
      timestamp: now - 60 * 60 * 1000,
      timeFormatted: formatTime(now - 60 * 60 * 1000),
      taskType: "代码重构",
      promptPreview: "重构 DSH Agent 的上下文缓存守卫逻辑，保持历史消息字节绝对稳定...",
      ttftMs: 360,
      thinkingDurationSec: 15.6,
      thinkingTokens: 1680,
      outputDurationSec: 21.5,
      outputTokens: 1450,
      generationSpeedTokPerSec: 67.4,
      promptTokens: 7200,
      totalTokens: 10330,
      cacheHitRatePct: 89.6,
      status: "success",
    },
    {
      id: "demo_5",
      turnNumber: 5,
      timestamp: now - 95 * 60 * 1000,
      timeFormatted: formatTime(now - 95 * 60 * 1000),
      taskType: "常规推理",
      promptPreview: "解释大语言模型中 Sink Tokens 与 Recent Tokens 的滑动窗口置换算法...",
      ttftMs: 325,
      thinkingDurationSec: 9.8,
      thinkingTokens: 980,
      outputDurationSec: 10.2,
      outputTokens: 710,
      generationSpeedTokPerSec: 69.6,
      promptTokens: 2100,
      totalTokens: 3790,
      cacheHitRatePct: 82.0,
      status: "success",
    },
    {
      id: "demo_4",
      turnNumber: 4,
      timestamp: now - 130 * 60 * 1000,
      timeFormatted: formatTime(now - 130 * 60 * 1000),
      taskType: "代码生成",
      promptPreview: "编写高效的 CUDA FP4 量化解压算子，适配本地 GPU 硬件特性...",
      ttftMs: 290,
      thinkingDurationSec: 16.2,
      thinkingTokens: 1740,
      outputDurationSec: 17.8,
      outputTokens: 1250,
      generationSpeedTokPerSec: 70.2,
      promptTokens: 6400,
      totalTokens: 9390,
      cacheHitRatePct: 93.0,
      status: "success",
    },
    {
      id: "demo_3",
      turnNumber: 3,
      timestamp: now - 170 * 60 * 1000,
      timeFormatted: formatTime(now - 170 * 60 * 1000),
      taskType: "架构分析",
      promptPreview: "诊断系统显存占用并识别可从独显迁移至核显的非关键后台进程...",
      ttftMs: 350,
      thinkingDurationSec: 10.5,
      thinkingTokens: 1100,
      outputDurationSec: 11.0,
      outputTokens: 780,
      generationSpeedTokPerSec: 70.9,
      promptTokens: 3800,
      totalTokens: 5680,
      cacheHitRatePct: 86.4,
      status: "success",
    },
    {
      id: "demo_2",
      turnNumber: 2,
      timestamp: now - 220 * 60 * 1000,
      timeFormatted: formatTime(now - 220 * 60 * 1000),
      taskType: "单元测试",
      promptPreview: "为缓存守卫的前缀匹配与重锚定机制编写单元测试套件...",
      ttftMs: 380,
      thinkingDurationSec: 8.5,
      thinkingTokens: 890,
      outputDurationSec: 8.6,
      outputTokens: 590,
      generationSpeedTokPerSec: 68.6,
      promptTokens: 2900,
      totalTokens: 4380,
      cacheHitRatePct: 79.5,
      status: "success",
    },
    {
      id: "demo_1",
      turnNumber: 1,
      timestamp: now - 260 * 60 * 1000,
      timeFormatted: formatTime(now - 260 * 60 * 1000),
      taskType: "常规推理",
      promptPreview: "你好！请介绍你的核心能力与终端编程支持特性...",
      ttftMs: 420,
      thinkingDurationSec: 6.2,
      thinkingTokens: 620,
      outputDurationSec: 7.5,
      outputTokens: 510,
      generationSpeedTokPerSec: 68.0,
      promptTokens: 1450,
      totalTokens: 2580,
      cacheHitRatePct: 0.0,
      status: "success",
    },
  ];
}

function loadInitialTelemetry(): TelemetryRecord[] {
  const saved = localStorage.getItem("antigravity_telemetry_records_v2");
  if (saved) {
    try {
      const parsed = JSON.parse(saved);
      if (Array.isArray(parsed)) {
        return parsed.sort((a, b) => b.timestamp - a.timestamp);
      }
    } catch {}
  }
  return [];
}

const telemetryRecords = ref<TelemetryRecord[]>(loadInitialTelemetry());
const isShowingDemoData = ref(false);
const telemetrySearch = ref("");
const telemetryTimeFilter = ref<"all" | "today" | "recent5">("all");
const activeChartMetric = ref<"speed" | "ttft" | "both">("speed");
const hoveredPoint = ref<{ record: TelemetryRecord; x: number; y: number } | null>(null);
const hoveredBar = ref<{ record: TelemetryRecord; x: number; y: number } | null>(null);
const isProbeRunning = ref(false);
const probeResultNotice = ref("");
const expandedTurnId = ref<string | null>(telemetryRecords.value[0]?.id || null);

function loadDemoTelemetry() {
  telemetryRecords.value = getDemoTelemetryRecords();
  isShowingDemoData.value = true;
  expandedTurnId.value = "demo_10";
}

function clearDemoTelemetry() {
  telemetryRecords.value = [];
  isShowingDemoData.value = false;
  expandedTurnId.value = null;
  localStorage.removeItem("antigravity_telemetry_records_v2");
  loadTelemetryFromBackend();
}

function saveTelemetryRecords() {
  localStorage.setItem("antigravity_telemetry_records_v2", JSON.stringify(telemetryRecords.value));
}

const filteredTelemetryRecords = computed(() => {
  let list = [...telemetryRecords.value].sort((a, b) => b.timestamp - a.timestamp);
  if (telemetryTimeFilter.value === "recent5") {
    list = list.slice(0, 5);
  } else if (telemetryTimeFilter.value === "today") {
    const todayStart = new Date().setHours(0, 0, 0, 0);
    list = list.filter((r) => r.timestamp >= todayStart);
  }
  if (telemetrySearch.value.trim()) {
    const q = telemetrySearch.value.trim().toLowerCase();
    list = list.filter(
      (r) =>
        r.taskType.toLowerCase().includes(q) ||
        r.promptPreview.toLowerCase().includes(q) ||
        `#${r.turnNumber}`.includes(q) ||
        r.timeFormatted.includes(q)
    );
  }
  return list;
});

const telemetryKPIs = computed(() => {
  const records = telemetryRecords.value;
  if (!records.length) {
    return {
      totalTurns: 0,
      totalTokens: 0,
      promptTokens: 0,
      thinkingTokens: 0,
      outputTokens: 0,
      avgTtftMs: 0,
      minTtftMs: 0,
      avgSpeed: 0,
      peakSpeed: 0,
      avgThinkingSec: 0,
      avgCacheHitPct: 0,
    };
  }

  let totalTokens = 0;
  let promptTokens = 0;
  let thinkingTokens = 0;
  let outputTokens = 0;
  let sumTtft = 0;
  let minTtft = 999999;
  let sumSpeed = 0;
  let peakSpeed = 0;
  let sumThinkingSec = 0;
  let sumCacheHit = 0;

  for (const r of records) {
    totalTokens += r.totalTokens;
    promptTokens += r.promptTokens;
    thinkingTokens += r.thinkingTokens;
    outputTokens += r.outputTokens;
    sumTtft += r.ttftMs;
    if (r.ttftMs > 0 && r.ttftMs < minTtft) minTtft = r.ttftMs;
    sumSpeed += r.generationSpeedTokPerSec;
    if (r.generationSpeedTokPerSec > peakSpeed) peakSpeed = r.generationSpeedTokPerSec;
    sumThinkingSec += r.thinkingDurationSec;
    sumCacheHit += r.cacheHitRatePct;
  }

  const count = records.length;
  return {
    totalTurns: count,
    totalTokens,
    promptTokens,
    thinkingTokens,
    outputTokens,
    avgTtftMs: Math.round(sumTtft / count),
    minTtftMs: minTtft === 999999 ? 0 : minTtft,
    avgSpeed: Math.round((sumSpeed / count) * 10) / 10,
    peakSpeed: Math.round(peakSpeed * 10) / 10,
    avgThinkingSec: Math.round((sumThinkingSec / count) * 10) / 10,
    avgCacheHitPct: Math.round((sumCacheHit / count) * 10) / 10,
  };
});

const chartChronologicalRecords = computed(() => {
  return [...telemetryRecords.value].reverse().slice(-10);
});

// Speed & TTFT Chart Computations
const speedChartData = computed(() => {
  const records = chartChronologicalRecords.value;
  if (!records.length) return null;

  const left = 50;
  const right = 720;
  const top = 30;
  const bottom = 175;
  const width = right - left;
  const height = bottom - top;

  const speeds = records.map((r) => r.generationSpeedTokPerSec);
  const ttfts = records.map((r) => r.ttftMs);

  const minSpeed = Math.max(0, Math.floor(Math.min(...speeds) - 5));
  const maxSpeed = Math.ceil(Math.max(...speeds) + 5);
  const minTtft = Math.max(0, Math.floor(Math.min(...ttfts) - 50));
  const maxTtft = Math.ceil(Math.max(...ttfts) + 50);

  const points = records.map((r, i) => {
    const x = records.length === 1 ? (left + right) / 2 : left + (i / (records.length - 1)) * width;
    const ySpeed = bottom - ((r.generationSpeedTokPerSec - minSpeed) / Math.max(1, maxSpeed - minSpeed)) * height;
    const yTtft = bottom - ((r.ttftMs - minTtft) / Math.max(1, maxTtft - minTtft)) * height;
    return { x, ySpeed, yTtft, record: r };
  });

  const speedPathPoints = points.map((p) => ({ x: p.x, y: p.ySpeed }));
  const ttftPathPoints = points.map((p) => ({ x: p.x, y: p.yTtft }));

  const speedLine = buildSmoothPath(speedPathPoints);
  const ttftLine = buildSmoothPath(ttftPathPoints);

  const lastX = points[points.length - 1]?.x || right;
  const firstX = points[0]?.x || left;

  const speedArea = speedLine ? `${speedLine} L ${lastX.toFixed(1)} ${bottom} L ${firstX.toFixed(1)} ${bottom} Z` : "";
  const ttftArea = ttftLine ? `${ttftLine} L ${lastX.toFixed(1)} ${bottom} L ${firstX.toFixed(1)} ${bottom} Z` : "";

  const avgSpeed = records.reduce((s, r) => s + r.generationSpeedTokPerSec, 0) / records.length;
  const avgTtft = records.reduce((s, r) => s + r.ttftMs, 0) / records.length;

  const avgSpeedY = bottom - ((avgSpeed - minSpeed) / Math.max(1, maxSpeed - minSpeed)) * height;
  const avgTtftY = bottom - ((avgTtft - minTtft) / Math.max(1, maxTtft - minTtft)) * height;

  return {
    points,
    speedLine,
    speedArea,
    ttftLine,
    ttftArea,
    minSpeed,
    maxSpeed,
    minTtft,
    maxTtft,
    avgSpeed: Math.round(avgSpeed * 10) / 10,
    avgTtft: Math.round(avgTtft),
    avgSpeedY,
    avgTtftY,
    left,
    right,
    top,
    bottom,
  };
});

// Stacked Bar Chart Computations
const barChartData = computed(() => {
  const records = chartChronologicalRecords.value;
  if (!records.length) return null;

  const left = 50;
  const right = 720;
  const top = 30;
  const bottom = 175;
  const totalW = right - left;
  const totalH = bottom - top;

  const maxTokens = Math.max(10000, ...records.map((r) => r.totalTokens));
  const count = records.length;
  const barWidth = Math.min(36, Math.floor(totalW / (count * 1.5)));
  const totalBarsW = count * barWidth;
  const gap = count > 1 ? (totalW - totalBarsW) / (count - 1) : 0;

  const bars = records.map((r, i) => {
    const x = count === 1 ? (left + right) / 2 - barWidth / 2 : left + i * (barWidth + gap);
    const hTotal = Math.max(10, (r.totalTokens / maxTokens) * totalH);
    const hPrompt = (r.promptTokens / r.totalTokens) * hTotal;
    const hThinking = (r.thinkingTokens / r.totalTokens) * hTotal;
    const hOutput = (r.outputTokens / r.totalTokens) * hTotal;

    const yPrompt = bottom - hPrompt;
    const yThinking = yPrompt - hThinking;
    const yOutput = yThinking - hOutput;

    return {
      x,
      barWidth,
      yPrompt,
      hPrompt,
      yThinking,
      hThinking,
      yOutput,
      hOutput,
      yTotal: yOutput,
      totalTokens: r.totalTokens,
      record: r,
    };
  });

  return {
    bars,
    maxTokens,
    left,
    right,
    top,
    bottom,
  };
});

async function runProbeBenchmark() {
  if (isProbeRunning.value) return;
  isProbeRunning.value = true;
  probeResultNotice.value = t("正在向本地大模型发起实时探针测速...", "Sending real-time probe to local engine...");

  try {
    const guardPort = appConfig.value?.GuardPort || 1236;
    const isRunning = serviceState.value.status === "Running";

    if (!isRunning) {
      probeResultNotice.value = t(
        "⚠️ 本地大模型未启动，无法进行实时硬件测速。请先点击右上角「启动模型」后再运行测速。",
        "⚠️ Local model is not running. Please start the engine first."
      );
      isProbeRunning.value = false;
      setTimeout(() => {
        probeResultNotice.value = "";
      }, 5000);
      return;
    }

    const testPrompt = "请简要分析本地混合显存架构的优势（50字以内）。";
    const t0 = performance.now();
    let ttft = 0;
    let thinkingChars = 0;
    let outputChars = 0;
    let thinkingStart = 0;
    let thinkingEnd = 0;

    const targetUrl = `http://127.0.0.1:${guardPort}/v1/chat/completions`;
    const resp = await fetch(targetUrl, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        model: "local-model",
        messages: [
          { role: "system", content: "You are a concise AI assistant." },
          { role: "user", content: testPrompt }
        ],
        stream: true,
        temperature: 0.6,
      })
    });

    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status} - 引擎服务未就绪`);
    }

    const reader = resp.body?.getReader();
    const decoder = new TextDecoder("utf-8");

    if (reader) {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        const now = performance.now();
        if (ttft === 0) {
          ttft = Math.round(now - t0);
        }
        const chunkStr = decoder.decode(value, { stream: true });
        if (chunkStr.includes('"reasoning_content"') || chunkStr.includes('<think>')) {
          if (!thinkingStart) thinkingStart = now;
          thinkingChars += chunkStr.length;
        } else if (chunkStr.includes('"content"') || chunkStr.includes('</think>')) {
          if (thinkingStart && !thinkingEnd) thinkingEnd = now;
          outputChars += chunkStr.length;
        }
      }
    }

    const totalDurationSec = Math.max(0.5, (performance.now() - t0) / 1000);
    const thinkingSec = (thinkingStart && thinkingEnd)
      ? Math.max(0.5, (thinkingEnd - thinkingStart) / 1000)
      : (totalDurationSec > 2 ? totalDurationSec * 0.35 : 0);
    const outputSec = Math.max(0.2, totalDurationSec - (thinkingSec || 0));
    const thinkingTok = Math.max(10, Math.round(thinkingChars / 3.5));
    const outputTok = Math.max(20, Math.round(outputChars / 3.5));
    const actualSpeed = Math.round((outputTok / outputSec) * 10) / 10;

    const formatTime = (ts: number) => {
      const d = new Date(ts);
      return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
    };

    const newTurn: TelemetryRecord = {
      id: "probe_" + Date.now().toString(36),
      turnNumber: telemetryRecords.value.length + 1,
      timestamp: Date.now(),
      timeFormatted: formatTime(Date.now()),
      taskType: "探针测速",
      promptPreview: testPrompt,
      ttftMs: ttft || 280,
      thinkingDurationSec: Math.round(thinkingSec * 10) / 10,
      thinkingTokens: thinkingTok,
      outputDurationSec: Math.round(outputSec * 10) / 10,
      outputTokens: outputTok,
      generationSpeedTokPerSec: actualSpeed,
      promptTokens: 85,
      totalTokens: 85 + thinkingTok + outputTok,
      cacheHitRatePct: 0.0,
      status: "success",
    };

    try {
      await invoke("save_telemetry_record", { record: newTurn });
    } catch (e) {
      console.error("Failed to persist probe record:", e);
    }

    telemetryRecords.value.unshift(newTurn);
    isShowingDemoData.value = false;
    saveTelemetryRecords();
    probeResultNotice.value = t(
      `✓ 硬件探针测速完成！首字时延: ${newTurn.ttftMs}ms · 解码速率: ${newTurn.generationSpeedTokPerSec} tok/s`,
      `✓ Hardware probe complete! TTFT: ${newTurn.ttftMs}ms · Speed: ${newTurn.generationSpeedTokPerSec} tok/s`
    );
  } catch (err: any) {
    probeResultNotice.value = t(`探针测速提示: ${err?.message || "引擎未连接"}`, `Probe: ${err?.message}`);
  } finally {
    isProbeRunning.value = false;
    setTimeout(() => {
      probeResultNotice.value = "";
    }, 5000);
  }
}

function exportTelemetry(format: "json" | "csv") {
  const records = telemetryRecords.value;
  if (!records.length) return;

  if (format === "json") {
    const jsonStr = JSON.stringify(records, null, 2);
    const blob = new Blob([jsonStr], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `inference-telemetry-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  } else {
    const headers = [
      "TurnNumber",
      "Timestamp",
      "Time",
      "TaskType",
      "TTFT_ms",
      "ThinkingSec",
      "ThinkingTokens",
      "OutputTokens",
      "Speed_tok_s",
      "PromptTokens",
      "TotalTokens",
      "CacheHit_pct",
      "Status",
    ];
    const rows = records.map((r) => [
      r.turnNumber,
      r.timestamp,
      `"${r.timeFormatted}"`,
      `"${r.taskType}"`,
      r.ttftMs,
      r.thinkingDurationSec,
      r.thinkingTokens,
      r.outputTokens,
      r.generationSpeedTokPerSec,
      r.promptTokens,
      r.totalTokens,
      r.cacheHitRatePct,
      r.status,
    ]);
    const csvContent = "\uFEFF" + [headers.join(","), ...rows.map((e) => e.join(","))].join("\n");
    const blob = new Blob([csvContent], { type: "text/csv;charset=utf-8;" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `inference-telemetry-${new Date().toISOString().slice(0, 10)}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  }
}

async function clearTelemetryHistory() {
  const confirmed = await showConfirm({
    title: t("重置统计历史", "Reset Telemetry History"),
    message: t(
      "确定要清空当前的推理性能统计历史吗？清空后将从磁盘删除所有会话遥测记录。",
      "Are you sure you want to reset the inference telemetry records? This will delete all records from disk."
    ),
    type: "warning",
    confirmText: t("重置清空", "Reset"),
    cancelText: t("取消", "Cancel"),
  });

  if (confirmed) {
    try {
      await invoke("clear_telemetry_records");
    } catch (e) {
      console.error("Failed to clear telemetry from backend:", e);
    }
    try {
      const port = appConfig.value?.GuardPort || 1236;
      await fetch(`http://127.0.0.1:${port}/v1/telemetry`, { method: "DELETE" });
    } catch (e) {
      console.error("Failed to reset guard telemetry:", e);
    }
    localStorage.removeItem("antigravity_telemetry_records_v2");
    telemetryRecords.value = [];
    isShowingDemoData.value = false;
    expandedTurnId.value = null;
  }
}

async function loadTelemetryFromBackend() {
  try {
    const records = await invoke<TelemetryRecord[]>("get_telemetry_records");
    if (Array.isArray(records) && records.length > 0) {
      // Re-format timestamps to client locale
      const formatTime = (ts: number) => {
        const d = new Date(ts);
        return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
      };
      records.forEach((r) => {
        if (r.timestamp > 0) {
          r.timeFormatted = formatTime(r.timestamp);
        }
      });
      telemetryRecords.value = records;
      isShowingDemoData.value = false;
      saveTelemetryRecords();
      if (!expandedTurnId.value && records[0]) {
        expandedTurnId.value = records[0].id;
      }
    }
  } catch (e) {
    console.error("Failed to load telemetry from backend:", e);
  }
}

function toggleTurnExpand(id: string) {
  expandedTurnId.value = expandedTurnId.value === id ? null : id;
}

function formatTokK(n: number): string {
  if (n >= 1000000) return (n / 1000000).toFixed(1) + "M";
  if (n >= 1000) return (n / 1000).toFixed(1) + "K";
  return n.toString();
}

function switchToOverview() {
  currentTab.value = "overview";
}

function switchToModel() {
  currentTab.value = "model";
}

function switchToVram() {
  currentTab.value = "vram";
  if (!vramBreakdown.value && !isVramLoading.value) {
    fetchVramBreakdown();
  }
}

function switchToLogs() {
  currentTab.value = "logs";
}

watch(currentTab, async (newTab: TabType) => {
  if (newTab === "overview") {
    if (telemetryRecords.value.length === 0) {
      await loadTelemetryFromBackend();
    }
    if (serviceState.value.status === "Running") {
      await syncTelemetryFromGuard();
    }
  } else if (newTab === "vram") {
    if (!vramBreakdown.value && !isVramLoading.value) {
      fetchVramBreakdown();
    }
  } else if (newTab === "settings") {
    if (!localRepoRoot.value) {
      invoke<string>("get_repo_root_path").then((p) => {
        if (p) localRepoRoot.value = p;
      }).catch(() => {});
    }
  }
});

async function syncTelemetryFromGuard() {
  if (serviceState.value.status !== "Running") return;
  try {
    const port = appConfig.value?.GuardPort || 1236;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 600);
    const resp = await fetch(`http://127.0.0.1:${port}/v1/telemetry`, { signal: controller.signal });
    clearTimeout(timer);
    if (resp.ok) {
      const data = await resp.json();
      if (data.ok && Array.isArray(data.records)) {
        if (data.records.length === 0 && telemetryRecords.value.length === 0) {
          return;
        }
        const formatTime = (ts: number) => {
          const d = new Date(ts);
          return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
        };
        let changed = false;
        const existingMap = new Map(telemetryRecords.value.map((r, i) => [r.id, i]));

        for (const item of data.records) {
          if (existingMap.has(item.id)) {
            // Update existing turn with latest multi-step stats
            const idx = existingMap.get(item.id)!;
            const existing = telemetryRecords.value[idx];
            if (
              existing.outputTokens !== item.outputTokens ||
              existing.thinkingTokens !== item.thinkingTokens ||
              existing.taskType !== item.taskType ||
              existing.steps !== item.steps
            ) {
              existing.thinkingDurationSec = item.thinkingDurationSec;
              existing.thinkingTokens = item.thinkingTokens;
              existing.outputDurationSec = item.outputDurationSec;
              existing.outputTokens = item.outputTokens;
              existing.generationSpeedTokPerSec = item.generationSpeedTokPerSec;
              existing.promptTokens = item.promptTokens;
              existing.totalTokens = item.totalTokens;
              existing.cacheHitRatePct = item.cacheHitRatePct;
              existing.taskType = item.taskType;
              existing.steps = item.steps;
              changed = true;
            }
          } else {
            // Add new turn
            const record: TelemetryRecord = {
              id: item.id,
              turnNumber: telemetryRecords.value.length + 1,
              timestamp: item.timestamp,
              timeFormatted: item.timeFormatted || formatTime(item.timestamp),
              taskType: item.taskType || "常规推理",
              promptPreview: item.promptPreview || "本地大模型对话...",
              ttftMs: item.ttftMs,
              thinkingDurationSec: item.thinkingDurationSec,
              thinkingTokens: item.thinkingTokens,
              outputDurationSec: item.outputDurationSec,
              outputTokens: item.outputTokens,
              generationSpeedTokPerSec: item.generationSpeedTokPerSec,
              promptTokens: item.promptTokens,
              totalTokens: item.totalTokens,
              cacheHitRatePct: item.cacheHitRatePct,
              steps: item.steps,
              status: "success",
            };
            telemetryRecords.value.unshift(record);
            existingMap.set(item.id, 0);
            try {
              await invoke("save_telemetry_record", { record });
            } catch {}
            changed = true;
          }
        }
        if (changed) {
          isShowingDemoData.value = false;
          saveTelemetryRecords();
        }
      }
    }
  } catch {}
}

// VRAM Inspector Computed Properties
const totalAppVramMb = computed(() => {
  return Math.round(
    currentProcessList.value
      .filter((p) => !p.is_critical_system && p.category !== "engine")
      .reduce((sum, p) => sum + p.vram_mb, 0)
  );
});

const userAppsCount = computed(() => {
  return currentProcessList.value.filter(
    (p) => !p.is_critical_system && p.category !== "engine"
  ).length;
});

const filteredProcesses = computed(() => {
  let list = currentProcessList.value;

  if (vramCategoryFilter.value !== "all") {
    if (vramCategoryFilter.value === "user") {
      list = list.filter((p) => !p.is_critical_system && p.category !== "engine");
    } else {
      list = list.filter((p) => p.category === vramCategoryFilter.value);
    }
  }

  if (vramSearchQuery.value.trim()) {
    const q = vramSearchQuery.value.trim().toLowerCase();
    list = list.filter((p) => p.name.toLowerCase().includes(q) || p.pid.toString().includes(q));
  }

  return list;
});

// Actions
async function fetchStatus() {
  try {
    serviceState.value = await invoke<ServiceState>("get_service_state");
    const cfg = await invoke<AppConfig>("get_app_config");
    if (cfg && !cfg.GpuArch) {
      cfg.GpuArch = "sm120a";
    }
    appConfig.value = cfg;
    if (cfg?.ModelPath) {
      refreshModelMetadata(cfg.ModelPath);
    }
    const stats = await invoke<GpuStats | null>("get_gpu_stats");
    if (stats) {
      gpuStats.value = stats;
      checkHardwareDriverOnStartup();
    }
  } catch (e) {
    console.error("Failed to fetch initial status:", e);
    if (!appConfig.value) {
      appConfig.value = {
        EnginePath: "runtime/engine/ninfer-serve.exe",
        ModelPath: "",
        ModelId: "qwen-local",
        GpuArch: "sm120a",
        EnginePort: 1234,
        GuardPort: 1236,
        WebPort: 3080,
        MaxContext: 262144,
        DeviceKV: 28672,
        RecentTokens: 14336,
        SinkTokens: 12288,
        RetrieveTokens: 1536,
        HostKVMiB: 7168,
        ReasoningEffort: "xhigh",
        KVDType: "rk4v4",
        AnswerPenaltyScope: "answer"
      };
      if (appConfig.value.ModelPath) {
        refreshModelMetadata(appConfig.value.ModelPath);
      }
    }
  }
}

async function handleStart() {
  if (!isModelConfigured.value) {
    modelNotConfiguredWarning.value = true;
    currentTab.value = "model";
    return;
  }

  // Pre-flight check: ensure model is compatible with NInfer engine
  if (modelMetadata.value && !modelMetadata.value.is_engine_compatible) {
    currentTab.value = "model";
    return;
  }

  try {
    logs.value = [];
    serviceState.value = {
      status: "Starting",
      data: { step: t(`正在初始化 ${detectedGpuShortName.value || '硬件'} 加速环境...`, `Initializing ${detectedGpuShortName.value || 'hardware'} acceleration environment...`) },
    };
    currentTab.value = "chat";
    await invoke("start_service");
  } catch (e: any) {
    serviceState.value = {
      status: "Error",
      data: { message: e.toString() },
    };
  }
}

async function handleStop() {
  try {
    await invoke("stop_service");
    serviceState.value = { status: "Stopped" };
  } catch (e) {
    console.error("Failed to stop service:", e);
  }
}

async function handlePickModel() {
  try {
    const selected = await invoke<string | null>("pick_model_file");
    if (!selected) return;

    // Strict validation BEFORE modifying or binding to appConfig
    const meta = await invoke<ModelMetadata>("inspect_model_metadata", { path: selected });
    if (!meta.is_engine_compatible || meta.format !== "NINFER") {
      await showAlert({
        title: t("不支持的模型格式：无法绑定", "Unsupported Model Format: Cannot Bind"),
        message: t(
          `❌ 无法绑定此文件！\n\n您选择的文件：${meta.filename || selected}\n检测格式：${meta.format}\n\n${meta.compatibility_notice || '当前底层推理引擎仅支持 .ninfer 格式大模型制品。无法加载 .gguf / safetensors 等其他格式。'}\n\n请重新选择有效的 .ninfer 格式模型制品。`,
          `❌ Cannot bind this file!\n\nFile: ${meta.filename || selected}\nFormat: ${meta.format}\n\n${meta.compatibility_notice || 'NInfer engine only accepts .ninfer model artifacts.'}\n\nPlease select a valid .ninfer model.`
        ),
        type: "danger",
        confirmText: t("重新选择", "Select Again")
      });
      return;
    }

    if (appConfig.value) {
      appConfig.value.ModelPath = selected;
      modelMetadata.value = meta;
      if (meta.is_valid && meta.model_id) {
        appConfig.value.ModelId = meta.model_id;
      }
      await invoke("update_app_config", { config: appConfig.value });
      modelNotConfiguredWarning.value = false;

      await showAlert({
        title: t("模型文件绑定成功", "Model File Bound Successfully"),
        message: t("已成功加载并绑定指定模型制品：\n" + selected, "Successfully loaded and bound model artifact:\n" + selected),
        type: "success",
        confirmText: t("完成", "Done")
      });
    }
  } catch (e: any) {
    await showAlert({
      title: t("选择模型失败", "Failed to Select Model"),
      message: t("选择模型文件时发生异常：\n", "Failed to select model file:\n") + e,
      type: "danger"
    });
  }
}

function clearModelPath() {
  if (appConfig.value) {
    appConfig.value.ModelPath = "";
    modelMetadata.value = null;
  }
}
function setHostKVMiB(val: number) {
  if (appConfig.value) appConfig.value.HostKVMiB = val;
}
function setMaxContext(val: number) {
  if (appConfig.value) appConfig.value.MaxContext = val;
}

async function handleSaveConfig(shouldRestart: boolean = false) {
  if (!appConfig.value) return;
  if (shouldRestart && serviceState.value.status === "Running") {
    setSaveNotice(t("参数已保存，正在重启引擎...", "Saved! Restarting engine..."));
    await handleStop();
    setTimeout(async () => {
      await handleStart();
      setSaveNotice(t("✓ 引擎已重启并成功应用最新配置！", "✓ Engine restarted with latest settings!"));
    }, 1200);
    return;
  }
  if (!isModelConfigured.value) {
    await showAlert({
      title: t("路径未指定", "Invalid Model Path"),
      message: t("请先指定有效的本地模型权重文件路径 (.ninfer)！", "Please specify a valid local model file path (.ninfer)!"),
      type: "warning"
    });
    return;
  }
  if (modelMetadata.value && !modelMetadata.value.is_engine_compatible) {
    await showAlert({
      title: t("无法保存：模型格式不兼容", "Cannot Save: Incompatible Format"),
      message: t(
        `❌ 无法保存配置！\n\n检测到当前模型文件格式为 [${modelMetadata.value.format}]，非 NInfer 引擎所支持的 .ninfer 格式。\n\n${modelMetadata.value.compatibility_notice || '当前底层推理引擎仅支持 .ninfer 格式大模型制品。'}\n\n请修改或重新选择有效的 .ninfer 格式模型制品后再保存。`,
        `❌ Cannot save configuration!\n\nCurrent model format is [${modelMetadata.value.format}], which is incompatible with NInfer engine.\n\nPlease select a valid .ninfer model artifact.`
      ),
      type: "danger",
      confirmText: t("我知道了", "Understood")
    });
    return;
  }
  try {
    await invoke("update_app_config", { config: appConfig.value });
    modelNotConfiguredWarning.value = false;

    if (serviceState.value.status === "Running") {
      const doRestart = await showConfirm({
        title: t("重启模型引擎以生效", "Restart Engine to Apply Changes"),
        message: t(
          "模型参数已成功保存！\n\n当前大模型引擎正在运行中。新设置的 KV 显存池容量与上下文参数需要重新启动引擎方可生效。\n\n是否立即为您重启引擎？",
          "Configuration saved successfully!\n\nThe engine is currently running. New KV cache pool and context parameters require an engine restart.\n\nWould you like to restart the engine now?"
        ),
        type: "warning",
        confirmText: t("立即重启引擎", "Restart Engine Now"),
        cancelText: t("稍后手动重启", "Restart Later")
      });
      if (doRestart) {
        await handleStop();
        setTimeout(async () => {
          await handleStart();
        }, 1200);
      }
    } else {
      setSaveNotice(t("✓ 配置已保存至 local.json，下次启动将自动载入", "✓ Saved to local.json, will apply on next launch"));
    }
  } catch (e: any) {
    await showAlert({
      title: t("保存配置失败", "Failed to Save Configuration"),
      message: t("保存配置时遇到错误：\n", "Failed to save configuration:\n") + e,
      type: "danger"
    });
  }
}


function hasLog(pattern: string): boolean {
  return logs.value.some((l) => l.message.toLowerCase().includes(pattern.toLowerCase()));
}

const startupProgress = computed(() => {
  if (serviceState.value.status === "Running") return 100;
  if (serviceState.value.status !== "Starting") return 0;

  if (hasLog("dsh web:") || hasLog("DSH Agent")) return 96;
  if (hasLog("cache_guard listening")) return 90;
  if (hasLog("warmup complete") || hasLog("engine ready")) return 82;
  if (hasLog("CUDA graphs ready") || hasLog("pinning host")) return 70;
  if (hasLog("weights ready")) return 60;

  const loadingLog = logs.value.slice().reverse().find((l) => l.message.toLowerCase().includes("loading weights"));
  if (loadingLog) {
    const match = loadingLog.message.match(/loading weights\s+([\d\.]+)%/i);
    if (match) {
      const pct = parseFloat(match[1]);
      return Math.min(58, Math.max(25, Math.round(20 + pct * 0.38)));
    }
    return 35;
  }

  if (hasLog("launching engine") || hasLog("starting engine")) return 20;

  return 12;
});

const startupStage = computed(() => {
  const p = startupProgress.value;
  if (p < 25) return 1;
  if (p < 60) return 2;
  if (p < 85) return 3;
  return 4;
});

const currentStartupStepDescription = computed(() => {
  const stage = startupStage.value;
  if (stage === 1) return t("正在核验显存空间与准备硬件加速环境...", "Initializing Hardware & Port Bindings...");
  if (stage === 2) return t("正在高速映射 9.71 GB 模型权重至 GPU 显存...", "Streaming 9.71 GB Model Weights to VRAM...");
  if (stage === 3) return t("正在预热 CUDA 静态计算图与 7GB Host KV 内存...", "Warming up CUDA Graph & Allocating 7GB Host KV...");
  return t("正在挂载 Cache Guard 守卫与 DSH 智能体服务...", "Mounting Cache Guard & DSH Agent Workspace...");
});

const startupRecentLogs = computed(() => {
  return logs.value.slice(-4);
});

function clearLogs() {
  logs.value = [];
}

async function fetchVramBreakdown() {
  if (isVramLoading.value) return;
  isVramLoading.value = true;
  try {
    const res = await invoke<VramBreakdown>("get_gpu_process_breakdown");
    vramBreakdown.value = res;
    if (res && (!gpuStats.value || res.used_mb > 0)) {
      gpuStats.value = {
        total_mb: res.total_mb,
        used_mb: res.used_mb,
        free_mb: res.free_mb,
        gpu_util: gpuStats.value?.gpu_util || 0,
        temp_c: gpuStats.value?.temp_c || 0,
        gpu_name: gpuStats.value?.gpu_name || res.topology?.dgpu_name || "",
        compute_cap: gpuStats.value?.compute_cap || "",
        driver_version: gpuStats.value?.driver_version || res.topology?.dgpu_driver || "",
      };
    }
  } catch (e) {
    console.error("Failed to query VRAM breakdown:", e);
  } finally {
    isVramLoading.value = false;
  }
}

async function handleKillProcess(proc: ProcessVramInfo) {
  if (proc.is_critical_system) {
    await showAlert({
      title: t("系统核心组件保护", "Protected System Component"),
      message: t(
        `应用「${proc.name} (PID: ${proc.pid})」属于 Windows 核心系统组件或当前工作台界面，强制终止将导致系统异常或工作台闪退，已被系统保护锁定！`,
        `"${proc.name} (PID: ${proc.pid})" is a Windows core component or the current studio interface. Force termination is blocked for stability!`
      ),
      type: "warning",
      confirmText: t("我知道了", "Understood")
    });
    return;
  }
  const confirmed = await showConfirm({
    title: t("释放专用显存确认", "Terminate Process to Free VRAM"),
    message: t(
      `确定要彻底关闭应用「${proc.name} (PID: ${proc.pid})」吗？\n\n该操作将完全关闭该应用的所有工作进程树，预计可立即释放约 ${proc.vram_mb} MB 显存。`,
      `Are you sure you want to terminate "${proc.name} (PID: ${proc.pid})"?\n\nThis will completely close its process tree, instantly freeing approx ${proc.vram_mb} MB dedicated VRAM.`
    ),
    type: "danger",
    confirmText: t("立即关闭并释放显存", "Terminate & Free VRAM"),
    cancelText: t("取消", "Cancel")
  });
  if (!confirmed) return;

  terminatingPid.value = proc.pid;
  try {
    await invoke("kill_process_by_pid", { pid: proc.pid, name: proc.name });

    // Optimistic UI update: immediately remove from current list and adjust used_mb
    if (vramBreakdown.value) {
      const freedMb = proc.vram_mb;
      vramBreakdown.value.processes = vramBreakdown.value.processes.filter(
        (p) => p.pid !== proc.pid && p.name.toLowerCase() !== proc.name.toLowerCase()
      );
      vramBreakdown.value.used_mb = Math.max(0, vramBreakdown.value.used_mb - Math.round(freedMb));
      vramBreakdown.value.free_mb = Math.min(vramBreakdown.value.total_mb, vramBreakdown.value.free_mb + Math.round(freedMb));
    }

    copySuccessNotice.value = t(`已成功关闭应用「${proc.name}」，显存已释放！`, `Successfully closed "${proc.name}", dedicated VRAM freed!`);
    setTimeout(() => {
      copySuccessNotice.value = "";
    }, 3000);

    // Re-sample hardware counters after 1.5s for WDDM driver to refresh
    setTimeout(() => {
      fetchVramBreakdown();
    }, 1500);
  } catch (e: any) {
    await showAlert({
      title: t("关闭应用失败", "Failed to Terminate Process"),
      message: t("关闭进程时遇到系统拒绝或权限不足：\n", "Encountered permission error or denial:\n") + e,
      type: "danger"
    });
  } finally {
    terminatingPid.value = null;
  }
}

async function handleMigrateGpu(proc: ProcessVramInfo, target: "igpu" | "dgpu") {
  if (proc.is_critical_system) {
    await showAlert({
      title: t("系统保护组件", "Protected Component"),
      message: t(
        `应用「${proc.name} (PID: ${proc.pid})」属于 Windows 核心系统组件或当前工作台，已被保护锁定，禁止更改调度！`,
        `"${proc.name} (PID: ${proc.pid})" is a Windows core component or the current studio. Migration is locked.`
      ),
      type: "warning"
    });
    return;
  }

  const isToIgpu = target === "igpu";
  const title = isToIgpu
    ? t("迁移到核显确认", "Migrate to iGPU Confirmation")
    : t("迁移到独显确认", "Migrate to dGPU Confirmation");

  const message = isToIgpu
    ? t(
        `确定要将应用「${proc.name} (PID: ${proc.pid})」调度至【核显 (Intel Graphics)】运行吗？\n\n将自动写入首选项并重启该应用。`,
        `Switch "${proc.name} (PID: ${proc.pid})" to integrated GPU (Intel Graphics)?\n\nWill automatically configure preferences and restart the application.`
      )
    : t(
        `确定要将应用「${proc.name} (PID: ${proc.pid})」调度回【独显 (${detectedGpuShortName.value || '高性能'})】模式吗？\n\n将自动写入首选项并重启该应用。`,
        `Switch "${proc.name} (PID: ${proc.pid})" back to ${detectedGpuShortName.value || 'dedicated'} GPU?\n\nWill automatically configure preferences and restart the application.`
      );

  const confirmed = await showConfirm({
    title,
    message,
    type: isToIgpu ? "info" : "warning",
    confirmText: isToIgpu ? t("迁移到核显", "Migrate to iGPU") : t("迁移到独显", "Migrate to dGPU"),
    cancelText: t("取消", "Cancel")
  });
  if (!confirmed) return;

  isMigratingPid.value = proc.pid;
  try {
    const msg = await invoke<string>("migrate_process_gpu", {
      pid: proc.pid,
      exePath: proc.exe_path || null,
      target,
      restart: true
    });

    copySuccessNotice.value = msg;
    setTimeout(() => {
      copySuccessNotice.value = "";
    }, 4000);

    setTimeout(() => {
      fetchVramBreakdown();
    }, 1800);
  } catch (e: any) {
    await showAlert({
      title: t("显卡调度失败", "GPU Migration Failed"),
      message: t("调度或重启进程时遇到错误：\n", "Encountered error during migration:\n") + e,
      type: "danger"
    });
  } finally {
    isMigratingPid.value = null;
  }
}

async function handleResetPreference(proc: ProcessVramInfo) {
  if (!proc.exe_path) {
    await showAlert({
      title: t("无法定位路径", "Path Not Found"),
      message: t("未能获取此进程的可执行文件路径，无法重置注册表首选项。", "Executable path not found, cannot reset registry preference."),
      type: "warning"
    });
    return;
  }

  try {
    await invoke("set_process_gpu_preference", {
      exePath: proc.exe_path,
      target: "default"
    });
    copySuccessNotice.value = t(`已恢复应用「${proc.name}」为系统默认图形首选项！`, `Reset "${proc.name}" to default GPU preference!`);
    setTimeout(() => {
      copySuccessNotice.value = "";
    }, 3000);
    setTimeout(() => {
      fetchVramBreakdown();
    }, 1000);
  } catch (e: any) {
    await showAlert({
      title: t("重置首选项失败", "Reset Failed"),
      message: String(e),
      type: "danger"
    });
  }
}

async function handleBatchMigrateToIgpu() {
  const confirmed = await showConfirm({
    title: t("⚡ 一键将后台应用迁移至核显", "Batch Migrate Background Apps to iGPU"),
    message: t(
      "该操作将扫描独显上运行的所有第三方后台常驻应用（如 QQ、微信、浏览器、音乐播放器等）：\n\n1. 永久将它们设置为优先使用节能核显（Intel Graphics）\n2. 依次自动平滑重启这些应用\n3. 彻底释放独显物理显存空间，消除模型 OOM 隐患！\n\n系统核心组件（DWM、桌面等）与模型引擎受安全白名单保护，不会受到任何影响。\n\n是否立即执行批量优化？",
      `This will scan all third-party background apps running on ${detectedGpuShortName.value || 'dedicated GPU'} (QQ, WeChat, browsers, media players):\n\n1. Set them permanently to Intel Graphics\n2. Automatically restart them smoothly\n3. Free up dedicated VRAM to prevent model OOM!\n\nSystem components (DWM, Explorer) and AI Engine are protected.\n\nProceed with batch migration?`
    ),
    type: "info",
    confirmText: t("⚡ 立即一键优化", "Optimize Now"),
    cancelText: t("取消", "Cancel")
  });
  if (!confirmed) return;

  batchMigrating.value = true;
  try {
    const migrated = await invoke<string[]>("batch_migrate_to_igpu", {
      categories: ["im", "media", "browser", "other"]
    });

    if (migrated.length === 0) {
      await showAlert({
        title: t("无需迁移", "No Apps to Migrate"),
        message: t("当前没有运行在独显上的第三方后台软件，独显环境非常干净！", "No non-system background apps found on dedicated GPU. Environment is optimal!"),
        type: "success"
      });
    } else {
      copySuccessNotice.value = t(
        `一键优化完成！已将 ${migrated.length} 个应用 (${migrated.join(", ")}) 成功调度至核显并平滑重启！`,
        `Batch migration complete! Successfully migrated ${migrated.length} apps (${migrated.join(", ")}) to iGPU!`
      );
      setTimeout(() => {
        copySuccessNotice.value = "";
      }, 5000);
    }

    setTimeout(() => {
      fetchVramBreakdown();
    }, 2000);
  } catch (e: any) {
    await showAlert({
      title: t("批量优化失败", "Batch Migration Failed"),
      message: String(e),
      type: "danger"
    });
  } finally {
    batchMigrating.value = false;
  }
}

async function handleBatchMigrateToDgpu() {
  const confirmed = await showConfirm({
    title: t("🖥️ 一键切换到独显", "Batch Migrate Apps to dGPU"),
    message: t(
      `确定要将当前核显上的第三方应用程序批量切换至【独显 (${detectedGpuShortName.value || '高性能'})】模式吗？\n\n将自动写入首选项并重启该应用。`,
      `Switch non-system apps on integrated GPU back to ${detectedGpuShortName.value || 'dedicated GPU'}?\n\nWill automatically configure preferences and restart the applications.`
    ),
    type: "warning",
    confirmText: t("🖥️ 一键切换到独显", "Switch to dGPU"),
    cancelText: t("取消", "Cancel")
  });
  if (!confirmed) return;

  batchMigrating.value = true;
  try {
    const migrated = await invoke<string[]>("batch_migrate_to_dgpu", {
      categories: ["im", "media", "browser", "other"]
    });

    if (migrated.length === 0) {
      await showAlert({
        title: t("无需切换", "No Apps to Switch"),
        message: t("当前核显列表中没有可迁移的第三方应用！", "No non-system apps found on integrated GPU!"),
        type: "info"
      });
    } else {
      copySuccessNotice.value = t(
        `一键切换完成！已将 ${migrated.length} 个应用 (${migrated.join(", ")}) 成功切换至独显并平滑重启！`,
        `Successfully switched ${migrated.length} apps (${migrated.join(", ")}) to dGPU!`
      );
      setTimeout(() => {
        copySuccessNotice.value = "";
      }, 5000);
    }

    setTimeout(() => {
      fetchVramBreakdown();
    }, 2000);
  } catch (e: any) {
    await showAlert({
      title: t("批量切换失败", "Batch Switch Failed"),
      message: String(e),
      type: "danger"
    });
  } finally {
    batchMigrating.value = false;
  }
}

async function copyVramReport() {
  if (!vramBreakdown.value) return;
  const b = vramBreakdown.value;
  const lines: string[] = [];
  lines.push(t(`# ${detectedGpuBadge.value || '独显'} 显存占用管理报告`, `# ${detectedGpuBadge.value || 'Dedicated GPU'} VRAM Report`));
  lines.push(`- ${t("采样时间", "Timestamp")}: ${new Date(b.timestamp * 1000).toLocaleString()}`);
  lines.push(`- ${t("总物理显存", "Total VRAM")}: ${(b.total_mb / 1024).toFixed(2)} GB (${b.total_mb} MB)`);
  lines.push(`- ${t("当前已占用", "Currently Used")}: ${(b.used_mb / 1024).toFixed(2)} GB (${b.used_mb} MB)`);
  lines.push(`- ${t("剩余可用空间", "Free Available")}: ${(b.free_mb / 1024).toFixed(2)} GB (${b.free_mb} MB)`);
  lines.push(`\n## ${t("进程占用排行", "Process VRAM Ranking")}:`);
  lines.push(`| ${t("排名", "Rank")} | ${t("进程名", "Process")} | PID | ${t("类别", "Category")} | ${t("专用显存 (MB)", "VRAM (MB)")} | ${t("占 12GB 比例", "Ratio (12GB)")} |`);
  lines.push(`|---|---|---|---|---|---|`);
  b.processes.slice(0, 20).forEach((p, idx) => {
    lines.push(
      `| #${idx + 1} | ${p.name} | ${p.pid} | ${getCategoryLabel(p.category)} | ${p.vram_mb} MB | ${(
        (p.vram_mb / b.total_mb) *
        100
      ).toFixed(1)}% |`
    );
  });

  try {
    await navigator.clipboard.writeText(lines.join("\n"));
    copySuccessNotice.value = t("已复制诊断报告至剪贴板！", "Copied diagnostic report to clipboard!");
    setTimeout(() => {
      copySuccessNotice.value = "";
    }, 2500);
  } catch (e) {
    await showAlert({
      title: t("复制失败", "Copy Failed"),
      message: t("无法写入系统剪贴板：\n", "Unable to write to clipboard:\n") + e,
      type: "warning"
    });
  }
}

function getCategoryLabel(cat: string): string {
  switch (cat) {
    case "client":
      return t("当前工作台", "Client Studio");
    case "engine":
      return t("AI 推理引擎", "AI Engine");
    case "browser":
      return t("网页浏览器", "Web Browser");
    case "chat":
      return t("即时通讯", "IM & Chat");
    case "media":
      return t("视频/流媒体", "Media / Video");
    case "ide":
      return t("开发集成环境", "IDE / Code");
    case "system":
      return t("系统组件", "System Component");
    case "driver":
      return t("硬件/驱动组件", "Driver / Hardware");
    default:
      return t("其他应用程序", "Other Apps");
  }
}

function getCategoryBadgeClass(cat: string): string {
  switch (cat) {
    case "client":
      return "cat-badge-client";
    case "engine":
      return "cat-badge-engine";
    case "browser":
      return "cat-badge-browser";
    case "chat":
      return "cat-badge-chat";
    case "media":
      return "cat-badge-media";
    case "ide":
      return "cat-badge-ide";
    case "system":
      return "cat-badge-system";
    case "driver":
      return "cat-badge-driver";
    default:
      return "cat-badge-other";
  }
}

// ==============================================================
// Mutually Exclusive Sidebar Accordion (DSH <-> Antigravity Studio)
// "这个展开的时候图2就收起来，反之"
// ==============================================================
function postMessageToDsh(message: any) {
  const iframe = document.querySelector<HTMLIFrameElement>(".dsh-seamless-frame");
  if (iframe && iframe.contentWindow) {
    iframe.contentWindow.postMessage(message, "*");
  }
}

function toggleOuterSidebar() {
  const nextCollapsed = !isSidebarCollapsed.value;
  isSidebarCollapsed.value = nextCollapsed;
  if (currentTab.value === "chat" && serviceState.value.status === "Running") {
    // When outer is collapsed (nextCollapsed = true), DSH expands (expand: true)
    // When outer is expanded (nextCollapsed = false), DSH collapses (expand: false)
    postMessageToDsh({
      type: "set-dsh-sidebar",
      expand: nextCollapsed,
    });
  }
}

function handleDshMessage(event: MessageEvent) {
  if (event.data && typeof event.data === "object" && event.data.type === "dsh-sidebar-state") {
    if (currentTab.value === "chat" && serviceState.value.status === "Running") {
      // Mutually exclusive seesaw:
      // When DSH sidebar is expanded (true), outer sidebar collapses (true).
      // When DSH sidebar is collapsed (false), outer sidebar expands (false).
      isSidebarCollapsed.value = !!event.data.expanded;
    }
  }
}

function ensureIframeFocus() {
  const iframe = document.querySelector<HTMLIFrameElement>(".dsh-seamless-frame");
  if (iframe) {
    try {
      iframe.focus();
      iframe.contentWindow?.focus();
      iframe.contentWindow?.postMessage({ type: "focus-composer" }, "*");
    } catch (e) {}
  }
}

async function openInExternalBrowser() {
  const url = serviceState.value.data?.web_url || "http://127.0.0.1:3080/";
  try {
    await invoke("open_in_browser", { url });
  } catch (e) {
    window.open(url, "_blank");
  }
}

function openModelDownloadUrl() {
  const url = "https://huggingface.co/fyb1214/Huihui-Qwen3.8-27B-Abliterated-Swift-1.5-GSQ-RCO-NInfer/tree/main";
  try {
    invoke("open_in_browser", { url });
  } catch (e) {
    window.open(url, "_blank");
  }
}

function onIframeLoad() {
  setTimeout(() => {
    postMessageToDsh({ type: "query-dsh-sidebar" });
    ensureIframeFocus();
  }, 300);
  setTimeout(() => {
    postMessageToDsh({ type: "query-dsh-sidebar" });
    ensureIframeFocus();
  }, 1000);
}

function switchToChat() {
  currentTab.value = "chat";
  if (serviceState.value.status === "Running") {
    nextTick(() => {
      postMessageToDsh({ type: "query-dsh-sidebar" });
      ensureIframeFocus();
    });
  }
}

let sysThemeMediaQuery: MediaQueryList | null = null;
const handleSysThemeChange = (e: MediaQueryListEvent) => {
  isSystemDark.value = e.matches;
  if (currentTheme.value === "system") {
    applyTheme("system");
  }
};

onMounted(() => {
  window.addEventListener("keydown", handleModalKeydown);
  window.addEventListener("message", handleDshMessage);

  applyTheme(currentTheme.value);
  applyColorScheme(currentColorScheme.value);
  invoke<string>("get_repo_root_path")
    .then((p) => {
      if (p) localRepoRoot.value = p;
    })
    .catch(() => {});

  if (typeof window !== "undefined" && window.matchMedia) {
    sysThemeMediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
    isSystemDark.value = sysThemeMediaQuery.matches;
    sysThemeMediaQuery.addEventListener("change", handleSysThemeChange);
  }

  // Parallel non-blocking initialization
  fetchStatus().then(() => {
    if (window.location.hash.includes("overview")) {
      currentTab.value = "overview";
    } else if (window.location.hash.includes("model") || window.location.search.includes("tab=model")) {
      currentTab.value = "model";
    } else if (window.location.hash.includes("vram") || window.location.search.includes("tab=vram")) {
      currentTab.value = "vram";
    } else if (!isModelConfigured.value) {
      modelNotConfiguredWarning.value = true;
      currentTab.value = "model";
    } else {
      currentTab.value = "chat";
    }
  });
  loadTelemetryFromBackend();

  // Listen for gpu-stats emitted by Rust background thread
  listen<GpuStats>("gpu-stats", (event) => {
    gpuStats.value = event.payload;
  });

  // Polling status & sync telemetry (bypassed during active fast scroll to eliminate stutter)
  setInterval(async () => {
    if (isViewportScrolling.value) return;
    try {
      const state = await invoke<ServiceState>("get_service_state");
      serviceState.value = state;
      if (currentTab.value === "overview" && state.status === "Running") {
        await syncTelemetryFromGuard();
      }
    } catch (e) {}
  }, 3000);

  // Listen for service state changed
  listen<ServiceState>("service-state-changed", (event) => {
    serviceState.value = event.payload;
    if (event.payload.status === "Running") {
      iframeKey.value++;
      currentTab.value = "chat";
    }
  });

  // Listen for logs
  listen<LogEntry>("log-message", (event) => {
    logs.value.push(event.payload);
    if (logs.value.length > 1000) logs.value.shift();
    if (autoScroll.value) {
      nextTick(() => {
        if (logContainer.value) {
          logContainer.value.scrollTop = logContainer.value.scrollHeight;
        }
      });
    }
  });
});

onUnmounted(() => {
  window.removeEventListener("keydown", handleModalKeydown);
  window.removeEventListener("message", handleDshMessage);
  if (sysThemeMediaQuery) {
    sysThemeMediaQuery.removeEventListener("change", handleSysThemeChange);
  }
});
</script>

<template>
  <div
    class="studio-app"
    :class="{ 'sidebar-collapsed': isSidebarCollapsed }"
    :data-theme="effectiveTheme"
    :data-color-scheme="currentColorScheme"
  >
    <!-- LEFT SIDEBAR (Reference design: pure white, clean icons, subtle collapse arrow) -->
    <aside class="sidebar">
      <div class="sidebar-top">
        <div class="brand-box" v-if="!isSidebarCollapsed">
          <div class="brand-logo-glow">
            <svg class="brand-logo-svg" viewBox="0 0 100 100" fill="none">
              <!-- Pmod 专属立体算力晶体 P -->
              <path d="M26 24C26 21.79 27.79 20 30 20H48C62.36 20 74 31.64 74 46C74 60.36 62.36 72 48 72H38V76C38 78.21 36.21 80 34 80H30C27.79 80 26 78.21 26 76V24Z" fill="url(#pFlowSide)"/>
              <path d="M38 32H48C55.73 32 62 38.27 62 46C62 53.73 55.73 60 48 60H38V32Z" fill="#090d16"/>
              <path d="M50 38L57 46L50 54L43 46L50 38Z" fill="#ffffff"/>
              <path d="M26 84C40 88 60 88 74 84" stroke="#38bdf8" stroke-width="4" stroke-linecap="round"/>
              <defs>
                <linearGradient id="pFlowSide" x1="26" y1="20" x2="74" y2="80" gradientUnits="userSpaceOnUse">
                  <stop offset="0%" stop-color="#38bdf8"/>
                  <stop offset="50%" stop-color="#6366f1"/>
                  <stop offset="100%" stop-color="#a855f7"/>
                </linearGradient>
              </defs>
            </svg>
          </div>
          <div class="brand-text">
            <span class="brand-name">Pmod</span>
          </div>
        </div>
        <div class="brand-logo-glow brand-logo-mini" v-else>
          <svg class="brand-logo-svg" viewBox="0 0 100 100" fill="none">
            <path d="M26 24C26 21.79 27.79 20 30 20H48C62.36 20 74 31.64 74 46C74 60.36 62.36 72 48 72H38V76C38 78.21 36.21 80 34 80H30C27.79 80 26 78.21 26 76V24Z" fill="url(#pFlowSideMini)"/>
            <path d="M38 32H48C55.73 32 62 38.27 62 46C62 53.73 55.73 60 48 60H38V32Z" fill="#090d16"/>
            <path d="M50 38L57 46L50 54L43 46L50 38Z" fill="#ffffff"/>
            <path d="M26 84C40 88 60 88 74 84" stroke="#38bdf8" stroke-width="4" stroke-linecap="round"/>
            <defs>
              <linearGradient id="pFlowSideMini" x1="26" y1="20" x2="74" y2="80" gradientUnits="userSpaceOnUse">
                <stop offset="0%" stop-color="#38bdf8"/>
                <stop offset="50%" stop-color="#6366f1"/>
                <stop offset="100%" stop-color="#a855f7"/>
              </linearGradient>
            </defs>
          </svg>
        </div>

        <button
          class="btn-collapse"
          @click="toggleOuterSidebar"
          :title="isSidebarCollapsed ? t('展开菜单', 'Expand sidebar') : t('收起菜单', 'Collapse sidebar')"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="collapse-arrow" :class="{ 'arrow-collapsed': isSidebarCollapsed }">
            <polyline points="15 18 9 12 15 6"></polyline>
          </svg>
        </button>
      </div>

      <!-- Navigation Menu -->
      <nav class="sidebar-menu">
        <!-- 1. 对话 (Default Landing) -->
        <button
          class="menu-item"
          :class="{ 'menu-item-active': currentTab === 'chat' }"
          @click="switchToChat"
          :title="t('对话', 'Chat')"
        >
          <div class="menu-icon-box">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="menu-svg">
              <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path>
            </svg>
          </div>
          <span class="menu-label" v-if="!isSidebarCollapsed">{{ t('对话', 'Chat') }}</span>
          <span v-if="serviceState.status === 'Running' && !isSidebarCollapsed" class="menu-badge-online">{{ t('在线', 'Online') }}</span>
        </button>

        <!-- 2. 推理统计 -->
        <button
          class="menu-item"
          :class="{ 'menu-item-active': currentTab === 'overview' }"
          @click="switchToOverview"
          :title="t('推理统计', 'Inference Telemetry')"
        >
          <div class="menu-icon-box">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="menu-svg">
              <path d="M18 20V10"></path>
              <path d="M12 20V4"></path>
              <path d="M6 20v-6"></path>
            </svg>
          </div>
          <span class="menu-label" v-if="!isSidebarCollapsed">{{ t('推理统计', 'Telemetry') }}</span>
        </button>

        <!-- 3. 模型参数 -->
        <button
          class="menu-item"
          :class="{ 'menu-item-active': currentTab === 'model' }"
          @click="switchToModel"
          :title="t('模型', 'Model')"
        >
          <div class="menu-icon-box">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="menu-svg">
              <rect x="4" y="4" width="16" height="16" rx="2"></rect>
              <rect x="9" y="9" width="6" height="6"></rect>
              <line x1="9" y1="1" x2="9" y2="4"></line>
              <line x1="15" y1="1" x2="15" y2="4"></line>
              <line x1="9" y1="20" x2="9" y2="23"></line>
              <line x1="15" y1="20" x2="15" y2="23"></line>
              <line x1="20" y1="9" x2="23" y2="9"></line>
              <line x1="20" y1="14" x2="23" y2="14"></line>
              <line x1="1" y1="9" x2="4" y2="9"></line>
              <line x1="1" y1="14" x2="4" y2="14"></line>
            </svg>
          </div>
          <span class="menu-label" v-if="!isSidebarCollapsed">{{ t('模型', 'Model') }}</span>
        </button>

        <!-- 4. 显存管理 -->
        <button
          class="menu-item"
          :class="{ 'menu-item-active': currentTab === 'vram' }"
          @click="switchToVram"
          :title="t('显存管理', 'VRAM Management')"
        >
          <div class="menu-icon-box">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="menu-svg">
              <line x1="18" y1="20" x2="18" y2="10"></line>
              <line x1="12" y1="20" x2="12" y2="4"></line>
              <line x1="6" y1="20" x2="6" y2="14"></line>
            </svg>
          </div>
          <span class="menu-label" v-if="!isSidebarCollapsed">{{ t('显存管理', 'VRAM Management') }}</span>
          <span v-if="gpuStats && !isSidebarCollapsed" class="menu-badge-vram">
            {{ (gpuStats.used_mb / 1024).toFixed(1) }}G
          </span>
        </button>

        <!-- 5. 运行日志 -->
        <button
          class="menu-item"
          :class="{ 'menu-item-active': currentTab === 'logs' }"
          @click="switchToLogs"
          :title="t('日志', 'Logs')"
        >
          <div class="menu-icon-box">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="menu-svg">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
              <polyline points="14 2 14 8 20 8"></polyline>
              <line x1="16" y1="13" x2="8" y2="13"></line>
              <line x1="16" y1="17" x2="8" y2="17"></line>
            </svg>
          </div>
          <span class="menu-label" v-if="!isSidebarCollapsed">{{ t('日志', 'Logs') }}</span>
          <span v-if="logs.length > 0 && !isSidebarCollapsed" class="menu-badge-count">{{ logs.length }}</span>
        </button>
      </nav>

      <!-- Sidebar Bottom Telemetry Status -->
      <div class="sidebar-bottom">
        <!-- Settings Nav Button (Matching Image 1) -->
        <button
          class="menu-item menu-item-settings"
          :class="{ 'menu-item-active': currentTab === 'settings' }"
          @click="switchToSettings"
          :title="t('设置', 'Settings')"
        >
          <div class="menu-icon-box">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="menu-svg">
              <circle cx="12" cy="12" r="3"></circle>
              <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
            </svg>
          </div>
          <span class="menu-label" v-if="!isSidebarCollapsed">{{ t('设置', 'Settings') }}</span>
        </button>

        <div class="sidebar-status-card" v-if="!isSidebarCollapsed">
          <div class="status-card-header">
            <span class="status-chip-dot" :class="{
              'dot-gray': serviceState.status === 'Stopped',
              'dot-yellow': serviceState.status === 'Starting',
              'dot-green': serviceState.status === 'Running',
              'dot-red': serviceState.status === 'Error'
            }"></span>
            <span class="status-chip-text">
              {{
                serviceState.status === "Stopped" ? t("引擎待命", "Engine Standby") :
                serviceState.status === "Starting" ? t("正在启动...", "Starting...") :
                serviceState.status === "Running" ? t(`在线就绪 (${appConfig?.WebPort || 3080})`, `Ready (${appConfig?.WebPort || 3080})`) : t("异常报错", "Error")
              }}
            </span>
          </div>
          <div class="status-card-vram-row" v-if="gpuStats">
            <span class="vram-mini-title">{{ (detectedGpuShortName || t('独显', 'VRAM')) + ' ' + t('显存:', 'VRAM:') }}</span>
            <span class="vram-mini-val">{{ (gpuStats.used_mb / 1024).toFixed(1) }} / 12G</span>
          </div>
          <div class="status-vram-bar-track">
            <div
              class="status-vram-bar-fill"
              :style="{ width: vramPercent + '%', backgroundColor: vramStatusColor }"
            ></div>
          </div>
        </div>
      </div>
    </aside>

    <!-- RIGHT CONTENT VIEWPORT (Soft lilac/blue gradient background) -->
    <main class="main-viewport">
      <!-- Top Title & Action Bar -->
      <header class="viewport-header">
        <div class="header-left-title">
          <h1 class="header-main-title">
            {{
              currentTab === 'chat' ? t('对话', 'Chat') :
              currentTab === 'overview' ? t('推理性能与会话统计', 'Inference & Conversation Telemetry') :
              currentTab === 'vram' ? t('显存管理', 'VRAM Management') :
              currentTab === 'model' ? t('模型与引擎参数', 'Model Settings') :
              currentTab === 'logs' ? t('运行日志', 'Runtime Logs') : t('系统设置', 'Settings')
            }}
          </h1>

          <!-- Real-time Model & Engine Status Badges -->
          <div class="header-sub-badges">
            <div
              class="engine-status-badge"
              :class="{
                'status-badge-running': serviceState.status === 'Running',
                'status-badge-starting': serviceState.status === 'Starting',
                'status-badge-stopped': serviceState.status === 'Stopped',
                'status-badge-error': serviceState.status === 'Error'
              }"
            >
              <span class="engine-badge-dot"></span>
              <span>
                {{
                  serviceState.status === 'Running' ? t(`模型已启动 (${appConfig?.WebPort || 3080})`, `Model Running (${appConfig?.WebPort || 3080})`) :
                  serviceState.status === 'Starting' ? t('模型启动中...', 'Starting Model...') :
                  serviceState.status === 'Stopped' ? t('模型未启动', 'Model Stopped') : t('引擎异常', 'Engine Error')
                }}
              </span>
            </div>

            <div
              class="header-model-pill"
              :class="{ 'model-pill-missing': !isModelConfigured }"
              @click="currentTab = 'model'"
              :title="isModelConfigured ? appConfig?.ModelPath : t('点击配置本地模型文件', 'Click to configure model file')"
            >
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="model-pill-svg">
                <rect x="4" y="4" width="16" height="16" rx="2"></rect>
                <rect x="9" y="9" width="6" height="6"></rect>
              </svg>
              <span>{{ isModelConfigured ? t('本地大模型 (已配置)', 'Local Model (Configured)') : t('⚠️ 未配置模型 (点击配置)', '⚠️ No Model Configured (Click to set)') }}</span>
            </div>
          </div>
        </div>

        <div class="header-right-actions">
          <!-- VRAM Quick Pill -->
          <div
            class="header-pill-vram"
            @click="currentTab = 'vram'; fetchVramBreakdown();"
            :title="t('点击切换到显存透视', 'Click to switch to VRAM Inspector')"
          >
            <span class="pill-dot" :style="{ backgroundColor: vramStatusColor }"></span>
            <span>{{ t('独显占用: ', 'VRAM: ') }}<strong>{{ gpuStats ? (gpuStats.used_mb / 1024).toFixed(1) : t('检测中', 'Checking') }}</strong> / 11.9 GB</span>
            <span class="pill-tag">{{ t('明细', 'Details') }}</span>
          </div>

          <!-- Language Switcher Button -->
          <button
            class="btn-header-secondary"
            @click="toggleLanguage"
            :title="t('切换语言 (中 / EN)', 'Switch Language (ZH / EN)')"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-sub-svg">
              <circle cx="12" cy="12" r="10"></circle>
              <line x1="2" y1="12" x2="22" y2="12"></line>
              <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path>
            </svg>
            <span>{{ currentLang === 'zh' ? 'English' : '简体中文' }}</span>
          </button>

          <!-- Model Switch Button -->
          <button
            class="btn-header-secondary"
            :disabled="serviceState.status === 'Starting'"
            @click="handlePickModel"
            :title="t('切换 .ninfer 权重', 'Change .ninfer weights')"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-sub-svg">
              <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
            </svg>
            <span>{{ t('更换模型', 'Change Model') }}</span>
          </button>

          <!-- Refresh Workspace Button (Only in running chat) -->
          <button
            v-if="serviceState.status === 'Running' && currentTab === 'chat'"
            class="btn-header-secondary"
            @click="iframeKey++"
            :title="t('重置/刷新对话工作台', 'Reset/Refresh Agent Workspace')"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-sub-svg">
              <path d="M23 4v6h-6"></path>
              <path d="M1 20v-6h6"></path>
              <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
            </svg>
            <span>{{ t('刷新界面', 'Refresh') }}</span>
          </button>

          <!-- Open in External Browser Button (Only in running chat) -->
          <button
            v-if="serviceState.status === 'Running' && currentTab === 'chat'"
            class="btn-header-secondary"
            @click="openInExternalBrowser"
            :title="t('在独立浏览器中打开完整对话工作台', 'Open full agent workspace in external browser')"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-sub-svg">
              <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path>
              <polyline points="15 3 21 3 21 9"></polyline>
              <line x1="10" y1="14" x2="21" y2="3"></line>
            </svg>
            <span>{{ t('浏览器打开', 'Browser') }}</span>
          </button>

          <!-- Primary Start / Stop Model Action Button -->
          <button
            v-if="serviceState.status === 'Stopped'"
            class="btn-header-primary btn-gradient-blue"
            @click="handleStart"
            :title="t('启动本地大模型推理引擎', 'Start local large model inference engine')"
          >
            <svg viewBox="0 0 24 24" fill="currentColor" class="btn-primary-svg">
              <path d="M8 5v14l11-7z" />
            </svg>
            <span>{{ t('启动模型', 'Start Model') }}</span>
          </button>

          <button
            v-else-if="serviceState.status === 'Starting'"
            class="btn-header-primary btn-state-loading"
            disabled
          >
            <span class="btn-spin-dot"></span>
            <span>{{ t('模型启动中...', 'Starting Model...') }}</span>
          </button>

          <button
            v-else-if="serviceState.status === 'Running'"
            class="btn-header-primary btn-state-danger"
            @click="handleStop"
            :title="t('关闭本地模型与智能体服务', 'Stop local model & agent services')"
          >
            <svg viewBox="0 0 24 24" fill="currentColor" class="btn-primary-svg">
              <rect x="6" y="6" width="12" height="12" rx="2" />
            </svg>
            <span>{{ t('关闭模型', 'Stop Model') }}</span>
          </button>

          <button
            v-else-if="serviceState.status === 'Error'"
            class="btn-header-primary btn-state-danger"
            @click="handleStart"
          >
            <svg viewBox="0 0 24 24" fill="currentColor" class="btn-primary-svg">
              <path d="M8 5v14l11-7z" />
            </svg>
            <span>{{ t('重试启动', 'Retry') }}</span>
          </button>
        </div>
      </header>

      <!-- Viewport Content Container (Optimized for High-FPS Compositor Scrolling) -->
      <div
        class="viewport-content"
        :class="{
          'is-chat-tab': currentTab === 'chat',
          'is-chat-running': currentTab === 'chat' && serviceState.status === 'Running',
          'is-scrolling': isViewportScrolling
        }"
        @scroll.passive="handleViewportScroll"
      >
        <!-- ==============================================================
             PAGE 1: 推理性能与会话统计 (Inference & Conversation Telemetry Dashboard)
             ============================================================== -->
        <div v-show="currentTab === 'overview'" class="tab-page page-telemetry">
          <!-- 1. Top Control & Summary Banner -->
          <div class="telemetry-top-banner">
            <div class="banner-left">
              <div class="banner-icon-squircle">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="banner-svg">
                  <path d="M3 3v18h18"></path>
                  <path d="M18 17V9"></path>
                  <path d="M13 17V5"></path>
                  <path d="M8 17v-3"></path>
                </svg>
              </div>
              <div class="banner-text">
                <h2 class="banner-title">{{ t('大模型对话推理性能分析', 'Inference & Conversation Telemetry') }}</h2>
              </div>
            </div>

            <div class="banner-actions">
              <button
                class="btn-probe"
                :class="{ 'btn-probe-running': isProbeRunning }"
                @click="runProbeBenchmark"
                :disabled="isProbeRunning"
                :title="t('向本地模型发起一次探针测速，实时测量并追加首字时延与生成速度', 'Send a real-time probe to measure TTFT and generation speed')"
              >
                <svg v-if="!isProbeRunning" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" class="action-btn-svg">
                  <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon>
                </svg>
                <div v-else class="probe-spinner"></div>
                <span>{{ isProbeRunning ? t('测速分析中...', 'Probing...') : t('探针测速', 'Probe Benchmark') }}</span>
              </button>

              <div class="export-dropdown-group">
                <button class="btn-banner-subtle" @click="exportTelemetry('csv')" :title="t('导出为 CSV 表格', 'Export CSV')">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="action-btn-svg">
                    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                    <polyline points="7 10 12 15 17 10"></polyline>
                    <line x1="12" y1="15" x2="12" y2="3"></line>
                  </svg>
                  <span>{{ t('导出 CSV', 'Export CSV') }}</span>
                </button>
                <button class="btn-banner-subtle" @click="exportTelemetry('json')" :title="t('导出为 JSON 数据', 'Export JSON')">
                  <span>JSON</span>
                </button>
              </div>

              <button class="btn-banner-subtle btn-reset" @click="clearTelemetryHistory" :title="t('清空并重置统计历史', 'Reset History')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="action-btn-svg">
                  <polyline points="3 6 5 6 21 6"></polyline>
                  <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                </svg>
                <span>{{ t('重置历史', 'Reset') }}</span>
              </button>
            </div>
          </div>

          <!-- Probe Benchmark Result Toast Notification -->
          <transition name="fade">
            <div v-if="probeResultNotice" class="probe-notice-toast">
              <span class="probe-toast-dot"></span>
              <span>{{ probeResultNotice }}</span>
            </div>
          </transition>

          <!-- Demo Data Banner Notice -->
          <transition name="fade">
            <div v-if="isShowingDemoData" class="demo-data-alert-bar">
              <div class="demo-alert-left">
                <span class="demo-alert-tag">🧪 {{ t('演示模式', 'Demo Mode') }}</span>
                <span class="demo-alert-text">{{ t('当前展示的是预设的演示数据，仅供预览图表与布局。点击右侧按钮可清空并恢复本地真实记录。', 'Currently viewing preset demo data for preview. Click to restore real local telemetry.') }}</span>
              </div>
              <button class="demo-alert-action" @click="clearDemoTelemetry">
                {{ t('恢复真实数据', 'Restore Real Data') }}
              </button>
            </div>
          </transition>

          <!-- 2. KPI Summary Cards Grid (6 Metric Cards) -->
          <div class="telemetry-kpi-grid">
            <!-- Card 1: 累计对话轮次 -->
            <div class="kpi-card">
              <div class="kpi-card-header">
                <span class="kpi-label">{{ t('累计对话轮次', 'Total Dialogues') }}</span>
                <div class="kpi-icon-pill bg-blue-subtle text-blue">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path>
                  </svg>
                </div>
              </div>
              <div class="kpi-value-row">
                <span class="kpi-value">{{ telemetryKPIs.totalTurns }}</span>
                <span class="kpi-unit">{{ t('轮', 'turns') }}</span>
              </div>
            </div>

            <!-- Card 2: 累计 Token 用量 -->
            <div class="kpi-card">
              <div class="kpi-card-header">
                <span class="kpi-label">{{ t('累计 Token 吞吐', 'Token Throughput') }}</span>
                <div class="kpi-icon-pill bg-purple-subtle text-purple">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <line x1="12" y1="1" x2="12" y2="23"></line>
                    <path d="M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6"></path>
                  </svg>
                </div>
              </div>
              <div class="kpi-value-row">
                <span class="kpi-value">{{ formatTokK(telemetryKPIs.totalTokens) }}</span>
                <span class="kpi-unit">tok</span>
              </div>
            </div>

            <!-- Card 3: 平均首字时延 (TTFT) -->
            <div class="kpi-card">
              <div class="kpi-card-header">
                <span class="kpi-label">{{ t('平均首字时延 (TTFT)', 'Avg TTFT Latency') }}</span>
                <div class="kpi-icon-pill bg-emerald-subtle text-emerald">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <circle cx="12" cy="12" r="10"></circle>
                    <polyline points="12 6 12 12 16 14"></polyline>
                  </svg>
                </div>
              </div>
              <div class="kpi-value-row">
                <span class="kpi-value">{{ telemetryKPIs.totalTurns > 0 ? telemetryKPIs.avgTtftMs : '--' }}</span>
                <span class="kpi-unit">ms</span>
              </div>
            </div>

            <!-- Card 4: 平均生成速率 (tok/s) -->
            <div class="kpi-card">
              <div class="kpi-card-header">
                <span class="kpi-label">{{ t('平均生成速率', 'Avg Generation Speed') }}</span>
                <div class="kpi-icon-pill bg-blue-subtle text-blue">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon>
                  </svg>
                </div>
              </div>
              <div class="kpi-value-row">
                <span class="kpi-value text-blue">{{ telemetryKPIs.totalTurns > 0 ? telemetryKPIs.avgSpeed : '--' }}</span>
                <span class="kpi-unit">tok/s</span>
              </div>
            </div>

            <!-- Card 5: 平均思考耗时 -->
            <div class="kpi-card">
              <div class="kpi-card-header">
                <span class="kpi-label">{{ t('平均思考耗时', 'Avg Reasoning Time') }}</span>
                <div class="kpi-icon-pill bg-purple-subtle text-purple">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <path d="M12 2a7 7 0 0 1 7 7c0 2.38-1.19 4.47-3 5.74V17a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2v-2.26C6.19 13.47 5 11.38 5 9a7 7 0 0 1 7-7z"></path>
                    <line x1="9" y1="21" x2="15" y2="21"></line>
                  </svg>
                </div>
              </div>
              <div class="kpi-value-row">
                <span class="kpi-value">{{ telemetryKPIs.totalTurns > 0 ? telemetryKPIs.avgThinkingSec : '--' }}</span>
                <span class="kpi-unit">{{ t('秒', 's') }}</span>
              </div>
            </div>

            <!-- Card 6: KV 缓存平均命中率 -->
            <div class="kpi-card">
              <div class="kpi-card-header">
                <span class="kpi-label">{{ t('KV 缓存平均命中率', 'Avg KV Cache Hit') }}</span>
                <div class="kpi-icon-pill bg-cyan-subtle text-cyan">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path>
                  </svg>
                </div>
              </div>
              <div class="kpi-value-row">
                <span class="kpi-value text-emerald">{{ telemetryKPIs.totalTurns > 0 ? telemetryKPIs.avgCacheHitPct : '--' }}</span>
                <span class="kpi-unit">%</span>
              </div>
            </div>
          </div>

          <!-- 3. Visual Charts Grid (Two Rich Charts Side-by-Side) -->
          <div class="telemetry-charts-grid">
            <!-- Chart 1: 推理速率 (tok/s) 与 首字时延 (ms) 趋势折线图 -->
            <div class="chart-panel-card">
              <div class="chart-panel-header">
                <div class="chart-header-left">
                  <h3 class="chart-panel-title">{{ t('推理生成速度与首字时延波动走势', 'Generation Speed & TTFT Latency Trend') }}</h3>
                  <span class="chart-panel-subtitle">{{ t('基于最近对话轮次实时采样 · 含基准平均线', 'Real-time sampling from recent conversation turns with benchmark baseline') }}</span>
                </div>
                <div class="chart-metric-switch">
                  <button
                    class="metric-pill"
                    :class="{ 'metric-pill-active': activeChartMetric === 'speed' }"
                    @click="activeChartMetric = 'speed'"
                  >
                    <span class="legend-dot dot-blue"></span>
                    {{ t('速度 (tok/s)', 'Speed') }}
                  </button>
                  <button
                    class="metric-pill"
                    :class="{ 'metric-pill-active': activeChartMetric === 'ttft' }"
                    @click="activeChartMetric = 'ttft'"
                  >
                    <span class="legend-dot dot-purple"></span>
                    {{ t('首字 (ms)', 'TTFT') }}
                  </button>
                  <button
                    class="metric-pill"
                    :class="{ 'metric-pill-active': activeChartMetric === 'both' }"
                    @click="activeChartMetric = 'both'"
                  >
                    {{ t('双轴对照', 'Dual Axis') }}
                  </button>
                </div>
              </div>

              <!-- SVG Interactive Trend Chart -->
              <div class="svg-chart-container" v-if="speedChartData">
                <svg viewBox="0 0 760 210" class="telemetry-svg">
                  <defs>
                    <!-- Speed Area Gradient -->
                    <linearGradient id="speedAreaGrad" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="0%" stop-color="#3b82f6" stop-opacity="0.22"/>
                      <stop offset="100%" stop-color="#3b82f6" stop-opacity="0.01"/>
                    </linearGradient>
                    <!-- TTFT Area Gradient -->
                    <linearGradient id="ttftAreaGrad" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="0%" stop-color="#a855f7" stop-opacity="0.20"/>
                      <stop offset="100%" stop-color="#a855f7" stop-opacity="0.01"/>
                    </linearGradient>
                  </defs>

                  <!-- Horizontal Gridlines & Y-Axis Labels -->
                  <g class="chart-grid">
                    <line x1="50" y1="30" x2="720" y2="30" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="65" x2="720" y2="65" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="102" x2="720" y2="102" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="138" x2="720" y2="138" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="175" x2="720" y2="175" stroke="#e2e8f0"/>

                    <!-- Left Y-Axis labels (Speed) -->
                    <text v-if="activeChartMetric !== 'ttft'" x="42" y="34" text-anchor="end" class="chart-axis-label">{{ speedChartData.maxSpeed }}</text>
                    <text v-if="activeChartMetric !== 'ttft'" x="42" y="105" text-anchor="end" class="chart-axis-label">{{ Math.round((speedChartData.maxSpeed + speedChartData.minSpeed)/2) }}</text>
                    <text v-if="activeChartMetric !== 'ttft'" x="42" y="175" text-anchor="end" class="chart-axis-label">{{ speedChartData.minSpeed }}</text>

                    <!-- Right Y-Axis labels (TTFT) when in TTFT mode -->
                    <text v-if="activeChartMetric === 'ttft'" x="42" y="34" text-anchor="end" class="chart-axis-label">{{ speedChartData.maxTtft }}ms</text>
                    <text v-if="activeChartMetric === 'ttft'" x="42" y="105" text-anchor="end" class="chart-axis-label">{{ Math.round((speedChartData.maxTtft + speedChartData.minTtft)/2) }}ms</text>
                    <text v-if="activeChartMetric === 'ttft'" x="42" y="175" text-anchor="end" class="chart-axis-label">{{ speedChartData.minTtft }}ms</text>
                  </g>

                  <!-- Benchmark Baseline Dashed Line (Speed) -->
                  <g v-if="activeChartMetric !== 'ttft'" class="benchmark-line">
                    <line
                      x1="50"
                      :y1="speedChartData.avgSpeedY"
                      x2="720"
                      :y2="speedChartData.avgSpeedY"
                      stroke="#93c5fd"
                      stroke-width="1.5"
                      stroke-dasharray="6 4"
                    />
                    <text
                      x="715"
                      :y="speedChartData.avgSpeedY - 4"
                      text-anchor="end"
                      class="benchmark-tag"
                    >
                      {{ t('平均速率', 'Avg') }} {{ speedChartData.avgSpeed }} tok/s
                    </text>
                  </g>

                  <!-- TTFT Baseline Dashed Line (when in TTFT mode) -->
                  <g v-if="activeChartMetric === 'ttft'" class="benchmark-line">
                    <line
                      x1="50"
                      :y1="speedChartData.avgTtftY"
                      x2="720"
                      :y2="speedChartData.avgTtftY"
                      stroke="#d8b4fe"
                      stroke-width="1.5"
                      stroke-dasharray="6 4"
                    />
                    <text
                      x="715"
                      :y="speedChartData.avgTtftY - 4"
                      text-anchor="end"
                      class="benchmark-tag text-purple"
                    >
                      {{ t('平均首字', 'Avg TTFT') }} {{ speedChartData.avgTtft }} ms
                    </text>
                  </g>

                  <!-- Area Fills -->
                  <path
                    v-if="activeChartMetric !== 'ttft' && speedChartData.speedArea"
                    :d="speedChartData.speedArea"
                    fill="url(#speedAreaGrad)"
                  />
                  <path
                    v-if="activeChartMetric !== 'speed' && speedChartData.ttftArea"
                    :d="speedChartData.ttftArea"
                    fill="url(#ttftAreaGrad)"
                  />

                  <!-- Smooth Curve Lines -->
                  <path
                    v-if="activeChartMetric !== 'ttft' && speedChartData.speedLine"
                    :d="speedChartData.speedLine"
                    fill="none"
                    stroke="#2563eb"
                    stroke-width="3"
                    stroke-linecap="round"
                    stroke-linejoin="round"
                  />
                  <path
                    v-if="activeChartMetric !== 'speed' && speedChartData.ttftLine"
                    :d="speedChartData.ttftLine"
                    fill="none"
                    stroke="#9333ea"
                    stroke-width="2.5"
                    stroke-linecap="round"
                    stroke-linejoin="round"
                  />

                  <!-- Interactive Data Points (Speed) -->
                  <g v-if="activeChartMetric !== 'ttft'">
                    <g
                      v-for="p in speedChartData.points"
                      :key="'pt-speed-' + p.record.id"
                      class="chart-point-group"
                      @mouseenter="hoveredPoint = { record: p.record, x: p.x, y: p.ySpeed }"
                      @mouseleave="hoveredPoint = null"
                    >
                      <circle :cx="p.x" :cy="p.ySpeed" r="6" fill="#ffffff" stroke="#2563eb" stroke-width="2.5"/>
                      <circle :cx="p.x" :cy="p.ySpeed" r="14" fill="transparent"/>
                      <text :x="p.x" y="194" text-anchor="middle" class="chart-x-label">#{{ p.record.turnNumber }}</text>
                    </g>
                  </g>

                  <!-- Interactive Data Points (TTFT when in TTFT mode) -->
                  <g v-if="activeChartMetric === 'ttft'">
                    <g
                      v-for="p in speedChartData.points"
                      :key="'pt-ttft-' + p.record.id"
                      class="chart-point-group"
                      @mouseenter="hoveredPoint = { record: p.record, x: p.x, y: p.yTtft }"
                      @mouseleave="hoveredPoint = null"
                    >
                      <circle :cx="p.x" :cy="p.yTtft" r="6" fill="#ffffff" stroke="#9333ea" stroke-width="2.5"/>
                      <circle :cx="p.x" :cy="p.yTtft" r="14" fill="transparent"/>
                      <text :x="p.x" y="194" text-anchor="middle" class="chart-x-label">#{{ p.record.turnNumber }}</text>
                    </g>
                  </g>
                </svg>

                <!-- Floating Hover Tooltip -->
                <transition name="fade">
                  <div
                    v-if="hoveredPoint"
                    class="chart-tooltip-floating"
                    :style="{ left: `${(hoveredPoint.x / 760) * 100}%`, top: `${Math.max(10, (hoveredPoint.y / 210) * 100 - 32)}%` }"
                  >
                    <div class="tooltip-header">
                      <span class="tooltip-turn">#{{ hoveredPoint.record.turnNumber }} 轮 · {{ hoveredPoint.record.taskType }}</span>
                      <span class="tooltip-time">{{ hoveredPoint.record.timeFormatted }}</span>
                    </div>
                    <div class="tooltip-metrics">
                      <div class="tmetric-item">
                        <span class="tmetric-k">{{ t('解码速度', 'Speed') }}:</span>
                        <span class="tmetric-v text-blue">{{ hoveredPoint.record.generationSpeedTokPerSec }} tok/s</span>
                      </div>
                      <div class="tmetric-item">
                        <span class="tmetric-k">{{ t('首字时延', 'TTFT') }}:</span>
                        <span class="tmetric-v text-purple">{{ hoveredPoint.record.ttftMs }} ms</span>
                      </div>
                      <div class="tmetric-item">
                        <span class="tmetric-k">{{ t('思考耗时', 'Reasoning') }}:</span>
                        <span class="tmetric-v">{{ hoveredPoint.record.thinkingDurationSec }}s ({{ hoveredPoint.record.thinkingTokens }} tok)</span>
                      </div>
                      <div class="tmetric-item">
                        <span class="tmetric-k">{{ t('Token 总量', 'Total Tok') }}:</span>
                        <span class="tmetric-v">{{ hoveredPoint.record.totalTokens }} tok</span>
                      </div>
                    </div>
                  </div>
                </transition>
              </div>

              <!-- Empty State when 0 telemetry records -->
              <div v-else class="chart-empty-state-box">
                <div class="chart-empty-inner">
                  <div class="chart-empty-icon">📈</div>
                  <div class="chart-empty-title">{{ t('暂无实时采样走势', 'No Real-time Sampling Data Yet') }}</div>
                  <div class="chart-empty-desc">{{ t('与大模型开始对话，或点击右上角「⚡ 探针测速」获取实时走势。', 'Start chatting with local agent or click Benchmark Probe to capture real-time curves.') }}</div>
                  <button class="btn-chart-demo" @click="loadDemoTelemetry">
                    🧪 {{ t('载入演示数据', 'Load Demo Data') }}
                  </button>
                </div>
              </div>
            </div>

            <!-- Chart 2: 每次对话 Token 吞吐与构成分布堆积图 -->
            <div class="chart-panel-card">
              <div class="chart-panel-header">
                <div class="chart-header-left">
                  <h3 class="chart-panel-title">{{ t('每次对话 Token 吞吐与构成分布', 'Token Consumption & Composition per Turn') }}</h3>
                  <span class="chart-panel-subtitle">{{ t('单轮提示词、深度推导与回复 Token 占比', 'Breakdown of Prompt, Chain-of-Thought, and Output tokens per dialogue') }}</span>
                </div>
                <div class="chart-legends-row">
                  <span class="legend-item"><span class="legend-box bg-prompt"></span>Prompt ({{ t('输入', 'Input') }})</span>
                  <span class="legend-item"><span class="legend-box bg-thinking"></span>Thinking ({{ t('思考', 'Think') }})</span>
                  <span class="legend-item"><span class="legend-box bg-output"></span>Output ({{ t('输出', 'Output') }})</span>
                </div>
              </div>

              <!-- SVG Interactive Stacked Bar Chart -->
              <div class="svg-chart-container" v-if="barChartData">
                <svg viewBox="0 0 760 210" class="telemetry-svg">
                  <!-- Horizontal Gridlines -->
                  <g class="chart-grid">
                    <line x1="50" y1="30" x2="720" y2="30" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="65" x2="720" y2="65" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="102" x2="720" y2="102" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="138" x2="720" y2="138" stroke="#f1f5f9" stroke-dasharray="4 4"/>
                    <line x1="50" y1="175" x2="720" y2="175" stroke="#e2e8f0"/>

                    <text x="42" y="34" text-anchor="end" class="chart-axis-label">{{ formatTokK(barChartData.maxTokens) }}</text>
                    <text x="42" y="105" text-anchor="end" class="chart-axis-label">{{ formatTokK(Math.round(barChartData.maxTokens / 2)) }}</text>
                    <text x="42" y="175" text-anchor="end" class="chart-axis-label">0</text>
                  </g>

                  <!-- Stacked Bars -->
                  <g v-for="b in barChartData.bars" :key="'bar-' + b.record.id" class="chart-bar-group"
                     @mouseenter="hoveredBar = { record: b.record, x: b.x + b.barWidth / 2, y: b.yTotal }"
                     @mouseleave="hoveredBar = null"
                  >
                    <!-- Segment 1: Prompt (Bottom) -->
                    <rect
                      :x="b.x"
                      :y="b.yPrompt"
                      :width="b.barWidth"
                      :height="b.hPrompt"
                      fill="#93c5fd"
                    />

                    <!-- Segment 2: Thinking (Middle) -->
                    <rect
                      :x="b.x"
                      :y="b.yThinking"
                      :width="b.barWidth"
                      :height="b.hThinking"
                      fill="#c084fc"
                    />

                    <!-- Segment 3: Output (Top with rounded corners) -->
                    <rect
                      :x="b.x"
                      :y="b.yOutput"
                      :width="b.barWidth"
                      :height="b.hOutput"
                      rx="3"
                      ry="3"
                      fill="#34d399"
                    />

                    <!-- Value label above bar -->
                    <text
                      :x="b.x + b.barWidth / 2"
                      :y="b.yTotal - 5"
                      text-anchor="middle"
                      class="bar-val-label"
                    >
                      {{ formatTokK(b.totalTokens) }}
                    </text>

                    <!-- Turn # on X-axis -->
                    <text
                      :x="b.x + b.barWidth / 2"
                      y="194"
                      text-anchor="middle"
                      class="chart-x-label"
                    >
                      #{{ b.record.turnNumber }}
                    </text>
                  </g>
                </svg>

                <!-- Floating Hover Tooltip for Stacked Bar -->
                <transition name="fade">
                  <div
                    v-if="hoveredBar"
                    class="chart-tooltip-floating"
                    :style="{ left: `${(hoveredBar.x / 760) * 100}%`, top: `${Math.max(10, (hoveredBar.y / 210) * 100 - 32)}%` }"
                  >
                    <div class="tooltip-header">
                      <span class="tooltip-turn">#{{ hoveredBar.record.turnNumber }} 轮 · {{ hoveredBar.record.taskType }}</span>
                      <span class="tooltip-time">{{ hoveredBar.record.timeFormatted }}</span>
                    </div>
                    <div class="tooltip-metrics">
                      <div class="tmetric-item">
                        <span class="tmetric-k">总用量:</span>
                        <span class="tmetric-v text-bold">{{ hoveredBar.record.totalTokens }} tok</span>
                      </div>
                      <div class="tmetric-item">
                        <span class="tmetric-k"><span class="legend-dot dot-blue"></span>Prompt (输入):</span>
                        <span class="tmetric-v">{{ hoveredBar.record.promptTokens }} tok ({{ Math.round((hoveredBar.record.promptTokens / hoveredBar.record.totalTokens) * 100) }}%)</span>
                      </div>
                      <div class="tmetric-item">
                        <span class="tmetric-k"><span class="legend-dot dot-purple"></span>Thinking (思考):</span>
                        <span class="tmetric-v">{{ hoveredBar.record.thinkingTokens }} tok ({{ Math.round((hoveredBar.record.thinkingTokens / hoveredBar.record.totalTokens) * 100) }}%)</span>
                      </div>
                      <div class="tmetric-item">
                        <span class="tmetric-k"><span class="legend-dot dot-green"></span>Output (输出):</span>
                        <span class="tmetric-v">{{ hoveredBar.record.outputTokens }} tok ({{ Math.round((hoveredBar.record.outputTokens / hoveredBar.record.totalTokens) * 100) }}%)</span>
                      </div>
                    </div>
                  </div>
                </transition>
              </div>

              <!-- Empty State when 0 bar chart data -->
              <div v-else class="chart-empty-state-box">
                <div class="chart-empty-inner">
                  <div class="chart-empty-icon">📊</div>
                  <div class="chart-empty-title">{{ t('暂无 Token 构成分布', 'No Token Breakdown Data Yet') }}</div>
                  <div class="chart-empty-desc">{{ t('真实会话发生后，系统将自动拆解输入、思考与输出 Token 构成。', 'Input, reasoning, and output tokens will be decomposed automatically after real conversations.') }}</div>
                  <button class="btn-chart-demo" @click="loadDemoTelemetry">
                    🧪 {{ t('载入演示数据', 'Load Demo Data') }}
                  </button>
                </div>
              </div>

              <!-- Composition Progress Ratio Bar -->
              <div class="composition-summary-bar">
                <div class="summary-bar-track">
                  <div class="track-segment seg-prompt" :style="{ width: `${Math.round((telemetryKPIs.promptTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100)}%` }" :title="`Prompt: ${Math.round((telemetryKPIs.promptTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100)}%`"></div>
                  <div class="track-segment seg-thinking" :style="{ width: `${Math.round((telemetryKPIs.thinkingTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100)}%` }" :title="`Thinking: ${Math.round((telemetryKPIs.thinkingTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100)}%`"></div>
                  <div class="track-segment seg-output" :style="{ width: `${Math.round((telemetryKPIs.outputTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100)}%` }" :title="`Output: ${Math.round((telemetryKPIs.outputTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100)}%`"></div>
                </div>
                <div class="summary-bar-labels">
                  <span class="slabel">Prompt <b>{{ Math.round((telemetryKPIs.promptTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100) }}%</b></span>
                  <span class="slabel text-purple">Thinking <b>{{ Math.round((telemetryKPIs.thinkingTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100) }}%</b></span>
                  <span class="slabel text-emerald">Output <b>{{ Math.round((telemetryKPIs.outputTokens / Math.max(1, telemetryKPIs.totalTokens)) * 100) }}%</b></span>
                </div>
              </div>
            </div>
          </div>

          <!-- 4. Detailed Turns Telemetry Table Panel (详情面板) -->
          <div class="telemetry-table-panel">
            <div class="table-panel-header">
              <div class="panel-header-left">
                <h3 class="panel-title">{{ t('每次对话明细参数详情面板', 'Detailed Conversation Turns Telemetry') }}</h3>
                <span class="panel-subtitle">{{ t('包含思考耗时、首字时延、生成速率、Token 细分用量及缓存命中状态', 'Full telemetry parameters per turn including reasoning time, TTFT, speed, token breakdown, and cache hit') }}</span>
              </div>

              <div class="panel-header-right">
                <!-- Search Box -->
                <div class="search-box-pill">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="search-svg">
                    <circle cx="11" cy="11" r="8"></circle>
                    <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
                  </svg>
                  <input
                    type="text"
                    v-model="telemetrySearch"
                    :placeholder="t('搜索轮次、任务类型或提示词...', 'Search turns, tasks, or prompts...')"
                    class="search-input"
                  />
                  <button v-if="telemetrySearch" class="search-clear-btn" @click="telemetrySearch = ''">×</button>
                </div>

                <!-- Range Filters -->
                <div class="filter-pills-row">
                  <button
                    class="fpill-btn"
                    :class="{ 'fpill-active': telemetryTimeFilter === 'all' }"
                    @click="telemetryTimeFilter = 'all'"
                  >
                    {{ t('全部轮次', 'All') }} ({{ telemetryRecords.length }})
                  </button>
                  <button
                    class="fpill-btn"
                    :class="{ 'fpill-active': telemetryTimeFilter === 'recent5' }"
                    @click="telemetryTimeFilter = 'recent5'"
                  >
                    {{ t('最近 5 轮', 'Recent 5') }}
                  </button>
                  <button
                    class="fpill-btn"
                    :class="{ 'fpill-active': telemetryTimeFilter === 'today' }"
                    @click="telemetryTimeFilter = 'today'"
                  >
                    {{ t('今日记录', 'Today') }}
                  </button>
                </div>
              </div>
            </div>

            <!-- Table Container -->
            <div class="telemetry-table-wrapper">
              <table class="telemetry-table">
                <thead>
                  <tr>
                    <th style="width: 70px;">{{ t('轮次', 'Turn') }}</th>
                    <th style="width: 120px;">{{ t('时间', 'Time') }}</th>
                    <th style="width: 100px;">{{ t('任务类型', 'Task') }}</th>
                    <th>{{ t('思考耗时 / Token', 'Reasoning Time / Tok') }}</th>
                    <th>{{ t('首字时延 (TTFT)', 'TTFT Latency') }}</th>
                    <th>{{ t('解码速率', 'Decoding Speed') }}</th>
                    <th>{{ t('Token 用量构成', 'Token Distribution') }}</th>
                    <th>{{ t('KV 缓存命中', 'Cache Hit') }}</th>
                    <th style="width: 80px;">{{ t('状态', 'Status') }}</th>
                    <th style="width: 70px; text-align: center;">{{ t('详情', 'Detail') }}</th>
                  </tr>
                </thead>
                <tbody>
                  <template v-for="r in filteredTelemetryRecords" :key="'row-' + r.id">
                    <tr
                      class="telemetry-row"
                      :class="{ 'row-expanded': expandedTurnId === r.id }"
                      @click="toggleTurnExpand(r.id)"
                    >
                      <td class="col-turn">
                        <span class="turn-badge">#{{ r.turnNumber }}</span>
                      </td>
                      <td class="col-time">{{ r.timeFormatted }}</td>
                      <td class="col-task">
                        <span class="task-tag">{{ r.taskType }}</span>
                      </td>
                      <td class="col-thinking">
                        <div class="thinking-cell">
                          <span class="cell-val text-purple">{{ r.thinkingDurationSec }}s</span>
                          <span class="cell-sub">({{ r.thinkingTokens }} tok)</span>
                        </div>
                      </td>
                      <td class="col-ttft">
                        <span class="ttft-badge" :class="r.ttftMs < 350 ? 'ttft-fast' : 'ttft-normal'">
                          {{ r.ttftMs }} ms
                        </span>
                      </td>
                      <td class="col-speed">
                        <div class="speed-cell">
                          <span class="speed-num">{{ r.generationSpeedTokPerSec }} <span class="unit">tok/s</span></span>
                          <div class="mini-speed-bar">
                            <div class="speed-fill" :style="{ width: `${Math.min(100, (r.generationSpeedTokPerSec / 80) * 100)}%` }"></div>
                          </div>
                        </div>
                      </td>
                      <td class="col-tokens">
                        <div class="token-breakdown-inline">
                          <span class="token-total">{{ formatTokK(r.totalTokens) }}</span>
                          <div class="inline-token-chips">
                            <span class="tok-chip chip-p" :title="`Prompt: ${r.promptTokens} tok`">P:{{ formatTokK(r.promptTokens) }}</span>
                            <span class="tok-chip chip-t" :title="`Thinking: ${r.thinkingTokens} tok`">T:{{ formatTokK(r.thinkingTokens) }}</span>
                            <span class="tok-chip chip-o" :title="`Output: ${r.outputTokens} tok`">O:{{ formatTokK(r.outputTokens) }}</span>
                          </div>
                        </div>
                      </td>
                      <td class="col-cache">
                        <span class="cache-badge" :class="r.cacheHitRatePct > 50 ? 'cache-hit' : 'cache-cold'">
                          {{ r.cacheHitRatePct > 0 ? `${r.cacheHitRatePct}%` : t('冷启动', 'Cold') }}
                        </span>
                      </td>
                      <td class="col-status">
                        <span class="status-pill status-ok">
                          <span class="status-dot"></span>
                          {{ t('完成', 'Done') }}
                        </span>
                      </td>
                      <td class="col-action" style="text-align: center;">
                        <button class="btn-expand-row" :title="t('展开单轮详情', 'Toggle details')">
                          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="arrow-svg" :class="{ 'arrow-rotated': expandedTurnId === r.id }">
                            <polyline points="6 9 12 15 18 9"></polyline>
                          </svg>
                        </button>
                      </td>
                    </tr>

                    <!-- Expanded Details Drawer Row -->
                    <tr v-if="expandedTurnId === r.id" class="drawer-detail-row">
                      <td colspan="10">
                        <div class="turn-detail-drawer">
                          <div class="drawer-grid">
                            <div class="drawer-col">
                              <span class="dcol-title">{{ t('会话输入提示词摘要', 'Prompt Summary') }}</span>
                              <div class="dcol-content prompt-box">
                                {{ r.promptPreview }}
                              </div>
                            </div>
                            <div class="drawer-col">
                              <span class="dcol-title">{{ t('性能诊断与硬件调度', 'Performance Diagnostics') }}</span>
                              <div class="drawer-metrics-list">
                                <div class="dmetric-row">
                                  <span class="dm-label">{{ t('预填充首字延迟 (TTFT)', 'TTFT Latency') }}:</span>
                                  <span class="dm-val highlight">{{ r.ttftMs }} ms ({{ r.ttftMs < 350 ? t('极速响应', 'Excellent') : t('正常', 'Normal') }})</span>
                                </div>
                                <div class="dmetric-row">
                                  <span class="dm-label">{{ t('推测解码速率 (MTP3)', 'Decoding Speed') }}:</span>
                                  <span class="dm-val text-blue">{{ r.generationSpeedTokPerSec }} tok/s</span>
                                </div>
                                <div class="dmetric-row">
                                  <span class="dm-label">{{ t('思考推导耗时与占比', 'Reasoning Ratio') }}:</span>
                                  <span class="dm-val text-purple">{{ r.thinkingDurationSec }}s ({{ Math.round((r.thinkingTokens / r.totalTokens) * 100) }}% Token)</span>
                                </div>
                                <div class="dmetric-row">
                                  <span class="dm-label">{{ t('推理模型权重', 'Model Weights') }}:</span>
                                  <span class="dm-val info-code" :title="modelFileName">{{ modelFileName }}</span>
                                </div>
                                <div v-if="r.steps && r.steps > 1" class="dmetric-row">
                                  <span class="dm-label">{{ t('智能体协同执行', 'Agent Loop Steps') }}:</span>
                                  <span class="dm-val text-blue">{{ r.steps }} {{ t('轮工具调用与思考协同', 'steps of tool calls & reasoning') }}</span>
                                </div>
                                <div class="dmetric-row">
                                  <span class="dm-label">{{ t('Cache Guard 前缀缓存', 'Cache Guard Status') }}:</span>
                                  <span class="dm-val text-emerald">{{ r.cacheHitRatePct > 0 ? `${r.cacheHitRatePct}% 命中` : '冷启动 prefill' }}</span>
                                </div>
                              </div>
                            </div>
                          </div>
                        </div>
                      </td>
                    </tr>
                  </template>
                </tbody>
              </table>

              <!-- Empty State if 0 records or search yields no results -->
              <div v-if="telemetryRecords.length === 0" class="telemetry-empty-box">
                <div class="empty-icon-circle">📊</div>
                <span class="empty-title">{{ t('暂无对话遥测记录', 'No Telemetry Records Yet') }}</span>
                <span class="empty-desc">{{ t('与大模型开始对话，或点击右上角「⚡ 探针测速」获取真实硬件推理参数。', 'Start chatting with local agent or click Benchmark Probe to capture real-time curves.') }}</span>
                <button class="btn-chart-demo" style="margin-top: 10px;" @click="loadDemoTelemetry">
                  🧪 {{ t('载入演示数据', 'Load Demo Data') }}
                </button>
              </div>
              <div v-else-if="filteredTelemetryRecords.length === 0" class="telemetry-empty-box">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="empty-svg">
                  <circle cx="11" cy="11" r="8"></circle>
                  <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
                </svg>
                <span class="empty-title">{{ t('未找到匹配的对话推理记录', 'No matching telemetry records found') }}</span>
                <span class="empty-desc">{{ t('请尝试更改搜索关键字或选择「全部轮次」', 'Try adjusting your search query or reset filter') }}</span>
              </div>
            </div>
          </div>
        </div>

        <!-- ==============================================================
             PAGE 2: 智能对话 (Chat - 1:1 DSH Agent Embedded or Launcher)
             ============================================================== -->
        <div v-show="currentTab === 'chat'" class="tab-page page-chat">
          <!-- State A: Running -> Seamless Full-Bleed DSH Agent -->
          <div
            v-if="serviceState.status === 'Running'"
            class="chat-viewport-running"
            @click="ensureIframeFocus"
            @mouseenter="ensureIframeFocus"
          >
            <iframe
              :key="iframeKey"
              :src="serviceState.data?.web_url"
              class="dsh-seamless-frame"
              tabindex="0"
              allow="clipboard-read; clipboard-write; forms; scripts; same-origin; fullscreen;"
              @load="onIframeLoad"
            ></iframe>
          </div>

          <!-- State B: Starting -> Friendly & Modern Dedicated Loading Viewport -->
          <div v-else-if="serviceState.status === 'Starting'" class="chat-viewport-starting">
            <div class="starting-container">
              <!-- Glowing Tech Hero Core -->
              <div class="starting-core-hero">
                <div class="core-pulse-rings">
                  <div class="pulse-ring ring-1"></div>
                  <div class="pulse-ring ring-2"></div>
                </div>
                <div class="core-central-squircle">
                  <svg viewBox="0 0 100 100" fill="none" class="core-svg-icon">
                    <path d="M26 24C26 21.79 27.79 20 30 20H48C62.36 20 74 31.64 74 46C74 60.36 62.36 72 48 72H38V76C38 78.21 36.21 80 34 80H30C27.79 80 26 78.21 26 76V24Z" fill="url(#pFlowLoadingHero)"/>
                    <path d="M38 32H48C55.73 32 62 38.27 62 46C62 53.73 55.73 60 48 60H38V32Z" fill="#090d16"/>
                    <path d="M50 38L57 46L50 54L43 46L50 38Z" fill="#ffffff"/>
                    <path d="M26 84C40 88 60 88 74 84" stroke="#38bdf8" stroke-width="4" stroke-linecap="round"/>
                    <defs>
                      <linearGradient id="pFlowLoadingHero" x1="26" y1="20" x2="74" y2="80" gradientUnits="userSpaceOnUse">
                        <stop offset="0%" stop-color="#38bdf8"/>
                        <stop offset="50%" stop-color="#6366f1"/>
                        <stop offset="100%" stop-color="#a855f7"/>
                      </linearGradient>
                    </defs>
                  </svg>
                </div>
              </div>

              <!-- Main Titles & Subtitle -->
              <div class="starting-hero-header">
                <h2 class="starting-hero-title">
                  {{ detectedModelDisplayName ? `${detectedModelDisplayName} 智能体启动中...` : t('本地大模型智能体启动中...', 'Launching Local AI Agent...') }}
                </h2>
                <p class="starting-hero-subtitle">
                  {{ detectedGpuShortName ? (`正在为 ${detectedGpuShortName} 载入 ${detectedModelDisplayName || '大模型'} 与极速推理架构，请稍候片刻`) : t('正在载入模型与极速推理架构，请稍候片刻', 'Loading model & ultra-fast inference pipeline, please wait...') }}
                </p>
                <div class="starting-hero-tags">
                  <span class="hero-tag-pill" v-if="detectedModelDisplayName">
                    <span class="tag-dot-green"></span>
                    {{ detectedModelDisplayName }}
                  </span>
                  <span class="hero-tag-pill" v-if="detectedGpuBadge">
                    ⚡ {{ detectedGpuBadge }}
                  </span>
                  <span class="hero-tag-pill">
                    🛡️ {{ t('零显存溢出 Host KV 保护', 'Host KV Zero Spillover') }}
                  </span>
                </div>
              </div>

              <!-- Glowing Progress Bar & Live Status -->
              <div class="starting-progress-card">
                <div class="progress-info-row">
                  <span class="progress-step-text">
                    <span class="mini-spin-dot"></span>
                    {{ serviceState.data?.step || currentStartupStepDescription }}
                  </span>
                  <span class="progress-percent-val">{{ startupProgress }}%</span>
                </div>
                <div class="progress-track-wrapper">
                  <div class="progress-fill-bar" :style="{ width: startupProgress + '%' }">
                    <div class="progress-shimmer"></div>
                  </div>
                </div>
              </div>

              <!-- 4-Stage Visual Timeline Cards -->
              <div class="starting-stages-grid">
                <!-- Stage 1 -->
                <div class="stage-card" :class="{
                  'stage-done': startupStage > 1,
                  'stage-active': startupStage === 1,
                  'stage-pending': startupStage < 1
                }">
                  <div class="stage-icon-circle">
                    <span v-if="startupStage > 1">✓</span>
                    <span v-else-if="startupStage === 1" class="stage-spin-icon">⚡</span>
                    <span v-else>1</span>
                  </div>
                  <div class="stage-text-block">
                    <div class="stage-title">{{ t('显存准备', 'VRAM Preparation') }}</div>
                    <div class="stage-desc">{{ t('检查独显占用', 'Check VRAM') }}</div>
                  </div>
                  <span class="stage-status-badge">
                    {{ startupStage > 1 ? t('已就绪', 'Done') : startupStage === 1 ? t('核验中', 'Active') : t('等待', 'Pending') }}
                  </span>
                </div>

                <!-- Stage 2 -->
                <div class="stage-card" :class="{
                  'stage-done': startupStage > 2,
                  'stage-active': startupStage === 2,
                  'stage-pending': startupStage < 2
                }">
                  <div class="stage-icon-circle">
                    <span v-if="startupStage > 2">✓</span>
                    <span v-else-if="startupStage === 2" class="stage-spin-icon">⚡</span>
                    <span v-else>2</span>
                  </div>
                  <div class="stage-text-block">
                    <div class="stage-title">{{ t('权重极速映射', 'Model Weights') }}</div>
                    <div class="stage-desc">{{ t('NVFP4/RK4V4 量化 · 直接显存 DMA 映射', 'Quantized weights DMA stream') }}</div>
                  </div>
                  <span class="stage-status-badge">
                    {{ startupStage > 2 ? t('已映射', 'Done') : startupStage === 2 ? t('载入中', 'Active') : t('等待', 'Pending') }}
                  </span>
                </div>

                <!-- Stage 3 -->
                <div class="stage-card" :class="{
                  'stage-done': startupStage > 3,
                  'stage-active': startupStage === 3,
                  'stage-pending': startupStage < 3
                }">
                  <div class="stage-icon-circle">
                    <span v-if="startupStage > 3">✓</span>
                    <span v-else-if="startupStage === 3" class="stage-spin-icon">⚡</span>
                    <span v-else>3</span>
                  </div>
                  <div class="stage-text-block">
                    <div class="stage-title">{{ t('CUDA 图预热 & Host KV', 'CUDA Graph & 7GB KV') }}</div>
                    <div class="stage-desc">{{ t('捕获静态图', 'Capture CUDA Graph') }}</div>
                  </div>
                  <span class="stage-status-badge">
                    {{ startupStage > 3 ? t('已预热', 'Done') : startupStage === 3 ? t('预热中', 'Active') : t('等待', 'Pending') }}
                  </span>
                </div>

                <!-- Stage 4 -->
                <div class="stage-card" :class="{
                  'stage-done': startupStage > 4,
                  'stage-active': startupStage === 4,
                  'stage-pending': startupStage < 4
                }">
                  <div class="stage-icon-circle">
                    <span v-if="startupStage > 4">✓</span>
                    <span v-else-if="startupStage === 4" class="stage-spin-icon">⚡</span>
                    <span v-else>4</span>
                  </div>
                  <div class="stage-text-block">
                    <div class="stage-title">{{ t('智能体挂载', 'DSH Agent') }}</div>
                    <div class="stage-desc">{{ t('Cache Guard 安全守卫 · DSH 交互总线', 'Zero leakage guard & DSH bus') }}</div>
                  </div>
                  <span class="stage-status-badge">
                    {{ startupStage > 4 ? t('已在线', 'Done') : startupStage === 4 ? t('挂载中', 'Active') : t('等待', 'Pending') }}
                  </span>
                </div>
              </div>

              <!-- Live Log Output Stream Card (Terminal Mini Drawer) -->
              <div class="starting-log-terminal">
                <div class="terminal-bar">
                  <div class="terminal-dots">
                    <span class="t-dot red"></span>
                    <span class="t-dot yellow"></span>
                    <span class="t-dot green"></span>
                  </div>
                  <span class="terminal-title">
                    {{ t('实时引擎控制台输出 (最新动态)', 'Live Engine Stream (Latest Output)') }}
                  </span>
                  <button class="terminal-btn-logs" @click="currentTab = 'logs'">
                    {{ t('完整日志 ↗', 'Full Logs ↗') }}
                  </button>
                </div>
                <div class="terminal-body">
                  <div v-if="startupRecentLogs.length === 0" class="terminal-line placeholder">
                    <span class="terminal-prefix">[system]</span> {{ t('等待引擎与子服务首批就绪日志...', 'Waiting for engine initialization logs...') }}
                  </div>
                  <div v-for="(log, idx) in startupRecentLogs" :key="idx" class="terminal-line">
                    <span class="terminal-prefix" :class="'prefix-' + log.source">[{{ log.source }}]</span>
                    <span class="terminal-msg">{{ log.message }}</span>
                  </div>
                </div>
              </div>

              <!-- Friendly Tip and Interrupt Button -->
              <div class="starting-bottom-bar">
                <span class="starting-hint-text">
                  💡 {{ t('提示：模型文件约 9.71 GB，冷启动映射通常需要 10~15 秒；启动完成后将直接进入全功能智能体工作台。', 'Note: Model is 9.71 GB, initial loading takes ~10-15s. Full workspace will open automatically once ready.') }}
                </span>
                <div class="starting-btn-group">
                  <button class="btn-cancel-startup" @click="handleStop">
                    {{ t('中止启动', 'Cancel Launch') }}
                  </button>
                </div>
              </div>
            </div>
          </div>

          <!-- State C / D: Non-Running (Standby or Error) -> Modern Dedicated Standby Center -->
          <div v-else class="chat-viewport-stopped standby-center-viewport">
            <div class="standby-center-wrapper">

              <!-- 1. Error Banner (Only shown if status is Error) -->
              <div v-if="serviceState.status === 'Error'" class="standby-error-banner">
                <div class="standby-error-icon">⚠️</div>
                <div class="standby-error-content">
                  <div class="standby-error-title">{{ t('模型服务启动异常', 'Model Service Startup Error') }}</div>
                  <div class="standby-error-desc">{{ friendlyErrorMessage }}</div>
                </div>
                <div class="standby-error-actions">
                  <button class="standby-btn-retry" @click="handleStart">{{ t('重试启动', 'Retry Launch') }}</button>
                  <button class="standby-btn-subtle" @click="currentTab = 'vram'; fetchVramBreakdown();">
                    🔍 {{ t('显存诊断', 'Inspect VRAM') }}
                  </button>
                  <button class="standby-btn-subtle" @click="currentTab = 'logs'">
                    {{ t('查看日志', 'View Logs') }}
                  </button>
                </div>
              </div>

              <!-- 2. Hero Header Card -->
              <div class="standby-hero-card">
                <!-- Project Official Logo (Replacing the purple plus box) -->
                <div class="standby-project-logo-glow">
                  <svg class="standby-project-logo-svg" viewBox="0 0 100 100" fill="none">
                    <path d="M26 24C26 21.79 27.79 20 30 20H48C62.36 20 74 31.64 74 46C74 60.36 62.36 72 48 72H38V76C38 78.21 36.21 80 34 80H30C27.79 80 26 78.21 26 76V24Z" fill="url(#pStandbyPmodLogo)"/>
                    <path d="M38 32H48C55.73 32 62 38.27 62 46C62 53.73 55.73 60 48 60H38V32Z" fill="#090d16"/>
                    <path d="M50 38L57 46L50 54L43 46L50 38Z" fill="#ffffff"/>
                    <path d="M26 84C40 88 60 88 74 84" stroke="#38bdf8" stroke-width="4" stroke-linecap="round"/>
                    <defs>
                      <linearGradient id="pStandbyPmodLogo" x1="26" y1="20" x2="74" y2="80" gradientUnits="userSpaceOnUse">
                        <stop offset="0%" stop-color="#38bdf8"/>
                        <stop offset="50%" stop-color="#6366f1"/>
                        <stop offset="100%" stop-color="#a855f7"/>
                      </linearGradient>
                    </defs>
                  </svg>
                </div>

                <div class="standby-title-group">
                  <h2 class="standby-main-title">
                    {{ dshHeroTitle }}
                  </h2>
                  <p class="standby-subtitle">
                    {{ dshHeroChipText }}
                  </p>
                </div>

                <div class="standby-status-pill-wrap">
                  <span v-if="serviceState.status === 'Error'" class="standby-pill pill-error">
                    <span class="pill-dot dot-red"></span>
                    {{ t('启动异常 · 需要排查', 'Error · Inspection Needed') }}
                  </span>
                  <span v-else-if="!isModelConfigured" class="standby-pill pill-warning">
                    <span class="pill-dot dot-yellow"></span>
                    {{ t('未配置模型', 'Not Configured') }}
                  </span>
                  <span class="standby-pill pill-info" v-if="detectedGpuBadge">
                    {{ detectedGpuBadge }}
                  </span>
                  <span class="standby-pill pill-info" v-if="isModelConfigured">
                    {{ appConfig?.KVDType ? appConfig.KVDType.toUpperCase() : 'NVFP4' }} 量化
                  </span>
                </div>

                <!-- Primary Launch CTA (Beautiful, Sleek, Proportional Button) -->
                <div class="standby-cta-section">
                  <button
                    v-if="!isModelConfigured"
                    class="btn-standby-launch btn-launch-config"
                    @click="handlePickModel"
                  >
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="launch-btn-svg">
                      <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
                    </svg>
                    <span>{{ t('选择本地模型文件 (.ninfer)', 'Select Model File (.ninfer)') }}</span>
                  </button>

                  <button
                    v-if="!isModelConfigured"
                    class="btn-standby-download"
                    @click="openModelDownloadUrl"
                    :title="t('前往 Hugging Face 下载官方适配模型权重 (.ninfer)', 'Go to Hugging Face to download official models (.ninfer)')"
                  >
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="download-sub-svg">
                      <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                      <polyline points="7 10 12 15 17 10"></polyline>
                      <line x1="12" y1="15" x2="12" y2="3"></line>
                    </svg>
                    <span>{{ t('获取官方适配模型权重 (Hugging Face)', 'Download Model Weights (Hugging Face)') }}</span>
                  </button>

                  <button
                    v-else-if="modelMetadata && !modelMetadata.is_engine_compatible"
                    class="btn-standby-launch btn-launch-incompatible"
                    @click="currentTab = 'model'"
                  >
                    <span class="launch-btn-emoji">⚠️</span>
                    <span>{{ t('模型格式不匹配 (需 .ninfer) · 点击更换', 'Incompatible Format (Need .ninfer) · Change Model') }}</span>
                  </button>

                  <button
                    v-else
                    class="btn-standby-launch"
                    @click="handleStart"
                  >
                    <div class="btn-launch-icon-bubble">
                      <svg viewBox="0 0 24 24" fill="currentColor" class="launch-btn-svg">
                        <polygon points="7 4 19 12 7 20 7 4"></polygon>
                      </svg>
                    </div>
                    <span>{{ serviceState.status === 'Error' ? t('重试启动大模型', 'Retry Launching Model') : t('启动本地大模型', 'Start Local Model') }}</span>
                  </button>
                  <div class="standby-launch-hint">
                    {{ modelMetadata && !modelMetadata.is_engine_compatible ? t('当前选中的文件类型与 NInfer C++ 引擎不兼容，请点击上方按钮切换为 .ninfer 制品', 'Selected file is incompatible with NInfer engine. Please select a .ninfer artifact.') : t('冷启动首次映射约需 10 秒 · 启动后自动切换至全功能对话界面', 'Initial cold start takes ~10s · Automatically enters full chat interface once ready') }}
                  </div>
                </div>
              </div>

              <!-- 3. Hardware, Model & Engine, and Ecosystem Cards (3 Columns) -->
              <div class="standby-readiness-grid">
                <!-- Card 1: 硬件显存 (Hardware VRAM) -->
                <div class="standby-card" @click="currentTab = 'vram'; fetchVramBreakdown();">
                  <div class="standby-card-header">
                    <div class="standby-card-icon-box bg-blue">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <rect x="2" y="3" width="20" height="14" rx="2"></rect>
                        <line x1="8" y1="21" x2="16" y2="21"></line>
                        <line x1="12" y1="17" x2="12" y2="21"></line>
                      </svg>
                    </div>
                    <span class="standby-card-tag">{{ t('硬件显存', 'Hardware VRAM') }}</span>
                  </div>
                  <div class="standby-card-body">
                    <h4 class="standby-card-title">{{ detectedGpuShortName || t('独立显卡', 'Dedicated GPU') }}</h4>
                    <div class="standby-card-metric">
                      <span class="metric-big">{{ ((vramBreakdown?.free_mb || gpuStats?.free_mb || 0) / 1024).toFixed(1) }}</span>
                      <span class="metric-unit">GB {{ t('空闲', 'Free') }} / {{ detectedTotalVramGb || '12.0' }} GB</span>
                    </div>
                    <div class="standby-vram-bar">
                      <div class="standby-vram-fill" :style="{ width: vramPercent + '%', background: vramStatusColor }"></div>
                    </div>
                    <div class="standby-tags-row">
                      <span class="tech-badge">⚡ {{ t('专用显存充足', 'VRAM Sufficient') }}</span>
                      <span class="tech-badge">🛡️ {{ t('硬件加速就绪', 'Acceleration Ready') }}</span>
                    </div>
                    <p class="standby-card-desc">
                      {{ t('独显处于低能耗待命状态，显存准备充裕', 'GPU is in power-saving standby with ample VRAM') }}
                    </p>
                  </div>
                  <div class="standby-card-footer">
                    <span>{{ t('显存管理与进程调度', 'Inspect VRAM Allocation') }}</span>
                    <span class="card-arrow">→</span>
                  </div>
                </div>

                <!-- Card 2: 模型与引擎 (Dynamic Model & Engine Parameters) -->
                <div class="standby-card" :class="{ 'card-unconfigured': !isModelConfigured }" @click="currentTab = 'model'">
                  <div class="standby-card-header">
                    <div class="standby-card-icon-box" :class="isModelConfigured ? 'bg-purple' : 'bg-amber'">
                      <svg v-if="isModelConfigured" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
                        <polyline points="2 17 12 22 22 17"></polyline>
                        <polyline points="2 12 12 17 22 12"></polyline>
                      </svg>
                      <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
                        <line x1="12" y1="9" x2="12" y2="13"></line>
                        <line x1="12" y1="17" x2="12.01" y2="17"></line>
                      </svg>
                    </div>
                    <span class="standby-card-tag" :class="{ 'tag-warning': !isModelConfigured }">
                      {{ isModelConfigured ? t('模型与引擎', 'Model & Engine') : t('模型与引擎 · 待配置', 'Model · Not Configured') }}
                    </span>
                  </div>

                  <!-- Configured but Incompatible State -->
                  <div class="standby-card-body" v-if="isModelConfigured && modelMetadata && !modelMetadata.is_engine_compatible">
                    <h4 class="standby-card-title text-danger" :title="appConfig?.ModelPath">
                      {{ modelMetadata.filename }}
                    </h4>
                    <div class="standby-card-metric">
                      <span class="metric-warning-text text-danger">{{ t('⚠️ 格式不兼容 (需 .ninfer)', '⚠️ Incompatible: .ninfer required') }}</span>
                    </div>
                    <div class="standby-tags-row">
                      <span class="tech-badge badge-danger">{{ modelMetadata.format }} {{ t('不兼容', 'Incompatible') }}</span>
                      <span class="tech-badge">Ninfer C++</span>
                    </div>
                    <p class="standby-card-desc text-danger">
                      {{ modelMetadata.compatibility_notice }}
                    </p>
                  </div>

                  <!-- Configured State -->
                  <div class="standby-card-body" v-else-if="isModelConfigured">
                    <h4 class="standby-card-title" :title="appConfig?.ModelPath">
                      {{ modelMetadata?.architecture ? `${modelMetadata.architecture} ${modelMetadata.param_size} (${modelMetadata.quant_type})` : t('本地大模型 (已配置)', 'Local Model (Configured)') }}
                    </h4>
                    <div class="standby-card-metric">
                      <span class="metric-big">{{ Math.round((appConfig?.MaxContext || modelMetadata?.context_window || 262144) / 1024) }}</span>
                      <span class="metric-unit">K {{ t('长上下文', 'Context') }} · {{ appConfig?.ReasoningEffort ? (t('思考:', 'Effort:') + ' ' + appConfig.ReasoningEffort) : 'xhigh' }}</span>
                    </div>
                    <div class="standby-tags-row">
                      <span class="tech-badge">🧠 {{ appConfig?.KVDType ? appConfig.KVDType.toUpperCase() : 'NVFP4' }} KV</span>
                      <span class="tech-badge">💾 {{ appConfig?.HostKVMiB ? Math.round(appConfig.HostKVMiB / 1024) : 7 }}GB Host KV</span>
                      <span class="tech-badge">🚀 MTP3 {{ t('投机加速', 'Speculative') }}</span>
                    </div>
                    <p class="standby-card-desc">
                      {{ t('支持静态图预热与推测解码，大幅提升离线推理速度', 'CUDA graph warmup & speculative decoding boost inference speed') }}
                    </p>
                  </div>

                  <!-- Unconfigured State -->
                  <div class="standby-card-body" v-else>
                    <h4 class="standby-card-title text-amber">{{ t('尚未指定本地模型', 'No Model Configured') }}</h4>
                    <div class="standby-card-metric">
                      <span class="metric-warning-text">{{ t('请先配置模型权重', 'Please configure model weights') }}</span>
                    </div>
                    <p class="standby-card-desc">
                      {{ t('检测到尚未绑定 .ninfer 格式的模型文件，请点击下方前往配置。', 'No .ninfer model weights detected. Click below to configure.') }}
                    </p>
                  </div>

                  <div class="standby-card-footer">
                    <span>{{ isModelConfigured ? t('配置引擎参数', 'Configure Engine Settings') : t('前往配置模型', 'Go to Model Settings') }}</span>
                    <span class="card-arrow">→</span>
                  </div>
                </div>

                <!-- Card 3: 生态接口 (Ecosystem & API) -->
                <div class="standby-card" @click="switchToOverview">
                  <div class="standby-card-header">
                    <div class="standby-card-icon-box bg-emerald">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <circle cx="12" cy="12" r="10"></circle>
                        <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"></path>
                        <path d="M2 12h20"></path>
                      </svg>
                    </div>
                    <span class="standby-card-tag">{{ t('生态接口', 'API & Ecosystem') }}</span>
                  </div>
                  <div class="standby-card-body">
                    <h4 class="standby-card-title">{{ t('OpenAI 标准 API', 'OpenAI Standard API') }}</h4>
                    <div class="standby-card-metric">
                      <span class="metric-big">:{{ appConfig?.GuardPort || 1236 }}</span>
                      <span class="metric-unit">HTTP / SSE · 127.0.0.1</span>
                    </div>
                    <div class="standby-tags-row">
                      <span class="tech-badge">VS Code</span>
                      <span class="tech-badge">Cursor</span>
                      <span class="tech-badge">Claude Code</span>
                      <span class="tech-badge">OpenAI SDK</span>
                    </div>
                    <p class="standby-card-desc">
                      {{ t('内置本地安全代理网关，直接对接主流开发工具与客户端', 'Built-in local security proxy gateway for IDEs and CLI agents') }}
                    </p>
                  </div>
                  <div class="standby-card-footer">
                    <span>{{ t('查看推理性能看板', 'View Telemetry Dashboard') }}</span>
                    <span class="card-arrow">→</span>
                  </div>
                </div>
              </div>

              <!-- 4. Subtle Hint -->
              <div class="standby-bottom-bar">
                <span class="standby-hint-dot"></span>
                <span>{{ t('待命模式下显卡处于零功耗节能状态；点击上方「启动本地大模型」即可开启智能编程工作台。', 'GPU stays in power-saving standby; click "Start Local Model" above to launch workspace.') }}</span>
              </div>

            </div>
          </div>
        </div>

        <!-- ==============================================================
             PAGE 3: 显存管理 (VRAM & Dual-GPU Hybrid Process Management)
             ============================================================== -->
        <div v-show="currentTab === 'vram'" class="tab-page page-vram">
          <!-- Overview 4 Metrics -->
          <div class="vram-telemetry-grid">
            <div class="vram-card">
              <span class="vram-card-lbl">{{ t('总物理显存 (VRAM)', 'Total VRAM') }}</span>
              <span class="vram-card-num">
                {{ ((vramBreakdown?.total_mb || 12288) / 1024).toFixed(1) }} GB
              </span>
              <span class="vram-card-sub">{{ detectedGpuShortName ? `${detectedGpuShortName} ${t('独显', 'GPU')}` : t('专用显卡', 'Dedicated GPU') }}</span>
            </div>

            <div class="vram-card">
              <span class="vram-card-lbl">{{ t('当前系统总占用', 'Total VRAM Used') }}</span>
              <span class="vram-card-num" :style="{ color: vramStatusColor }">
                {{ ((vramBreakdown?.used_mb || 0) / 1024).toFixed(2) }} GB
              </span>
              <span class="vram-card-sub">
                {{ t('占显存容量 ', 'Takes ') }}{{ vramPercent }}%
              </span>
            </div>

            <div class="vram-card">
              <span class="vram-card-lbl">{{ t('当前剩余可用', 'Free Available') }}</span>
              <span class="vram-card-num">
                {{ ((vramBreakdown?.free_mb || 0) / 1024).toFixed(2) }} GB
              </span>
              <span class="vram-card-sub">
                {{ (vramBreakdown?.free_mb || 0) }} MB {{ t('空闲空间', 'Free Space') }}
              </span>
            </div>

            <div class="vram-card">
              <span class="vram-card-lbl">{{ t('当前列表应用占用', 'Current Apps VRAM') }}</span>
              <span class="vram-card-num" :style="{ color: totalAppVramMb > 1200 ? '#ea580c' : '#2563eb' }">
                {{ totalAppVramMb }} MB
              </span>
              <span class="vram-card-sub">
                {{ t('共 ', 'Total ') }}{{ userAppsCount }} {{ t('个非系统应用', ' non-system apps') }}
              </span>
            </div>
          </div>

          <!-- Dual-GPU Hybrid Topology Banner (点亮混合输出状态) -->
          <div v-if="isHybridGpuSupported" class="hybrid-gpu-banner">
            <div class="hybrid-banner-left">
              <img :src="andsiImg" class="hybrid-pulse-img" alt="andsi" />
              <div class="hybrid-banner-info">
                <div class="hybrid-banner-title">
                  <strong>{{ t('设备支持混合输出', 'Hybrid GPU Architecture Active (Optimus)') }}</strong>
                  <span class="hybrid-badge-live">{{ t('硬件级无损直调', 'Zero-Loss Dynamic Switching') }}</span>
                </div>
                <div class="hybrid-banner-desc">
                  <span>{{ dgpuName || t('独立显卡', 'Dedicated GPU') }} {{ detectedTotalVramGb ? `(${detectedTotalVramGb}GB 物理显存)` : '' }}</span>
                  <span class="hybrid-arrow-sep">⇄</span>
                  <span>{{ igpuName }} (动态共享内存)</span>
                </div>
              </div>
            </div>
            <div class="hybrid-banner-right">
              <button
                class="btn-batch-migrate"
                :disabled="batchMigrating"
                @click="selectedGpuTab === 'dgpu' ? handleBatchMigrateToIgpu() : handleBatchMigrateToDgpu()"
                :title="selectedGpuTab === 'dgpu' ? t('一键切换到核显', 'Batch migrate chat, media & browser apps to integrated GPU') : t('一键切换到独显', 'Batch switch non-system apps to dedicated GPU')"
              >
                <span v-if="batchMigrating" class="btn-spinner">⌛</span>
                <span v-else>{{ selectedGpuTab === 'dgpu' ? '⚡' : '🖥️' }}</span>
                <span>
                  {{
                    batchMigrating
                      ? t('正在批量调度优化...', 'Migrating Apps...')
                      : (selectedGpuTab === 'dgpu' ? t('一键切换到核显', 'Batch Switch to iGPU') : t('一键切换到独显', 'Batch Switch to dGPU'))
                  }}
                </span>
              </button>
            </div>
          </div>

          <!-- Main Dual-GPU Selection Tabs (独显应用 vs 核显应用 核心选项卡) -->
          <div class="gpu-arch-tabs" v-if="isHybridGpuSupported">
            <button
              class="gpu-tab-btn"
              :class="{ 'gpu-tab-active': selectedGpuTab === 'dgpu' }"
              @click="selectedGpuTab = 'dgpu'"
            >
              <div class="gpu-tab-indicator-bar" v-if="selectedGpuTab === 'dgpu'"></div>
              <img :src="gpuImg" class="gpu-tab-custom-icon" alt="gpu" />
              <div class="gpu-tab-label-group">
                <strong class="gpu-tab-title">{{ t('独显应用', 'Dedicated GPU (dGPU)') }}</strong>
                <span class="gpu-tab-sub">{{ detectedGpuBadge ? `${detectedGpuBadge} · 物理显存` : (dgpuName || t('独显物理显存', 'Dedicated GPU VRAM')) }}</span>
              </div>
              <span class="gpu-tab-count badge-dgpu">{{ dgpuAppsCount }}</span>
            </button>

            <button
              class="gpu-tab-btn"
              :class="{ 'gpu-tab-active': selectedGpuTab === 'igpu' }"
              @click="selectedGpuTab = 'igpu'"
            >
              <div class="gpu-tab-indicator-bar" v-if="selectedGpuTab === 'igpu'"></div>
              <img :src="cpuImg" class="gpu-tab-custom-icon" alt="cpu" />
              <div class="gpu-tab-label-group">
                <strong class="gpu-tab-title">{{ t('核显应用', 'Integrated GPU (iGPU)') }}</strong>
                <span class="gpu-tab-sub">{{ igpuName }} · 节能与共享内存</span>
              </div>
              <span class="gpu-tab-count badge-igpu">{{ igpuAppsCount }}</span>
            </button>
          </div>

          <!-- Diagnostic Advice Banner -->
          <div
            v-if="selectedGpuTab === 'dgpu' && vramBreakdown && vramBreakdown.used_mb > 1600 && serviceState.status === 'Stopped'"
            class="vram-notice-strip notice-warn"
          >
            <span class="notice-icon">⚠️</span>
            <div class="notice-text-group">
              <strong>{{ t('独显显存占用偏高提醒 (当前占用 ', 'High dedicated VRAM warning (currently used ') }}{{ ((vramBreakdown.used_mb) / 1024).toFixed(2) }} GB)</strong>
              <p>
                {{ t(
                  `${detectedModelDisplayName || '大模型'} 启动通常需要约 10 GB 专用显存。若遇到显存溢出报错，可直接点击列表中第三方应用右侧的「⚡ 迁移到核显」，系统将通过方案 A 优雅平滑重启该程序并将其移出独显，立即释放物理显存。`,
                  `${detectedModelDisplayName || 'Model'} startup requires ~10 GB dedicated VRAM. Click "Migrate to iGPU" on any background app to smoothly restart it on integrated GPU, immediately freeing VRAM.`
                ) }}
              </p>
            </div>
          </div>
          <div
            v-else-if="selectedGpuTab === 'igpu'"
            class="vram-notice-strip notice-igpu"
          >
            <span class="notice-icon">💡</span>
            <div class="notice-text-group">
              <strong>{{ t('核显应用托管池 (Intel Graphics 节能共享模式)', 'Integrated GPU Managed Apps (Intel Graphics)') }}</strong>
              <p>
                {{ t(`运行在核显上的程序完全不占用 ${detectedGpuShortName || '独显'} 的物理显存。如果您需要让某款设计软件或游戏恢复独显极致性能，可点击右侧的「🖥️ 迁移到独显」。`, `Apps running on iGPU consume 0 dedicated VRAM. Click "Migrate to dGPU" if you wish to run a heavy 3D app on dedicated GPU.`) }}
              </p>
            </div>
          </div>

          <!-- Table Controls Toolbar -->
          <div class="vram-action-toolbar">
            <div class="vram-tabs-group">
              <button
                class="vram-filter-pill"
                :class="{ active: vramCategoryFilter === 'all' }"
                @click="vramCategoryFilter = 'all'"
              >
                {{ t('全部应用', 'All Apps') }} ({{ currentProcessList.length }})
              </button>
              <button
                class="vram-filter-pill"
                :class="{ active: vramCategoryFilter === 'user' }"
                @click="vramCategoryFilter = 'user'"
              >
                {{ t('非系统应用', 'User Apps') }} ({{ userAppsCount }})
              </button>
              <button
                class="vram-filter-pill"
                :class="{ active: vramCategoryFilter === 'browser' }"
                @click="vramCategoryFilter = 'browser'"
              >
                {{ t('浏览器', 'Browser') }}
              </button>
              <button
                class="vram-filter-pill"
                :class="{ active: vramCategoryFilter === 'im' || vramCategoryFilter === 'chat' }"
                @click="vramCategoryFilter = 'im'"
              >
                {{ t('通讯社交', 'Chat / Social') }}
              </button>
              <button
                class="vram-filter-pill"
                :class="{ active: vramCategoryFilter === 'media' }"
                @click="vramCategoryFilter = 'media'"
              >
                {{ t('影音流媒体', 'Media') }}
              </button>
              <button
                class="vram-filter-pill"
                :class="{ active: vramCategoryFilter === 'system' }"
                @click="vramCategoryFilter = 'system'"
              >
                {{ t('系统组件', 'System') }}
              </button>
            </div>

            <div class="vram-tools-right">
              <input
                type="text"
                class="vram-search-field"
                v-model="vramSearchQuery"
                :placeholder="t('搜索应用名 / PID...', 'Search app / PID...')"
              />
              <button class="btn-tool-subtle" :class="{ 'is-spinning': isVramLoading }" @click="fetchVramBreakdown" :title="t('刷新采样', 'Refresh Sampling')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="tool-svg">
                  <path d="M23 4v6h-6"></path>
                  <path d="M1 20v-6h6"></path>
                  <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
                </svg>
                <span>{{ isVramLoading ? t('采样中...', 'Sampling...') : t('刷新', 'Refresh') }}</span>
              </button>
              <button class="btn-tool-subtle" @click="copyVramReport">
                📋 {{ t('复制报告', 'Copy Report') }}
              </button>
            </div>
          </div>

          <!-- Ranked Processes Table -->
          <div class="vram-table-card">
            <table class="vram-data-table">
              <thead>
                <tr>
                  <th style="width: 55px;">排名</th>
                  <th>应用程序 / 进程名</th>
                  <th style="width: 130px; white-space: nowrap;">{{ t('类型', 'Type') }}</th>
                  <th style="width: 80px;">PID</th>
                  <th style="width: 150px;">
                    {{ selectedGpuTab === 'dgpu' ? t('独显物理显存占用', 'Dedicated VRAM') : t('核显共享内存占用', 'iGPU Shared Memory') }}
                  </th>
                  <th style="width: 140px;">
                    {{ selectedGpuTab === 'dgpu' ? t('占 12GB 显存比例', 'Ratio (12GB)') : t('图形首选项状态', 'GPU Preference') }}
                  </th>
                  <th style="width: 220px; text-align: right;">{{ t('显卡调度 / 操作', 'GPU Switch / Action') }}</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="(proc, index) in filteredProcesses"
                  :key="proc.pid + proc.name"
                  :class="{ 'tr-engine': proc.category === 'engine' }"
                >
                  <td class="td-rank" :class="{ 'td-rank-top': index < 3 }">
                    #{{ index + 1 }}
                  </td>
                  <td>
                    <div class="proc-name-cell">
                      <span class="td-proc-name">{{ proc.name }}</span>
                      <span v-if="proc.exe_path" class="td-proc-path" :title="proc.exe_path">{{ proc.exe_path }}</span>
                    </div>
                  </td>
                  <td>
                    <span class="cat-pill" :class="getCategoryBadgeClass(proc.category)">
                      {{ getCategoryLabel(proc.category) }}
                    </span>
                  </td>
                  <td class="td-pid">
                    <code>{{ proc.pid }}</code>
                  </td>
                  <td>
                    <strong class="td-vram-mb">{{ proc.vram_mb }} MB</strong>
                    <span v-if="selectedGpuTab === 'igpu'" class="sub-shared-tag">共享</span>
                  </td>
                  <td>
                    <!-- If dGPU: ratio bar -->
                    <div v-if="selectedGpuTab === 'dgpu'" class="ratio-bar-box">
                      <div class="ratio-track">
                        <div
                          class="ratio-fill"
                          :class="{ 'fill-orange': proc.vram_mb > 150 }"
                          :style="{ width: Math.min(100, Math.max(4, (proc.vram_mb / 12288) * 100 * 4)) + '%' }"
                        ></div>
                      </div>
                      <span class="ratio-text">{{ ((proc.vram_mb / 12288) * 100).toFixed(1) }}%</span>
                    </div>

                    <!-- If iGPU: show preference tag -->
                    <div v-else class="pref-badge-group">
                      <span v-if="proc.preference === 1" class="pref-pill pill-igpu" :title="t('已被用户设置为强制使用核显节能模式', 'Forced to iGPU')">
                        ⚡ {{ t('强制核显', 'Forced iGPU') }}
                      </span>
                      <span v-else-if="proc.preference === 2" class="pref-pill pill-dgpu" :title="t('已被用户设置为强制独显高性能模式', 'Forced to dGPU')">
                        🖥️ {{ t('独显高性能', 'dGPU High Perf') }}
                      </span>
                      <span v-else class="pref-pill pill-default">
                        {{ t('系统默认', 'Default') }}
                      </span>
                    </div>
                  </td>
                  <td style="text-align: right;">
                    <!-- System / Protected / Engine Labels -->
                    <span v-if="proc.category === 'client'" class="badge-client-protect" :title="t('当前工作台界面客户端', 'Current Studio Client')">
                      {{ t('工作台(保护)', 'Client Studio') }}
                    </span>
                    <span v-else-if="proc.is_critical_system" class="badge-sys-lock" :title="t('Windows 核心系统服务', 'Windows Core Service')">
                      {{ t('系统保护', 'System Lock') }}
                    </span>
                    <span v-else-if="proc.category === 'engine'" class="badge-engine-tag">
                      {{ t('AI 引擎核心', 'AI Engine Core') }}
                    </span>

                    <!-- ACTION BUTTONS FOR THIRD-PARTY APPS -->
                    <div v-else class="table-action-btns">
                      <!-- 1. IF CURRENT TAB IS dGPU: Offer Migrate to iGPU (Option A: Smooth Restart) -->
                      <template v-if="selectedGpuTab === 'dgpu'">
                        <button
                          class="btn-switch-gpu to-igpu"
                          :disabled="isMigratingPid === proc.pid"
                          @click="handleMigrateGpu(proc, 'igpu')"
                          :title="t('写入注册表首选项并优雅平滑重启应用，立即释放独显物理显存', 'Option A: Smooth restart application on integrated GPU to free dedicated VRAM')"
                        >
                          <span v-if="isMigratingPid === proc.pid" class="btn-spinner">⌛</span>
                          <span v-else>⚡</span>
                          <span>{{ isMigratingPid === proc.pid ? t('平滑重启中...', 'Restarting...') : t('迁移到核显', 'Migrate to iGPU') }}</span>
                        </button>

                        <button
                          class="btn-kill-app-mini"
                          :disabled="terminatingPid === proc.pid"
                          @click="handleKillProcess(proc)"
                          :title="t('彻底终止此应用进程', 'Terminate process')"
                        >
                          {{ terminatingPid === proc.pid ? '...' : t('关闭', 'Kill') }}
                        </button>
                      </template>

                      <!-- 2. IF CURRENT TAB IS iGPU: Offer Migrate Back to dGPU or Reset -->
                      <template v-else>
                        <button
                          class="btn-switch-gpu to-dgpu"
                          :disabled="isMigratingPid === proc.pid"
                          @click="handleMigrateGpu(proc, 'dgpu')"
                          :title="t('将此应用切换回独显高性能模式并平滑重启', 'Switch app to dedicated high performance GPU and restart')"
                        >
                          <span v-if="isMigratingPid === proc.pid" class="btn-spinner">⌛</span>
                          <span v-else>🖥️</span>
                          <span>{{ isMigratingPid === proc.pid ? t('重启中...', 'Restarting...') : t('迁移到独显', 'Migrate to dGPU') }}</span>
                        </button>

                        <button
                          v-if="proc.preference === 1 || proc.preference === 2"
                          class="btn-reset-pref-mini"
                          @click="handleResetPreference(proc)"
                          :title="t('清除自定义首选项，恢复系统自动管理', 'Clear preference and restore system default')"
                        >
                          ↺
                        </button>
                      </template>
                    </div>
                  </td>
                </tr>

                <tr v-if="filteredProcesses.length === 0">
                  <td colspan="7" class="table-empty-row">
                    {{ isVramLoading ? t("正在采集显存计数器数据...", "Collecting VRAM telemetry...") : t("未找到匹配的进程", "No matching processes found") }}
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          <div class="vram-bottom-tip" v-if="copySuccessNotice">
            <span class="copy-toast">✓ {{ copySuccessNotice }}</span>
          </div>
        </div>

        <!-- ==============================================================
             PAGE 4: 模型与引擎参数 (Model & Config)
             ============================================================== -->
        <!-- ==============================================================
             PAGE 4: 模型与计算引擎调度中心 (Model & Universal Engine Orchestrator)
             ============================================================== -->
        <!-- ==============================================================
             PAGE 4: 模型与计算引擎调度中心 (Model & Universal Engine Orchestrator)
             ============================================================== -->
                <!-- ==============================================================
             PAGE 4: 模型与计算引擎调度中心 (Model & Universal Engine Orchestrator)
             ============================================================== -->
        <!-- ==============================================================
             PAGE 4: 模型与计算引擎调度中心 (Model & Universal Engine Orchestrator)
             ============================================================== -->
        <div v-show="currentTab === 'model'" class="tab-page page-model">
          <!-- 1. Top Studio Banner (Harmonized with Telemetry & VRAM Tabs) -->
          <div class="model-top-banner">
            <div class="banner-left">
              <div class="banner-icon-squircle">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="banner-svg">
                  <rect x="4" y="4" width="16" height="16" rx="2"></rect>
                  <rect x="9" y="9" width="6" height="6"></rect>
                  <line x1="9" y1="1" x2="9" y2="4"></line>
                  <line x1="15" y1="1" x2="15" y2="4"></line>
                  <line x1="9" y1="20" x2="9" y2="23"></line>
                  <line x1="15" y1="20" x2="15" y2="23"></line>
                  <line x1="20" y1="9" x2="23" y2="9"></line>
                  <line x1="20" y1="14" x2="23" y2="14"></line>
                  <line x1="1" y1="9" x2="4" y2="9"></line>
                  <line x1="1" y1="14" x2="4" y2="14"></line>
                </svg>
              </div>
              <div class="banner-text">
                <div class="banner-title-row">
                  <h2 class="banner-title">{{ t('引擎配置', 'Engine Configuration') }}</h2>
                  <span class="status-live-pill" :class="serviceState.status === 'Running' ? 'live-online' : 'live-standby'">
                    <span class="pulse-dot"></span>
                    {{ serviceState.status === 'Running' ? t('服务运行中', 'Engine Online') : t('引擎就绪待命', 'Engine Standby') }}
                  </span>
                </div>
                <p class="banner-desc">
                  {{ t('Qwen 27B · 专属 NInfer C++ 极速引擎与 KVMem 显卡显存 / 宿主机内存分层动态调度架构', 'Qwen 27B · NInfer C++ Engine with KVMem GPU VRAM & Host RAM Tiered Architecture') }}
                </p>
              </div>
            </div>

            <div class="banner-actions">
              <button class="btn-banner-subtle" @click="handleResetDefaults" :title="t('恢复系统出厂基准参数配置', 'Reset to default parameters')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="action-btn-svg">
                  <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"></path>
                  <path d="M3 3v5h5"></path>
                </svg>
                <span>{{ t('恢复默认', 'Reset') }}</span>
              </button>


              <button class="btn-banner-primary" @click="handleSaveConfig(false)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="action-btn-svg">
                  <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"></path>
                  <polyline points="17 21 17 13 7 13 7 21"></polyline>
                  <polyline points="7 3 7 8 15 8"></polyline>
                </svg>
                <span>{{ t('保存配置', 'Save Settings') }}</span>
              </button>

              <button v-if="serviceState.status === 'Running'" class="btn-banner-accent" @click="handleSaveConfig(true)" :title="t('保存新配置并立即平滑重启模型引擎', 'Save settings & restart model engine')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="action-btn-svg">
                  <path d="M23 4v6h-6"></path>
                  <path d="M1 20v-6h6"></path>
                  <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
                </svg>
                <span>{{ t('保存并重启', 'Save & Restart') }}</span>
              </button>
            </div>
          </div>

          <!-- Save Toast Notice -->
          <transition name="toast-fade">
            <div class="model-save-toast" v-if="configSaveNotice">
              <span class="toast-check-icon">✓</span>
              <span>{{ configSaveNotice }}</span>
            </div>
          </transition>

          <!-- Studio Alert: Unconfigured Model Guidance -->
          <transition name="alert-slide">
            <div v-if="modelNotConfiguredWarning || !isModelConfigured" class="studio-setup-banner">
              <div class="banner-left-glow"></div>
              <div class="banner-content">
                <div class="banner-icon-wrapper">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" class="banner-svg-icon">
                    <circle cx="12" cy="12" r="10"></circle>
                    <line x1="12" y1="8" x2="12" y2="12"></line>
                    <line x1="12" y1="16" x2="12.01" y2="16"></line>
                  </svg>
                </div>
                <div class="banner-text-content">
                  <div class="banner-header-line">
                    <h4 class="banner-headline">{{ t('尚未配置本地大模型权重文件', 'No Local Model File Configured') }}</h4>
                    <span class="banner-status-tag">{{ t('初始引导 · 待配置', 'Setup Required') }}</span>
                  </div>
                  <p class="banner-subtext">
                    {{ t('请点击下方「浏览本地文件...」或右侧快捷按钮选择已下载的大模型权重（*.ninfer），系统将自动穿透二进制解析规格并就绪待命。', 'Please click "Browse..." below or the button on the right to select a local model file (*.ninfer) to enable local inference.') }}
                  </p>
                </div>
              </div>
              <div class="banner-action-area">
                <button class="banner-cta-button" @click="handlePickModel">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-cta-svg">
                    <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
                    <polyline points="12 11 12 17"></polyline>
                    <polyline points="9 14 12 11 15 14"></polyline>
                  </svg>
                  <span>{{ t('浏览选择模型', 'Browse Model File') }}</span>
                </button>
              </div>
            </div>
          </transition>

          <!-- Studio Alert: GPU Driver Auto-Detection Alert Banner -->
          <transition name="alert-slide">
            <div v-if="isGpuArchMismatch || isCudaDriverOutdated" class="studio-setup-banner arch-mismatch-banner">
              <div class="banner-left-glow glow-rose"></div>
              <div class="banner-content">
                <div class="banner-icon-wrapper icon-rose">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" class="banner-svg-icon">
                    <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
                    <line x1="12" y1="9" x2="12" y2="13"></line>
                    <line x1="12" y1="17" x2="12.01" y2="17"></line>
                  </svg>
                </div>
                <div class="banner-text-content">
                  <div class="banner-header-line">
                    <h4 class="banner-headline">
                      {{
                        isGpuArchMismatch
                          ? t('自动检测提醒：本机显卡非默认 RTX 50 系列，请配置匹配的推理引擎驱动', 'Auto-Detection Alert: GPU Is Not RTX 50 Series — Please Configure Matching Engine Driver')
                          : t('自动检测提醒：本机 NVIDIA 显卡驱动版本低于 CUDA 13 要求', 'Auto-Detection Alert: NVIDIA Driver Below CUDA 13 Requirement')
                      }}
                    </h4>
                    <span class="banner-status-tag tag-rose">{{ t('自动检测 · 需配置驱动', 'Driver Setup Required') }}</span>
                  </div>
                  <p class="banner-subtext">
                    {{
                      isGpuArchMismatch
                        ? t(
                            `系统启动自动检测到本机显卡为「${dgpuName || detectedGpuShortName}」(${detectedArchFullLabel})，而当前使用的是系统自带的 RTX 50 系列 (sm_120a) 默认引擎驱动。请点击右侧「立即配置引擎驱动」或在下方指定适配本机显卡架构的引擎文件 (*.exe)。`,
                            `Auto-detected GPU "${dgpuName || detectedGpuShortName}" (${detectedArchFullLabel}) does not match the built-in RTX 50 Series (sm_120a) default driver. Please configure a matching engine binary (*.exe).`
                          )
                        : t(
                            `检测到本机 NVIDIA 显卡驱动版本为 ${dgpuDriver}，低于 CUDA 13 引擎所需的 ≥ 570.00，请升级 NVIDIA 官方显卡驱动后再启动模型。`,
                            `Detected NVIDIA driver ${dgpuDriver} is older than required 570.00 for CUDA 13. Please update your NVIDIA driver.`
                          )
                    }}
                  </p>
                </div>
              </div>
              <div class="banner-action-area" v-if="isGpuArchMismatch">
                <button class="banner-cta-button btn-cta-rose" @click="handlePickEngine">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-cta-svg">
                    <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"></path>
                  </svg>
                  <span>{{ t('立即配置引擎驱动', 'Configure Engine Driver') }}</span>
                </button>
              </div>
            </div>
          </transition>

          <!-- 2. Main Studio Configuration Card Container -->
          <div class="model-cards-stack" v-if="appConfig">
            <!-- Card Section A: Model Artifact & Architecture Detection -->
            <div class="studio-card">
              <div class="studio-card-head">
                <div class="head-left">
                  <div class="head-icon-box bg-blue-subtle text-blue">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                      <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
                      <polyline points="2 17 12 22 22 17"></polyline>
                      <polyline points="2 12 12 17 22 12"></polyline>
                    </svg>
                  </div>
                  <div>
                    <h3 class="studio-card-title">{{ t('模型配置', 'Model Configuration') }}</h3>
                    <span class="studio-card-sub">{{ t('支持指定 *.ninfer 标准大模型制品，系统将自动穿透二进制解析架构、规模及量化精度', 'Specify *.ninfer model weights; architecture, scale, and quantization are automatically inspected.') }}</span>
                  </div>
                </div>
              </div>

              <div class="studio-card-body">
                <!-- Path Picker Row -->
                <div class="studio-path-box">
                  <div class="path-input-group">
                    <div class="path-icon-affix">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="folder-svg">
                        <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
                      </svg>
                    </div>
                    <input
                      type="text"
                      class="path-text-input"
                      v-model="appConfig.ModelPath"
                      @input="handleModelPathInput"
                      :placeholder="t('点击右侧「浏览本地文件...」选择本地模型权重 (*.ninfer)', 'Select local model file path (*.ninfer)')"
                    />
                    <button v-if="appConfig.ModelPath" class="path-btn-clear" @click="clearModelPath" :title="t('清除路径', 'Clear')">✕</button>
                    <button class="path-btn-browse" @click="handlePickModel">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="browse-svg">
                        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                        <polyline points="17 8 12 3 7 8"></polyline>
                        <line x1="12" y1="3" x2="12" y2="15"></line>
                      </svg>
                      <span>{{ t('浏览本地文件...', 'Browse File...') }}</span>
                    </button>
                  </div>
                </div>

                <!-- Parsed Model Spec Inspection Panel (Tile Grid) -->
                <div class="spec-inspection-panel" v-if="parsedModelMeta.isConfigured">
                  <div class="inspection-top-row">
                    <div class="inspection-left">
                      <span class="inspection-live-dot">●</span>
                      <strong class="inspection-badge-title">{{ t('解析到模型：', 'Detected Model:') }}</strong>
                      <span class="inspection-filename" :title="parsedModelMeta.filename">{{ parsedModelMeta.filename }}</span>
                    </div>
                    <span class="inspection-format-pill">{{ parsedModelMeta.format }} {{ t('专属封装', 'Format') }}</span>
                  </div>

                  <div class="inspection-tiles-grid">
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('识别架构 (Architecture)', 'Architecture') }}</span>
                      <span class="tile-value text-indigo">{{ parsedModelMeta.arch }}</span>
                    </div>
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('参数体量 (Param Size)', 'Parameter Scale') }}</span>
                      <span class="tile-value text-blue">{{ parsedModelMeta.params }}</span>
                    </div>
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('量化精度 (Quantization)', 'Quantization') }}</span>
                      <span class="tile-value text-emerald">{{ parsedModelMeta.quant }}</span>
                    </div>
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('模型服务标识 (Model ID)', 'Model Identifier') }}</span>
                      <span class="tile-value text-slate tile-value-id" :title="appConfig?.ModelId || parsedModelMeta.name || 'qwen-local'">
                        {{ appConfig?.ModelId || parsedModelMeta.name || 'qwen-local' }}
                      </span>
                    </div>
                  </div>

                  <div class="inspection-meta-strip">
                    <span class="meta-strip-badge">
                      <span class="badge-k">{{ t('隐藏层数', 'Layers') }}</span>
                      <strong class="badge-v">{{ parsedModelMeta.layer_count || 64 }}</strong>
                    </span>
                    <span class="meta-strip-badge">
                      <span class="badge-k">{{ t('隐藏维度', 'Hidden Dim') }}</span>
                      <strong class="badge-v">{{ parsedModelMeta.hidden_size || 5120 }}</strong>
                    </span>
                    <span class="meta-strip-badge">
                      <span class="badge-k">{{ t('全域上下文', 'Max Context') }}</span>
                      <strong class="badge-v text-indigo">{{ Math.round((parsedModelMeta.context_window || 262144) / 1024) }}K ({{ (parsedModelMeta.context_window || 262144).toLocaleString() }})</strong>
                    </span>
                  </div>
                </div>
              </div>
            </div>

            <!-- Card Section A2: Automatic GPU Hardware Detection & Engine Driver Configuration -->
            <div class="studio-card" :class="{ 'card-arch-mismatch': isGpuArchMismatch }">
              <div class="studio-card-head">
                <div class="head-left">
                  <div class="head-icon-box" :class="isGpuArchMismatch ? 'bg-amber-subtle text-amber' : 'bg-emerald-subtle text-emerald'">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                      <rect x="2" y="4" width="20" height="16" rx="2"></rect>
                      <path d="M6 8h4v4H6z"></path>
                      <path d="M14 8h4v4h-4z"></path>
                      <line x1="6" y1="16" x2="18" y2="16"></line>
                    </svg>
                  </div>
                  <div>
                    <h3 class="studio-card-title">{{ t('显卡驱动配置', 'GPU Driver Configuration') }}</h3>
                    <span class="studio-card-sub">{{ t('打开软件自动检测本机显卡架构与 NVIDIA 驱动；默认自带 RTX 50 系列 (sm_120a) 驱动，非 50 系显卡请在此配置对应驱动文件', 'Automatically detects GPU architecture & CUDA driver on startup. Built-in driver targets RTX 50 Series (sm_120a).') }}</span>
                  </div>
                </div>
                <div class="head-right-status">
                  <span v-if="!isGpuArchMismatch" class="arch-status-pill pill-arch-ok">
                    <span class="arch-dot dot-ok"></span>
                    <span>
                      {{
                        isUsingDefaultBuiltInDriver
                          ? t(`自动检测通过 · 已匹配默认 50 系驱动 (${detectedGpuShortName || 'RTX 50 系列'})`, `Auto-Detected · Matched Default RTX 50 Driver`)
                          : t(`已配置自定义适配驱动 (${detectedGpuShortName || '就绪'})`, `Custom Engine Driver Configured`)
                      }}
                    </span>
                  </span>
                  <span v-else class="arch-status-pill pill-arch-warn">
                    <span class="arch-dot dot-warn"></span>
                    <span>{{ t(`检测到非 50 系显卡 (${detectedGpuShortName || '其他系列'}) · 请配置驱动`, `Non-50 Series Detected (${detectedGpuShortName}) · Configure Driver`) }}</span>
                  </span>
                </div>
              </div>

              <div class="studio-card-body">
                <!-- Engine Driver Path Picker Row (Matches Model Picker Style) -->
                <div class="studio-path-box">
                  <div class="path-input-group">
                    <div class="path-icon-affix">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="folder-svg">
                        <rect x="4" y="4" width="16" height="16" rx="2"></rect>
                        <rect x="9" y="9" width="6" height="6"></rect>
                      </svg>
                    </div>
                    <input
                      type="text"
                      class="path-text-input"
                      v-model="appConfig.EnginePath"
                      placeholder="runtime/engine/ninfer-serve.exe"
                    />
                    <button
                      v-if="!isUsingDefaultBuiltInDriver"
                      type="button"
                      class="btn-engine-reset"
                      style="margin-right: 6px;"
                      @click="resetEnginePath"
                      :title="t('恢复系统自带的 RTX 50 系列默认引擎驱动', 'Restore built-in RTX 50 series engine driver')"
                    >
                      {{ t('恢复默认 50 系驱动', 'Restore Default') }}
                    </button>
                    <button type="button" class="path-btn-browse" @click="handlePickEngine">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="browse-svg">
                        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                        <polyline points="17 8 12 3 7 8"></polyline>
                        <line x1="12" y1="3" x2="12" y2="15"></line>
                      </svg>
                      <span>{{ t('配置/更换引擎驱动 (.exe)...', 'Configure Driver (.exe)...') }}</span>
                    </button>
                  </div>
                </div>

                <!-- Auto-Detected Hardware & Driver Spec Inspection Panel (4 Tiles) -->
                <div class="spec-inspection-panel" style="margin-top: 12px;">
                  <div class="inspection-tiles-grid">
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('本机实测显卡 (Detected GPU)', 'Detected GPU') }}</span>
                      <span class="tile-value text-blue" :title="dgpuName">{{ detectedGpuShortName || dgpuName || t('正在检测显卡...', 'Detecting...') }}</span>
                    </div>
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('实测硬件架构 (Compute Arch)', 'Compute Architecture') }}</span>
                      <span class="tile-value text-indigo">{{ detectedArchFullLabel }}</span>
                    </div>
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('NVIDIA 显卡驱动 (Driver)', 'NVIDIA Driver') }}</span>
                      <span class="tile-value" :class="isCudaDriverOutdated ? 'text-danger' : 'text-emerald'">
                        {{ dgpuDriver ? `${dgpuDriver} (${isCudaDriverOutdated ? t('需升级 >=570', 'Update >=570') : 'CUDA 13 ✓'})` : t('已就绪', 'Ready') }}
                      </span>
                    </div>
                    <div class="spec-tile">
                      <span class="tile-label">{{ t('当前生效引擎驱动 (Active Driver)', 'Active Engine Driver') }}</span>
                      <span class="tile-value" :class="isGpuArchMismatch ? 'text-amber' : 'text-emerald'">
                        {{
                          isGpuArchMismatch
                            ? t('⚠️ 需配置适配驱动', '⚠️ Driver Setup Needed')
                            : isUsingDefaultBuiltInDriver
                              ? t('系统内置 50 系驱动', 'Built-in RTX 50 Driver')
                              : t('已加载自定义驱动', 'Custom Driver Bound')
                        }}
                      </span>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Card Section B: Memory Tiering & Context Parameters (2x2 Grid) -->
            <div class="studio-card">
              <div class="studio-card-head">
                <div class="head-left">
                  <div class="head-icon-box bg-purple-subtle text-purple">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                      <path d="M22 12h-4l-3 9L9 3l-3 9H2"></path>
                    </svg>
                  </div>
                  <div>
                    <h3 class="studio-card-title">{{ t('显存 / 内存分层 KV 缓存调度', 'Layered KV Cache & Paging Pool Scheduling') }}</h3>
                    <span class="studio-card-sub">{{ t('由显卡原生物理显存与宿主机内存 (Host RAM) 协同驱动，实现超长上下文零溢出保障', 'Physical GPU VRAM & Host System RAM collaborate dynamically to guarantee zero-OOM on extreme contexts.') }}</span>
                  </div>
                </div>
              </div>

              <div class="studio-card-body">
                <div class="studio-params-grid">
                  <!-- Col 1: KVDType (Custom Segmented Control - Sleek & Modern) -->
                  <div class="param-card-cell">
                    <div class="param-cell-head">
                      <label class="param-cell-title">{{ t('KV 缓存量化精度格式 (KVDType)', 'KV Cache Quantization Precision (KVDType)') }}</label>
                      <span class="param-cell-tag text-blue">{{ appConfig.KVDType === 'rk4v4' ? t('4-Bit 压缩', '4-Bit') : t('FP4 原生', 'FP4') }}</span>
                    </div>

                    <div class="kvd-dual-selector">
                      <div
                        class="kvd-dual-card"
                        :class="{ active: appConfig.KVDType === 'rk4v4' }"
                        @click="appConfig.KVDType = 'rk4v4'"
                      >
                        <div class="dual-top">
                          <div class="dual-title-group">
                            <span class="dual-radio-indicator"></span>
                            <strong class="dual-title">rk4v4</strong>
                          </div>
                          <span class="dual-pill-badge badge-blue">{{ t('4-Bit 压缩格式', '4-Bit Compression') }}</span>
                        </div>
                        <p class="dual-desc">{{ t('显存占用极小，上下文吞吐量高，适合长文本与多轮会话。', 'Minimal VRAM footprint, high throughput. Suitable for long-context sessions.') }}</p>
                      </div>

                      <div
                        class="kvd-dual-card"
                        :class="{ active: appConfig.KVDType === 'nvfp4' }"
                        @click="appConfig.KVDType = 'nvfp4'"
                      >
                        <div class="dual-top">
                          <div class="dual-title-group">
                            <span class="dual-radio-indicator"></span>
                            <strong class="dual-title">nvfp4</strong>
                          </div>
                          <span class="dual-pill-badge badge-emerald">{{ t('NVIDIA 原生 FP4', 'Native Tensor FP4') }}</span>
                        </div>
                        <p class="dual-desc">{{ t('硬件 TensorCore 原生微架构加速，兼备高保真数值精度与超快解码速度。', 'Native TensorCore FP4 microarchitecture. High numerical fidelity and fast decode.') }}</p>
                      </div>
                    </div>
                  </div>

                  <!-- Col 2: DeviceKV -->
                  <div class="param-card-cell">
                    <div class="param-cell-head">
                      <label class="param-cell-title">{{ t('显卡专属 KV 显存池容量 (DeviceKV)', 'Dedicated GPU KV Cache Pool (DeviceKV)') }}</label>
                      <span class="param-cell-tag text-purple">KV {{ Math.round((appConfig.DeviceKV || 28672) / 1024) }}K (≈ {{ estimatedKvVramGb }} GB)</span>
                    </div>

                    <div class="studio-input-wrap">
                      <input type="number" class="studio-text-input" v-model.number="appConfig.DeviceKV" placeholder="28672" />
                      <span class="studio-input-unit">Tokens / 令牌</span>
                    </div>

                    <div class="studio-pills-row">
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.DeviceKV === 16384 }" @click="appConfig.DeviceKV = 16384">16K (16384)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.DeviceKV === 24576 }" @click="appConfig.DeviceKV = 24576">24K (24576)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.DeviceKV === 28672 }" @click="appConfig.DeviceKV = 28672">28K (28672)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.DeviceKV === 32768 }" @click="appConfig.DeviceKV = 32768">32K (32768)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.DeviceKV === 36864 }" @click="appConfig.DeviceKV = 36864">36K (36864)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.DeviceKV === 39168 }" @click="appConfig.DeviceKV = 39168">39K (39168)</button>
                    </div>

                    <p class="param-cell-note">
                      {{ t('驻留在独立显卡物理显存中的活跃注意力容量，直接影响多轮对话的零延迟命中率。', 'Active attention capacity residing in GPU VRAM for instant zero-latency recall.') }}
                    </p>
                  </div>

                  <!-- Col 3: HostKVMiB -->
                  <div class="param-card-cell">
                    <div class="param-cell-head">
                      <label class="param-cell-title">{{ t('宿主机内存 KV 分页缓冲池 (HostKVMiB)', 'Host System RAM KV Paging Pool (HostKVMiB)') }}</label>
                      <span class="param-cell-tag text-emerald">{{ ((appConfig.HostKVMiB || 0) / 1024).toFixed(1) }} GB RAM</span>
                    </div>

                    <div class="studio-input-wrap">
                      <input type="number" class="studio-text-input" v-model.number="appConfig.HostKVMiB" placeholder="7168" />
                      <span class="studio-input-unit">MB (= {{ ((appConfig.HostKVMiB || 0) / 1024).toFixed(1) }} GB)</span>
                    </div>

                    <div class="studio-pills-row">
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.HostKVMiB === 4096 }" @click="setHostKVMiB(4096)">4 GB (4096)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.HostKVMiB === 7168 }" @click="setHostKVMiB(7168)">7 GB (7168)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.HostKVMiB === 12288 }" @click="setHostKVMiB(12288)">12 GB (12288)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.HostKVMiB === 16384 }" @click="setHostKVMiB(16384)">16 GB (16384)</button>
                    </div>

                    <div class="protect-callout-strip">
                      <span class="protect-icon">🛡️</span>
                      <span class="protect-text">
                        {{ t('【智能防爆保护】当全域上下文超出显存池时，KVMem 会自动将较早 Token 分页暂存至电脑内存 (RAM)，彻底杜绝大模型显存溢出 (OOM) 崩溃！', 'Zero-OOM Protection: When conversation exceeds VRAM, KVMem pages attention tokens to system RAM dynamically.') }}
                      </span>
                    </div>
                  </div>

                  <!-- Col 4: MaxContext -->
                  <div class="param-card-cell">
                    <div class="param-cell-head">
                      <label class="param-cell-title">{{ t('单次会话最大全域上下文 (MaxContext)', 'Max Logical Full Context (MaxContext)') }}</label>
                      <span class="param-cell-tag text-indigo">{{ Math.round((appConfig.MaxContext || 0) / 1024) }}K (≈ 35-40万字)</span>
                    </div>

                    <div class="studio-input-wrap">
                      <input type="number" class="studio-text-input" v-model.number="appConfig.MaxContext" placeholder="262144" />
                      <span class="studio-input-unit">Tokens ({{ Math.round((appConfig.MaxContext || 0) / 1024) }}K)</span>
                    </div>

                    <div class="studio-pills-row">
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.MaxContext === 32768 }" @click="setMaxContext(32768)">32K (32768)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.MaxContext === 65536 }" @click="setMaxContext(65536)">64K (65536)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.MaxContext === 131072 }" @click="setMaxContext(131072)">128K (131072)</button>
                      <button type="button" class="studio-pill-btn" :class="{ active: appConfig.MaxContext === 262144 }" @click="setMaxContext(262144)">256K (262144)</button>
                    </div>

                    <p class="param-cell-note">
                      {{ t('单次会话支持的最大逻辑全域上下文容量（默认 256K，覆盖长篇著作与完整工程代码库）。由 RoPE 位置编码与 KVMem 逻辑页池共同保证。', 'Max session context limit (default 256K, covers long texts and full codebases), guaranteed by RoPE & KVMem paging pool.') }}
                    </p>
                  </div>
                </div>
              </div>
            </div>

            <!-- Card Section C: Underlying Ports & Ring Buffers (Collapsible) -->
            <div class="studio-card card-accordion">
              <div class="accordion-head clickable" @click="showAdvancedConfig = !showAdvancedConfig">
                <div class="head-left">
                  <span class="accordion-arrow" :class="{ rotated: showAdvancedConfig }">▶</span>
                  <strong class="accordion-title">{{ t('端口配置', 'Port Configuration') }}</strong>
                  <span class="accordion-sub">{{ t('推理端口、智能体服务端口以及 KV 环形滑动窗口内部细粒度分配', 'Engine ports, agent guard ports, and fine-grained ring buffer allocations.') }}</span>
                </div>
                <span class="accordion-badge">{{ showAdvancedConfig ? t('收起', 'Collapse') : t('展开', 'Expand') }}</span>
              </div>

              <div class="accordion-body" v-if="showAdvancedConfig">
                <div class="ports-six-grid">
                  <div class="port-tile">
                    <label class="port-label">{{ t('服务模型标识 (Model ID)', 'Model ID') }}</label>
                    <input type="text" class="port-input" v-model="appConfig.ModelId" placeholder="qwen-local" />
                  </div>
                  <div class="port-tile">
                    <label class="port-label">{{ t('推理服务端口 (EnginePort)', 'Engine Port') }}</label>
                    <input type="number" class="port-input" v-model.number="appConfig.EnginePort" placeholder="1234" />
                  </div>
                  <div class="port-tile">
                    <label class="port-label">{{ t('智能卫士端口 (GuardPort)', 'Guard Port') }}</label>
                    <input type="number" class="port-input" v-model.number="appConfig.GuardPort" placeholder="1236" />
                  </div>
                  <div class="port-tile">
                    <label class="port-label">{{ t('WebUI 工作台端口 (WebPort)', 'WebPort') }}</label>
                    <input type="number" class="port-input" v-model.number="appConfig.WebPort" placeholder="3080" />
                  </div>
                  <div class="port-tile">
                    <label class="port-label">{{ t('滑动窗口 (RecentTokens)', 'Recent Tokens') }}</label>
                    <input type="number" class="port-input" v-model.number="appConfig.RecentTokens" />
                  </div>
                  <div class="port-tile">
                    <label class="port-label">{{ t('持久锚点 (SinkTokens)', 'Sink Tokens') }}</label>
                    <input type="number" class="port-input" v-model.number="appConfig.SinkTokens" />
                  </div>
                  <div class="port-tile">
                    <label class="port-label">{{ t('检索跨度 (RetrieveTokens)', 'Retrieve Tokens') }}</label>
                    <input type="number" class="port-input" v-model.number="appConfig.RetrieveTokens" />
                  </div>
                </div>
              </div>
            </div>

            <!-- Sticky Bottom Studio Action Bar -->
            <div class="model-bottom-bar">
              <div class="bottom-left-hw">
                <span class="hw-chip-pill">
                  <span class="hw-chip-dot"></span>
                  <span class="hw-chip-text">{{ detectedGpuShortName || 'RTX 5070 Ti Laptop GPU' }} · {{ detectedTotalVramGb ? detectedTotalVramGb + ' GB 独立显存' : '12 GB' }}</span>
                </span>
                <span class="hw-sub-info" v-if="vramBreakdown?.topology?.dgpu_driver">驱动: {{ vramBreakdown.topology.dgpu_driver }}</span>
              </div>

              <div class="bottom-right-actions">
                <button type="button" class="btn-studio-subtle" @click="handleResetDefaults" :title="t('恢复系统出厂基准参数配置', 'Reset to default parameters')">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-svg-sm">
                    <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"></path>
                    <path d="M3 3v5h5"></path>
                  </svg>
                  <span>{{ t('恢复默认', 'Reset') }}</span>
                </button>


                <button type="button" class="btn-studio-primary" @click="handleSaveConfig(false)">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-svg-sm">
                    <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"></path>
                    <polyline points="17 21 17 13 7 13 7 21"></polyline>
                    <polyline points="7 3 7 8 15 8"></polyline>
                  </svg>
                  <span>{{ t('保存配置', 'Save Configuration') }}</span>
                </button>

                <button v-if="serviceState.status === 'Running'" type="button" class="btn-studio-accent" @click="handleSaveConfig(true)">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-svg-sm">
                    <path d="M23 4v6h-6"></path>
                    <path d="M1 20v-6h6"></path>
                    <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
                  </svg>
                  <span>{{ t('保存并重启', 'Save & Restart') }}</span>
                </button>
              </div>
            </div>
          </div>
        </div>

        <!-- ==============================================================
             PAGE 5: 运行日志 (Logs)
             ============================================================== -->
        <div v-show="currentTab === 'logs'" class="tab-page page-logs">
          <div class="logs-card-box">
            <!-- Filter Toolbar -->
            <div class="logs-toolbar">
              <div class="logs-filters-group">
                <button
                  class="logs-tab-btn"
                  :class="{ active: activeLogFilter === 'all' }"
                  @click="activeLogFilter = 'all'"
                >
                  {{ t('全部日志', 'All Logs') }} ({{ logs.length }})
                </button>
                <button
                  class="logs-tab-btn"
                  :class="{ active: activeLogFilter === 'engine' }"
                  @click="activeLogFilter = 'engine'"
                >
                  {{ t('推理引擎', 'Engine') }}
                </button>
                <button
                  class="logs-tab-btn"
                  :class="{ active: activeLogFilter === 'guard' }"
                  @click="activeLogFilter = 'guard'"
                >
                  {{ t('守卫代理', 'Cache Guard') }}
                </button>
                <button
                  class="logs-tab-btn"
                  :class="{ active: activeLogFilter === 'dsh' }"
                  @click="activeLogFilter = 'dsh'"
                >
                  DSH Agent
                </button>
                <button
                  class="logs-tab-btn"
                  :class="{ active: activeLogFilter === 'system' }"
                  @click="activeLogFilter = 'system'"
                >
                  {{ t('系统', 'System') }}
                </button>
              </div>

              <div class="logs-actions-group">
                <label class="auto-scroll-checkbox">
                  <input type="checkbox" v-model="autoScroll" />
                  <span>{{ t('自动滚动', 'Auto Scroll') }}</span>
                </label>
                <button class="btn-tool-subtle" @click="clearLogs">{{ t('清空', 'Clear') }}</button>
              </div>
            </div>

            <!-- Terminal Output Area -->
            <div class="logs-terminal-viewport" ref="logContainer">
              <div
                v-for="(log, idx) in filteredLogs"
                :key="idx"
                class="terminal-log-row"
                :class="{ 'row-error': log.message.includes('ERROR') || log.message.includes('FATAL') }"
              >
                <span class="log-ts">{{ log.timestamp }}</span>
                <span class="log-src-tag" :class="'tag-' + log.source.toLowerCase()">{{ log.source }}</span>
                <span class="log-content">{{ log.message }}</span>
              </div>

              <div v-if="filteredLogs.length === 0" class="logs-empty-tip">
                {{ t('暂无日志输出，启动服务后将在此实时显示。', 'No logs yet. Realtime outputs will display here.') }}
              </div>
            </div>
          </div>
        </div>

        <!-- ==============================================================
             PAGE 6: 设置 (Settings)
             ============================================================== -->
        <div v-show="currentTab === 'settings'" class="tab-page page-settings">
          <!-- Top Floating Tab Switcher Bar (Matching Image 3) -->
          <div class="settings-top-nav-wrap">
            <div class="settings-pill-nav">
              <button
                class="settings-pill-tab"
                :class="{ active: currentSettingsTab === 'general' }"
                @click="currentSettingsTab = 'general'"
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="settings-tab-svg">
                  <circle cx="12" cy="12" r="3"></circle>
                  <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
                </svg>
                <span>{{ t('常规设置', 'General Settings') }}</span>
              </button>
              <button
                class="settings-pill-tab"
                :class="{ active: currentSettingsTab === 'about' }"
                @click="currentSettingsTab = 'about'"
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="settings-tab-svg">
                  <circle cx="12" cy="12" r="10"></circle>
                  <line x1="12" y1="16" x2="12" y2="12"></line>
                  <line x1="12" y1="8" x2="12.01" y2="8"></line>
                </svg>
                <span>{{ t('关于 Pmod', 'About Pmod') }}</span>
              </button>
            </div>
          </div>

          <!-- TAB 1: 常规设置 (Matching Image 2) -->
          <div v-show="currentSettingsTab === 'general'" class="settings-content-wrap">
            <div class="settings-group-header">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="group-header-icon">
                <circle cx="12" cy="12" r="3"></circle>
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
              </svg>
              <h2>{{ t('常规偏好', 'General Preferences') }}</h2>
            </div>

            <div class="settings-box-card">
              <!-- Item 1: 界面语言 -->
              <div class="pref-row">
                <div class="pref-lead">
                  <div class="pref-icon-box">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pref-icon-svg">
                      <path d="M5 8l6 6"></path>
                      <path d="M4 14l6-6 2-3"></path>
                      <path d="M2 5h12"></path>
                      <path d="M7 2h1"></path>
                      <path d="M22 22l-5-10-5 10"></path>
                      <path d="M14 18h6"></path>
                    </svg>
                  </div>
                  <div class="pref-text">
                    <div class="pref-title">{{ t('界面语言', 'Display Language') }}</div>
                    <div class="pref-desc">{{ t('切换应用显示语言', 'Change application language') }}</div>
                  </div>
                </div>
                <div class="pref-action">
                  <div class="pill-segmented">
                    <button
                      class="pill-seg-item"
                      :class="{ active: currentLang === 'zh' }"
                      @click="setLanguage('zh')"
                    >
                      简体中文
                    </button>
                    <button
                      class="pill-seg-item"
                      :class="{ active: currentLang === 'en' }"
                      @click="setLanguage('en')"
                    >
                      English
                    </button>
                  </div>
                </div>
              </div>

              <!-- Item 2: 外观主题 -->
              <div class="pref-row">
                <div class="pref-lead">
                  <div class="pref-icon-box">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pref-icon-svg">
                      <circle cx="13.5" cy="6.5" r=".5" fill="currentColor"></circle>
                      <circle cx="17.5" cy="10.5" r=".5" fill="currentColor"></circle>
                      <circle cx="8.5" cy="7.5" r=".5" fill="currentColor"></circle>
                      <circle cx="6.5" cy="12.5" r=".5" fill="currentColor"></circle>
                      <path d="M12 2C6.5 2 2 6.5 2 12s4.5 10 10 10c.926 0 1.648-.746 1.648-1.688 0-.437-.18-.835-.437-1.125-.29-.289-.438-.652-.438-1.125a1.64 1.64 0 0 1 1.668-1.668h1.996c3.051 0 5.563-2.512 5.563-5.563C22 6.5 17.5 2 12 2z"></path>
                    </svg>
                  </div>
                  <div class="pref-text">
                    <div class="pref-title">{{ t('外观主题', 'Appearance Theme') }}</div>
                    <div class="pref-desc">{{ t('选择系统、浅色或深色外观', 'Select system, light or dark appearance') }}</div>
                  </div>
                </div>
                <div class="pref-action">
                  <div class="pill-segmented">
                    <button
                      class="pill-seg-item"
                      :class="{ active: currentTheme === 'system' }"
                      @click="setTheme('system')"
                    >
                      {{ t('跟随系统', 'System') }}
                    </button>
                    <button
                      class="pill-seg-item"
                      :class="{ active: currentTheme === 'light' }"
                      @click="setTheme('light')"
                    >
                      {{ t('浅色模式', 'Light') }}
                    </button>
                    <button
                      class="pill-seg-item"
                      :class="{ active: currentTheme === 'dark' }"
                      @click="setTheme('dark')"
                    >
                      {{ t('深色模式', 'Dark') }}
                    </button>
                  </div>
                </div>
              </div>

              <!-- Item 3: 配色方案 -->
              <div class="pref-row">
                <div class="pref-lead">
                  <div class="pref-icon-box">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pref-icon-svg">
                      <path d="M12 2v4"></path>
                      <path d="M12 18v4"></path>
                      <path d="M4.93 4.93l2.83 2.83"></path>
                      <path d="M16.24 16.24l2.83 2.83"></path>
                      <path d="M2 12h4"></path>
                      <path d="M18 12h4"></path>
                      <path d="M4.93 19.07l2.83-2.83"></path>
                      <path d="M16.24 7.76l2.83-2.83"></path>
                    </svg>
                  </div>
                  <div class="pref-text">
                    <div class="pref-title">{{ t('配色方案', 'Color Scheme') }}</div>
                    <div class="pref-desc">{{ t('选择应用核心主题色系 (Material Design 3)', 'Select core theme color scheme (Material Design 3)') }}</div>
                  </div>
                </div>
                <div class="pref-action">
                  <div class="theme-colors-cluster">
                    <!-- 白曜极简 -->
                    <button
                      class="color-choice-pill"
                      :class="{ 'choice-active': currentColorScheme === 'default' }"
                      @click="setColorScheme('default')"
                    >
                      <span class="color-dot dot-default"></span>
                      <span>{{ t('白曜极简', 'Minimalist White') }}</span>
                    </button>
                    <!-- 赤霞绯红 -->
                    <button
                      class="color-choice-pill"
                      :class="{ 'choice-active': currentColorScheme === 'crimson' }"
                      @click="setColorScheme('crimson')"
                    >
                      <span class="color-dot dot-crimson"></span>
                      <span>{{ t('赤霞绯红', 'Crimson Red') }}</span>
                    </button>
                    <!-- 绿岚翡翠 -->
                    <button
                      class="color-choice-pill"
                      :class="{ 'choice-active': currentColorScheme === 'emerald' }"
                      @click="setColorScheme('emerald')"
                    >
                      <span class="color-dot dot-emerald"></span>
                      <span>{{ t('绿岚翡翠', 'Emerald Green') }}</span>
                    </button>
                    <!-- 蓝海清冽 -->
                    <button
                      class="color-choice-pill"
                      :class="{ 'choice-active': currentColorScheme === 'ocean' }"
                      @click="setColorScheme('ocean')"
                    >
                      <span class="color-dot dot-ocean"></span>
                      <span>{{ t('蓝海清冽', 'Ocean Blue') }}</span>
                    </button>
                  </div>
                </div>
              </div>

              <!-- Item 4: 软件版本与检查更新 (用户主动点击才检查，绝不自动弹窗) -->
              <div class="pref-row">
                <div class="pref-lead">
                  <div class="pref-icon-box">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pref-icon-svg">
                      <path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/>
                    </svg>
                  </div>
                  <div class="pref-text">
                    <div class="pref-title">{{ t('软件版本', 'Software Version') }}</div>
                    <div class="pref-desc">{{ t('当前版本 v1.0.0 (Release)', 'Current v1.0.0 (Release)') }}</div>
                  </div>
                </div>
                <div class="pref-action">
                  <button
                    class="btn-pref-ghost"
                    :disabled="isCheckingUpdate"
                    @click="checkAppUpdate"
                    :title="t('点击检查是否有新版本可用', 'Click to check for updates')"
                  >
                    <svg v-if="!isCheckingUpdate" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="width: 13px; height: 13px;">
                      <path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/>
                    </svg>
                    <span v-else class="btn-spinner-dot" style="width: 11px; height: 11px;"></span>
                    <span>{{ isCheckingUpdate ? t('检查中...', 'Checking...') : t('检查更新', 'Check for Updates') }}</span>
                  </button>
                </div>
              </div>
            </div>

            <!-- Additional Group: 本地存储与运行环境 -->
            <div class="settings-group-header" style="margin-top: 28px;">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="group-header-icon">
                <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
              </svg>
              <h2>{{ t('存储与运行目录', 'Storage & Local Paths') }}</h2>
            </div>

            <div class="settings-box-card">
              <!-- Item 4: 本地根目录 -->
              <div class="pref-row">
                <div class="pref-lead">
                  <div class="pref-icon-box">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pref-icon-svg">
                      <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"></path>
                      <polyline points="9 22 9 12 15 12 15 22"></polyline>
                    </svg>
                  </div>
                  <div class="pref-text">
                    <div class="pref-title">{{ t('工作台安装目录', 'App Root Directory') }}</div>
                    <div class="pref-desc">{{ localRepoRoot || t('动态自适应根路径', 'Dynamic Local Path') }}</div>
                  </div>
                </div>
                <div class="pref-action">
                  <button class="btn-pref-ghost" @click="handleOpenFolder('root')">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="width: 13px; height: 13px;">
                      <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path>
                      <polyline points="15 3 21 3 21 9"></polyline>
                      <line x1="10" y1="14" x2="21" y2="3"></line>
                    </svg>
                    <span>{{ t('打开目录', 'Open Folder') }}</span>
                  </button>
                </div>
              </div>

              <!-- Item 5: 日志与会话缓存 -->
              <div class="pref-row">
                <div class="pref-lead">
                  <div class="pref-icon-box">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pref-icon-svg">
                      <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"></polyline>
                    </svg>
                  </div>
                  <div class="pref-text">
                    <div class="pref-title">{{ t('运行日志与会话缓存', 'Logs & Runtime Cache') }}</div>
                    <div class="pref-desc">{{ t('排查启动日志、推理标准输出与智能体会话持久化', 'Inspect logs, stderr traces and agent storage') }}</div>
                  </div>
                </div>
                <div class="pref-action" style="display: flex; gap: 8px;">
                  <button class="btn-pref-ghost" @click="handleOpenFolder('runtime/logs')">
                    <span>{{ t('打开日志目录', 'Open Logs') }}</span>
                  </button>
                  <button class="btn-pref-ghost" @click="handleOpenFolder('runtime/cache')">
                    <span>{{ t('打开缓存目录', 'Open Cache') }}</span>
                  </button>
                </div>
              </div>
            </div>
          </div>

          <!-- TAB 2: 关于 Pmod (About Pmod) -->
          <div v-show="currentSettingsTab === 'about'" class="settings-content-wrap">
            <!-- Hero Card -->
            <div class="about-hero-card">
              <div class="about-hero-glow">
                <div class="about-logo-wrapper">
                  <svg class="about-logo-svg" viewBox="0 0 100 100" fill="none">
                    <path d="M26 24C26 21.79 27.79 20 30 20H48C62.36 20 74 31.64 74 46C74 60.36 62.36 72 48 72H38V76C38 78.21 36.21 80 34 80H30C27.79 80 26 78.21 26 76V24Z" fill="url(#pFlowAbout)"/>
                    <path d="M38 32H48C55.73 32 62 38.27 62 46C62 53.73 55.73 60 48 60H38V32Z" fill="#090d16"/>
                    <path d="M50 38L57 46L50 54L43 46L50 38Z" fill="#ffffff"/>
                    <path d="M26 84C40 88 60 88 74 84" stroke="#38bdf8" stroke-width="4" stroke-linecap="round"/>
                    <defs>
                      <linearGradient id="pFlowAbout" x1="26" y1="20" x2="74" y2="80" gradientUnits="userSpaceOnUse">
                        <stop offset="0%" stop-color="#38bdf8"/>
                        <stop offset="50%" stop-color="#6366f1"/>
                        <stop offset="100%" stop-color="#a855f7"/>
                      </linearGradient>
                    </defs>
                  </svg>
                </div>
                <div class="about-hero-details">
                  <div class="about-hero-title-row">
                    <h1 class="about-hero-title">Pmod 本地大模型管理平台</h1>
                    <span class="about-version-badge">v1.0.0 Release</span>
                  </div>
                  <p class="about-hero-subtitle">Pmod Desktop Local Agent · 专为大语言模型本地高性能推理、长上下文长程思维及多智能体协同设计的桌面级一体化平台</p>
                  <div class="about-hero-tags">
                    <span class="tech-tag">Tauri 2.0</span>
                    <span class="tech-tag">Vue 3.5</span>
                    <span class="tech-tag">Ninfer 1.0.0</span>
                    <span class="tech-tag">KVMem 256K</span>
                    <span class="tech-tag">nvfp4 / rk4v4</span>
                    <span class="tech-tag">DeepSeek Harness</span>
                  </div>
                </div>
              </div>
            </div>

            <!-- Highlights Grid (Image 3 Style: Clean minimalist line-art stroke icons, no emoji/AI style) -->
            <div class="about-highlights-grid">
              <div class="highlight-feature-card">
                <div class="feature-icon-box">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="feature-svg">
                    <rect x="4" y="4" width="16" height="16" rx="2"></rect>
                    <rect x="9" y="9" width="6" height="6"></rect>
                    <line x1="9" y1="1" x2="9" y2="4"></line>
                    <line x1="15" y1="1" x2="15" y2="4"></line>
                    <line x1="9" y1="20" x2="9" y2="23"></line>
                    <line x1="15" y1="20" x2="15" y2="23"></line>
                    <line x1="20" y1="9" x2="23" y2="9"></line>
                    <line x1="20" y1="14" x2="23" y2="14"></line>
                    <line x1="1" y1="9" x2="4" y2="9"></line>
                    <line x1="1" y1="14" x2="4" y2="14"></line>
                  </svg>
                </div>
                <div class="feature-info-title">Ninfer 高性能推理引擎</div>
                <div class="feature-info-desc">深度适配 NVIDIA Ada 与 Blackwell 架构，原生支持 nvfp4 硬件级 TensorCore 加速与 MTP 3Draft 投机解码，带来极致解码吞吐。</div>
              </div>

              <div class="highlight-feature-card">
                <div class="feature-icon-box">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="feature-svg">
                    <ellipse cx="12" cy="5" rx="9" ry="3"></ellipse>
                    <path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"></path>
                    <path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"></path>
                  </svg>
                </div>
                <div class="feature-info-title">分层 KV 调度 (KVMem)</div>
                <div class="feature-info-desc">GPU 原生显存与系统宿主内存协同分层调度，配合自适应环形置换缓冲区，轻松承载 256K 超长上下文长程思维。</div>
              </div>

              <div class="highlight-feature-card">
                <div class="feature-icon-box">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="feature-svg">
                    <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path>
                  </svg>
                </div>
                <div class="feature-info-title">Cache Guard 上下文守卫</div>
                <div class="feature-info-desc">解决多轮对话历史被客户端重排或裁剪导致的缓存击穿问题，严格恢复字节流，保持前缀复用命中率高达 90%+。</div>
              </div>

              <div class="highlight-feature-card">
                <div class="feature-icon-box">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="feature-svg">
                    <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"></polyline>
                  </svg>
                </div>
                <div class="feature-info-title">WDDM 混合显存调度器</div>
                <div class="feature-info-desc">精准嗅探独显与核显占用，一键将后台常驻应用平滑迁移至核显运行，彻底杜绝大模型 OOM 显存溢出。</div>
              </div>
            </div>

            <!-- Author & Community Group Card -->
            <div class="about-community-card">
              <div class="community-info-cluster">
                <div class="community-meta-item">
                  <span class="meta-label">作者</span>
                  <span class="meta-val">复读机</span>
                </div>
                <div class="meta-sep"></div>
                <div class="community-meta-item">
                  <span class="meta-label">作者 QQ</span>
                  <span class="meta-val">785029497</span>
                </div>
                <div class="meta-sep"></div>
                <div class="community-meta-item">
                  <span class="meta-label">交流群</span>
                  <span class="meta-val meta-highlight">1128130199</span>
                </div>
              </div>

              <div class="community-action-group">
                <button
                  class="btn-check-update"
                  :disabled="isCheckingUpdate"
                  @click="checkAppUpdate"
                  :title="t('在线检查最新发布版本与特性清单', 'Check online for latest releases')"
                >
                  <svg v-if="!isCheckingUpdate" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-check-svg">
                    <path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/>
                  </svg>
                  <span v-else class="btn-spinner-dot"></span>
                  <span>{{ isCheckingUpdate ? t('检查中...', 'Checking...') : t('检查更新', 'Check Updates') }}</span>
                </button>

                <button class="btn-join-qq" @click="openQqGroup" title="点击打开默认浏览器加入交流群">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="btn-join-svg">
                    <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path>
                  </svg>
                  <span>加入交流群</span>
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="arrow-svg">
                    <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path>
                    <polyline points="15 3 21 3 21 9"></polyline>
                    <line x1="10" y1="14" x2="21" y2="3"></line>
                  </svg>
                </button>
              </div>
            </div>

            <!-- Footer -->
            <div class="about-page-footer">
              <p class="footer-license-note">
                Copyright © 2026 Pmod. 作者: 复读机 (QQ: 785029497) · 交流群: 1128130199. All rights reserved. 开源许可证：Apache License 2.0 & NVIDIA CUDA EULA.
              </p>
            </div>
          </div>
        </div>
      </div>
    </main>

    <!-- ==============================================================
         PRESTIGE GLASS MODAL (Antigravity Global Modal System)
         ============================================================== -->
    <transition name="modal-bounce">
      <div v-if="modalState.isOpen" class="custom-modal-overlay" @click.self="handleModalCancel">
        <div class="custom-modal-box" :class="'modal-' + modalState.type" role="dialog" aria-modal="true">
          <!-- Top subtle ambient highlight strip -->
          <div class="modal-glow-strip" :class="'glow-' + modalState.type"></div>

          <!-- Header -->
          <div class="modal-header">
            <div class="modal-icon-badge" :class="'badge-' + modalState.type">
              <!-- Danger Icon -->
              <svg v-if="modalState.type === 'danger'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" class="modal-svg">
                <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
                <line x1="12" y1="9" x2="12" y2="13"></line>
                <line x1="12" y1="17" x2="12.01" y2="17"></line>
              </svg>
              <!-- Warning Icon -->
              <svg v-else-if="modalState.type === 'warning'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" class="modal-svg">
                <circle cx="12" cy="12" r="10"></circle>
                <line x1="12" y1="8" x2="12" y2="12"></line>
                <line x1="12" y1="16" x2="12.01" y2="16"></line>
              </svg>
              <!-- Success Icon -->
              <svg v-else-if="modalState.type === 'success'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" class="modal-svg">
                <polyline points="20 6 9 17 4 12"></polyline>
              </svg>
              <!-- Info Icon -->
              <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" class="modal-svg">
                <circle cx="12" cy="12" r="10"></circle>
                <line x1="12" y1="16" x2="12" y2="12"></line>
                <line x1="12" y1="8" x2="12.01" y2="8"></line>
              </svg>
            </div>

            <div class="modal-title-wrap">
              <h3 class="modal-title">{{ modalState.title }}</h3>
            </div>

            <button class="modal-close-icon-btn" @click="handleModalCancel" title="Esc">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" class="close-svg">
                <line x1="18" y1="6" x2="6" y2="18"></line>
                <line x1="6" y1="6" x2="18" y2="18"></line>
              </svg>
            </button>
          </div>

          <!-- Body -->
          <div class="modal-body">
            <div class="modal-message">{{ modalState.message }}</div>
            <div v-if="modalState.detail" class="modal-detail-card">
              <code>{{ modalState.detail }}</code>
            </div>
          </div>

          <!-- Footer Actions -->
          <div class="modal-footer">
            <button
              v-if="modalState.showCancel"
              class="modal-action-btn modal-btn-cancel"
              @click="handleModalCancel"
            >
              {{ modalState.cancelText }}
            </button>
            <button
              class="modal-action-btn modal-btn-confirm"
              :class="'btn-' + modalState.type"
              @click="handleModalConfirm"
            >
              {{ modalState.confirmText }}
            </button>
          </div>
        </div>
      </div>
    </transition>
  </div>
</template>

<style scoped>
/* =========================================
   Studio App Main Shell (Sidebar + Viewport)
   ========================================= */
.studio-app {
  display: flex;
  width: 100vw;
  height: 100vh;
  overflow: hidden;
  background: #f7f9fd;
  color: #1e293b;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
}

/* =========================================
   LEFT SIDEBAR (Pure White, Crisp Border)
   ========================================= */
.sidebar {
  width: 210px;
  background: #ffffff;
  border-right: 1px solid #eef2f7;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  flex-shrink: 0;
  transition: width 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  z-index: 50;
}

.studio-app.sidebar-collapsed .sidebar {
  width: 64px;
}

.studio-app.sidebar-collapsed .menu-item {
  justify-content: center;
  padding: 8px 0;
}

/* Sidebar Top Logo */
.sidebar-top {
  padding: 16px 14px 12px 14px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.brand-box {
  display: flex;
  align-items: center;
  gap: 11px;
  overflow: hidden;
}

.brand-logo-glow {
  width: 34px;
  height: 34px;
  border-radius: 9px;
  background: linear-gradient(145deg, #1e293b 0%, #09090b 100%);
  border: 1px solid rgba(255, 255, 255, 0.14);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.22), 0 3px 8px rgba(15, 23, 42, 0.16);
  display: flex;
  align-items: center;
  justify-content: center;
  color: #ffffff;
  flex-shrink: 0;
  transition: all 0.2s ease;
}

.brand-logo-glow:hover {
  transform: translateY(-1px);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.28), 0 5px 12px rgba(15, 23, 42, 0.22);
}

.brand-logo-mini {
  margin: 0 auto;
}

.brand-logo-svg {
  width: 20px;
  height: 20px;
}

.brand-text {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.brand-name {
  font-size: 15px;
  font-weight: 700;
  color: #0f172a;
  letter-spacing: -0.02em;
  line-height: 1;
  white-space: nowrap;
}

.brand-tag {
  font-size: 10px;
  color: #64748b;
  font-weight: 500;
  display: flex;
  align-items: center;
  gap: 4px;
  white-space: nowrap;
}

.brand-tag-badge {
  background: #f1f5f9;
  padding: 0.5px 5px;
  border-radius: 4px;
  color: #475569;
  font-weight: 600;
  font-size: 9.5px;
  border: 1px solid #e2e8f0;
}

.btn-collapse {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  border: 1px solid #e2e8f0;
  background: #ffffff;
  color: #64748b;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-collapse:hover {
  background: #f1f5f9;
  color: #1e293b;
}

.collapse-arrow {
  width: 14px;
  height: 14px;
  transition: transform 0.2s;
}

.collapse-arrow.arrow-collapsed {
  transform: rotate(180deg);
}

/* Sidebar Navigation Menu */
.sidebar-menu {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 8px 10px;
  flex: 1;
}

.menu-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 12px;
  border-radius: 8px;
  background: transparent;
  border: none;
  color: #475569;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
  text-align: left;
  width: 100%;
}

.menu-item:hover {
  background: #f8fafc;
  color: #1e293b;
}

.menu-item.menu-item-active {
  background: #edf2fe;
  color: #2563eb;
  font-weight: 600;
}

.menu-icon-box {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 20px;
  height: 20px;
  flex-shrink: 0;
}

.menu-svg {
  width: 17px;
  height: 17px;
}

.menu-label {
  flex: 1;
  white-space: nowrap;
}

.menu-badge-online {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 10px;
  background: #dcfce7;
  color: #166534;
  font-weight: 600;
}

.menu-badge-vram {
  font-size: 10.5px;
  padding: 1px 6px;
  border-radius: 10px;
  background: #eff6ff;
  color: #2563eb;
  font-family: monospace;
}

.menu-badge-count {
  font-size: 10px;
  padding: 1px 5px;
  border-radius: 8px;
  background: #f1f5f9;
  color: #64748b;
  font-family: monospace;
}

/* Sidebar Bottom Status */
.sidebar-bottom {
  padding: 12px 10px;
  border-top: 1px solid #f1f5f9;
}

.sidebar-status-card {
  padding: 10px;
  background: #f8fafc;
  border: 1px solid #eef2f7;
  border-radius: 8px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.status-card-header {
  display: flex;
  align-items: center;
  gap: 6px;
}

.status-chip-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.status-chip-text {
  font-size: 11.5px;
  font-weight: 600;
  color: #334155;
}

.status-card-vram-row {
  display: flex;
  justify-content: space-between;
  font-size: 10.5px;
  color: #64748b;
}

.status-vram-bar-track {
  width: 100%;
  height: 5px;
  background: #e2e8f0;
  border-radius: 3px;
  overflow: hidden;
}

.status-vram-bar-fill {
  height: 100%;
  border-radius: 3px;
  transition: width 0.3s ease;
}

/* =========================================
   RIGHT MAIN VIEWPORT
   ========================================= */
.main-viewport {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: linear-gradient(135deg, #f7f9fd 0%, #f1f5fd 100%);
}

/* Viewport Top Header - Solid Crisp White with zero GPU blur overhead */
.viewport-header {
  min-height: 52px;
  padding: 0 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid #eef2f6;
  background: #ffffff;
  flex-shrink: 0;
  z-index: 10;
  gap: 12px;
  overflow-x: auto;
  overflow-y: hidden;
  scrollbar-width: none;
}

.viewport-header::-webkit-scrollbar {
  display: none;
}

.header-left-title {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
  white-space: nowrap;
}

.header-main-title {
  font-size: 17px;
  font-weight: 700;
  color: #0f172a;
  letter-spacing: -0.01em;
  white-space: nowrap;
  flex-shrink: 0;
}

.header-sub-badges {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
  white-space: nowrap;
}

.engine-status-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 3px 8px;
  border-radius: 12px;
  font-size: 11px;
  font-weight: 600;
  white-space: nowrap;
  flex-shrink: 0;
}

.engine-badge-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex-shrink: 0;
}

.status-badge-running { background: #dcfce7; color: #15803d; }
.status-badge-running .engine-badge-dot { background: #16a34a; }
.status-badge-starting { background: #fef3c7; color: #b45309; }
.status-badge-starting .engine-badge-dot { background: #f59e0b; }
.status-badge-stopped { background: #f1f5f9; color: #64748b; }
.status-badge-stopped .engine-badge-dot { background: #94a3b8; }
.status-badge-error { background: #fee2e2; color: #b91c1c; }
.status-badge-error .engine-badge-dot { background: #ef4444; }

.header-model-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 20px;
  font-size: 11.5px;
  color: #475569;
  cursor: pointer;
  max-width: 200px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  flex-shrink: 1;
  transition: all 0.15s;
}

.header-model-pill:hover {
  background: #f8fafc;
  border-color: #cbd5e1;
}

.model-pill-missing {
  background: #fffbeb !important;
  border-color: #fde68a !important;
  color: #b45309 !important;
  font-weight: 600;
}

.model-pill-svg {
  width: 13px;
  height: 13px;
  flex-shrink: 0;
}

.header-right-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
  white-space: nowrap;
}

.header-pill-vram {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 20px;
  padding: 4px 12px;
  display: flex;
  align-items: center;
  gap: 7px;
  font-size: 12px;
  color: #475569;
  cursor: pointer;
  white-space: nowrap !important;
  flex-shrink: 0 !important;
  transition: all 0.15s;
}

.header-pill-vram:hover {
  background: #f8fafc;
  border-color: #cbd5e1;
  box-shadow: 0 2px 4px rgba(0,0,0,0.03);
}

.pill-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  flex-shrink: 0;
}

.pill-tag {
  font-size: 10px;
  padding: 1px 5px;
  border-radius: 10px;
  background: #eff6ff;
  color: #2563eb;
  font-weight: 600;
  white-space: nowrap;
  flex-shrink: 0;
}

.btn-header-secondary {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 7px;
  font-size: 12.5px;
  font-weight: 500;
  color: #334155;
  cursor: pointer;
  white-space: nowrap !important;
  flex-shrink: 0 !important;
  transition: all 0.15s;
}

.btn-header-secondary:hover:not(:disabled) {
  background: #f8fafc;
  border-color: #cbd5e1;
}

.btn-sub-svg {
  width: 14px;
  height: 14px;
  flex-shrink: 0;
}

.btn-header-primary {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 16px;
  border-radius: 7px;
  font-size: 12.5px;
  font-weight: 600;
  cursor: pointer;
  border: none;
  white-space: nowrap !important;
  flex-shrink: 0 !important;
  transition: all 0.15s;
  box-shadow: 0 2px 6px rgba(37, 99, 235, 0.2);
}

.btn-gradient-blue {
  background: linear-gradient(135deg, #2563eb 0%, #1d4ed8 100%);
  color: #ffffff;
}

.btn-gradient-blue:hover {
  background: linear-gradient(135deg, #1d4ed8 0%, #1e40af 100%);
}

.btn-state-danger {
  background: #fee2e2;
  color: #b91c1c;
  border: 1px solid #fecaca;
  box-shadow: none;
}

.btn-state-danger:hover {
  background: #fecaca;
}

.btn-state-loading {
  background: #fef3c7;
  color: #b45309;
  border: 1px solid #fde68a;
  box-shadow: none;
  cursor: not-allowed;
}

.btn-primary-svg {
  width: 13px;
  height: 13px;
}

.btn-spin-dot {
  width: 12px;
  height: 12px;
  border: 2px solid #b45309;
  border-top-color: transparent;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

/* Viewport Body Container - GPU Composited Smooth Scrolling */
.viewport-content {
  flex: 1;
  overflow-y: auto;
  padding: 20px 24px;
  overscroll-behavior-y: none;
  contain: content;
  transform: translateZ(0);
  will-change: scroll-position;
  -webkit-overflow-scrolling: touch;
}

/* While scrolling fast, disable all pointer events to eliminate hover & style recalculations */
.viewport-content.is-scrolling,
.viewport-content.is-scrolling * {
  pointer-events: none !important;
}

.tab-page {
  animation: fadeIn 0.15s ease;
  contain: layout style;
}

/* ==============================================================
   TAB 1: 推理性能与会话统计 (Inference & Conversation Telemetry)
   ============================================================== */
.page-telemetry {
  display: flex;
  flex-direction: column;
  gap: 16px;
  max-width: 1200px;
  margin: 0 auto;
  padding-bottom: 24px;
}

/* 1. Top Banner */
.telemetry-top-banner {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 14px;
  padding: 16px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.02);
}

.banner-left {
  display: flex;
  align-items: center;
  gap: 14px;
}

.banner-icon-squircle {
  width: 42px;
  height: 42px;
  border-radius: 11px;
  background: #090d16;
  border: 1px solid rgba(56, 189, 248, 0.35);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 3px 10px rgba(15, 23, 42, 0.15);
  flex-shrink: 0;
}

.banner-svg {
  width: 22px;
  height: 22px;
  color: #38bdf8;
}

.banner-title {
  font-size: 15px;
  font-weight: 700;
  color: #0f172a;
  margin: 0;
}

.banner-desc {
  font-size: 11.5px;
  color: #64748b;
  margin: 0;
}

.banner-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
}

.btn-probe {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 7px 15px;
  background: linear-gradient(135deg, #2563eb, #1d4ed8);
  color: #ffffff;
  border: none;
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  box-shadow: 0 2px 6px rgba(37, 99, 235, 0.2);
  transition: all 0.15s;
}

.btn-probe:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 10px rgba(37, 99, 235, 0.3);
}

.btn-probe:disabled {
  opacity: 0.75;
  cursor: wait;
}

.probe-spinner {
  width: 13px;
  height: 13px;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-top-color: #ffffff;
  border-radius: 50%;
  animation: probeSpin 0.7s linear infinite;
}

@keyframes probeSpin {
  to { transform: rotate(360deg); }
}

.action-btn-svg {
  width: 14px;
  height: 14px;
}

.export-dropdown-group {
  display: flex;
  align-items: center;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  overflow: hidden;
}

.btn-banner-subtle {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 6px 11px;
  background: transparent;
  border: none;
  font-size: 12px;
  font-weight: 500;
  color: #475569;
  cursor: pointer;
  transition: background 0.15s;
}

.btn-banner-subtle:hover {
  background: #f1f5f9;
  color: #0f172a;
}

.btn-reset {
  border: 1px solid #fee2e2;
  background: #fffafa;
  color: #b91c1c;
  border-radius: 8px;
}

.btn-reset:hover {
  background: #fee2e2;
  color: #991b1b;
}

/* Probe Toast */
.probe-notice-toast {
  background: #f0fdf4;
  border: 1px solid #bbf7d0;
  border-radius: 8px;
  padding: 8px 14px;
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  font-weight: 600;
  color: #15803d;
}

.probe-toast-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #16a34a;
  box-shadow: 0 0 6px #22c55e;
}

/* 2. KPI Summary Grid */
.telemetry-kpi-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 12px;
}

.kpi-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 12px;
  padding: 12px 14px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.02);
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  min-height: 72px;
  transition: transform 0.2s, box-shadow 0.2s, border-color 0.2s;
}

.kpi-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.04);
  border-color: #cbd5e1;
}

.kpi-card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 5px;
}

.kpi-label {
  font-size: 11px;
  font-weight: 600;
  color: #64748b;
}

.kpi-icon-pill {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.kpi-icon-pill svg {
  width: 13px;
  height: 13px;
}

.kpi-value-row {
  display: flex;
  align-items: baseline;
  gap: 4px;
  margin-bottom: 0;
}

.kpi-value {
  font-size: 21px;
  font-weight: 800;
  color: #0f172a;
  line-height: 1;
  letter-spacing: -0.02em;
}

.kpi-unit {
  font-size: 11px;
  font-weight: 600;
  color: #94a3b8;
}

.kpi-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 4px;
  font-size: 10px;
  color: #64748b;
}

.kpi-status-badge {
  font-size: 9.5px;
  font-weight: 600;
  padding: 1px 5px;
  border-radius: 4px;
  white-space: nowrap;
}

.kpi-subtext {
  font-size: 10px;
  color: #64748b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

/* 3. Charts Grid */
.telemetry-charts-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(380px, 1fr));
  gap: 16px;
}

.chart-panel-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 14px;
  padding: 16px 18px;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.02);
  display: flex;
  flex-direction: column;
  gap: 12px;
  position: relative;
}

.chart-panel-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
}

.chart-header-left {
  min-width: 0;
  flex: 1;
}

.chart-panel-title {
  font-size: 13.5px;
  font-weight: 700;
  color: #0f172a;
  margin: 0 0 2px 0;
  white-space: nowrap;
}

.chart-panel-subtitle {
  font-size: 11px;
  color: #64748b;
  margin: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.chart-metric-switch {
  display: flex;
  align-items: center;
  gap: 3px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 2px;
  flex-shrink: 0;
  white-space: nowrap;
}

.metric-pill {
  padding: 3px 9px;
  font-size: 11px;
  font-weight: 500;
  color: #64748b;
  background: transparent;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 5px;
  transition: all 0.15s;
  white-space: nowrap;
}

.metric-pill-active {
  background: #ffffff;
  color: #0f172a;
  font-weight: 600;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.06);
}

.legend-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}

.dot-blue { background: #2563eb; }
.dot-purple { background: #9333ea; }
.dot-green { background: #10b981; }

.chart-legends-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
  white-space: nowrap;
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 11px;
  color: #475569;
  font-weight: 500;
  white-space: nowrap;
  flex-shrink: 0;
}

.legend-box {
  width: 9px;
  height: 9px;
  border-radius: 2px;
}

.bg-prompt { background: #93c5fd; }
.bg-thinking { background: #c084fc; }
.bg-output { background: #34d399; }

.svg-chart-container {
  position: relative;
  width: 100%;
  height: 200px;
}

.telemetry-svg {
  width: 100%;
  height: 100%;
  overflow: visible;
}

.chart-axis-label {
  font-size: 9.5px;
  fill: #94a3b8;
  font-family: monospace;
}

.chart-x-label {
  font-size: 10px;
  fill: #64748b;
  font-weight: 600;
}

.bar-val-label {
  font-size: 9px;
  fill: #64748b;
  font-weight: 600;
  font-family: monospace;
}

.benchmark-tag {
  font-size: 9.5px;
  fill: #2563eb;
  font-weight: 600;
}

.chart-point-group, .chart-bar-group {
  cursor: pointer;
}

.chart-point-group:hover circle:first-child {
  r: 8;
  stroke-width: 3.5;
}

.chart-tooltip-floating {
  position: absolute;
  transform: translate(-50%, -100%);
  background: rgba(15, 23, 42, 0.95);
  backdrop-filter: blur(8px);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 8px;
  padding: 9px 12px;
  color: #ffffff;
  font-size: 11px;
  pointer-events: none;
  z-index: 25;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.25);
  min-width: 175px;
}

.tooltip-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.12);
  padding-bottom: 5px;
  margin-bottom: 6px;
}

.tooltip-turn {
  font-weight: 700;
  color: #38bdf8;
}

.tooltip-time {
  color: #94a3b8;
  font-size: 10px;
}

.tooltip-metrics {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.tmetric-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 6px;
  font-size: 10.5px;
}

.tmetric-k {
  color: #94a3b8;
  display: flex;
  align-items: center;
  gap: 4px;
}

.tmetric-v {
  font-weight: 600;
  color: #f8fafc;
  font-family: monospace;
}

.text-bold {
  font-weight: 700;
  color: #ffffff;
}

.composition-summary-bar {
  display: flex;
  flex-direction: column;
  gap: 5px;
  margin-top: 2px;
}

.summary-bar-track {
  height: 7px;
  width: 100%;
  border-radius: 4px;
  background: #f1f5f9;
  display: flex;
  overflow: hidden;
}

.seg-prompt { background: #93c5fd; }
.seg-thinking { background: #c084fc; }
.seg-output { background: #34d399; }

.summary-bar-labels {
  display: flex;
  justify-content: space-between;
  font-size: 10.5px;
  color: #64748b;
}

.summary-bar-labels b {
  color: #0f172a;
}

/* 4. Table Panel */
.telemetry-table-panel {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 14px;
  padding: 18px 20px;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.02);
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.table-panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
}

.panel-title {
  font-size: 14.5px;
  font-weight: 700;
  color: #0f172a;
  margin: 0 0 2px 0;
}

.panel-subtitle {
  font-size: 11px;
  color: #64748b;
  margin: 0;
}

.panel-header-right {
  display: flex;
  align-items: center;
  gap: 10px;
}

.search-box-pill {
  position: relative;
  display: flex;
  align-items: center;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 5px 10px;
  gap: 6px;
  min-width: 220px;
}

.search-svg {
  width: 13px;
  height: 13px;
  color: #94a3b8;
  flex-shrink: 0;
}

.search-input {
  border: none;
  background: transparent;
  font-size: 11.5px;
  color: #0f172a;
  outline: none;
  width: 100%;
}

.search-clear-btn {
  border: none;
  background: transparent;
  color: #94a3b8;
  cursor: pointer;
  font-size: 13px;
  padding: 0 2px;
}

.filter-pills-row {
  display: flex;
  align-items: center;
  gap: 3px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 2px;
}

.fpill-btn {
  padding: 3px 9px;
  font-size: 11px;
  font-weight: 500;
  color: #64748b;
  background: transparent;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.15s;
}

.fpill-active {
  background: #ffffff;
  color: #0f172a;
  font-weight: 600;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.06);
}

.telemetry-table-wrapper {
  overflow-x: auto;
  border: 1px solid #f1f5f9;
  border-radius: 10px;
}

.telemetry-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
  text-align: left;
}

.telemetry-table th {
  background: #f8fafc;
  padding: 9px 12px;
  font-weight: 600;
  color: #475569;
  border-bottom: 1px solid #e2e8f0;
  font-size: 11px;
  white-space: nowrap;
}

.telemetry-table td {
  padding: 10px 12px;
  border-bottom: 1px solid #f1f5f9;
  vertical-align: middle;
}

.telemetry-row {
  cursor: pointer;
  transition: background 0.15s;
}

.telemetry-row:hover {
  background: #f8fafc;
}

.row-expanded {
  background: #f0f7ff !important;
}

.turn-badge {
  display: inline-block;
  padding: 2px 7px;
  background: #f1f5f9;
  color: #334155;
  font-weight: 700;
  border-radius: 5px;
  font-family: monospace;
  font-size: 11px;
}

.col-time {
  color: #64748b;
  font-size: 11px;
  white-space: nowrap;
}

.task-tag {
  display: inline-block;
  padding: 2px 7px;
  background: #eff6ff;
  color: #2563eb;
  border-radius: 4px;
  font-size: 10.5px;
  font-weight: 600;
}

.thinking-cell {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.cell-val {
  font-weight: 700;
  font-family: monospace;
  font-size: 12px;
}

.cell-sub {
  font-size: 10px;
  color: #94a3b8;
}

.ttft-badge {
  display: inline-block;
  padding: 2px 7px;
  border-radius: 5px;
  font-weight: 700;
  font-family: monospace;
  font-size: 11px;
}

.ttft-fast {
  background: #ecfdf5;
  color: #059669;
  border: 1px solid #a7f3d0;
}

.ttft-normal {
  background: #f8fafc;
  color: #475569;
  border: 1px solid #e2e8f0;
}

.speed-cell {
  display: flex;
  flex-direction: column;
  gap: 3px;
  min-width: 90px;
}

.speed-num {
  font-weight: 700;
  color: #2563eb;
  font-family: monospace;
  font-size: 12px;
}

.speed-num .unit {
  font-size: 9.5px;
  color: #64748b;
  font-weight: 500;
}

.mini-speed-bar {
  width: 100%;
  height: 3.5px;
  background: #e2e8f0;
  border-radius: 2px;
  overflow: hidden;
}

.speed-fill {
  height: 100%;
  background: linear-gradient(90deg, #3b82f6, #6366f1);
  border-radius: 2px;
}

.token-breakdown-inline {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.token-total {
  font-weight: 700;
  color: #0f172a;
  font-family: monospace;
  font-size: 12px;
}

.inline-token-chips {
  display: flex;
  align-items: center;
  gap: 3px;
}

.tok-chip {
  font-size: 9px;
  padding: 1px 4px;
  border-radius: 3px;
  font-family: monospace;
}

.chip-p { background: #eff6ff; color: #2563eb; }
.chip-t { background: #faf5ff; color: #9333ea; }
.chip-o { background: #ecfdf5; color: #059669; }

.cache-badge {
  display: inline-block;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 10.5px;
  font-weight: 600;
}

.cache-hit {
  background: #ecfeff;
  color: #0891b2;
  border: 1px solid #a5f3fc;
}

.cache-cold {
  background: #f8fafc;
  color: #64748b;
  border: 1px solid #e2e8f0;
}

.status-pill {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 10.5px;
  font-weight: 600;
  padding: 2px 7px;
  border-radius: 10px;
}

.status-ok {
  background: #ecfdf5;
  color: #15803d;
}

.status-dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #16a34a;
}

.btn-expand-row {
  border: none;
  background: transparent;
  cursor: pointer;
  color: #64748b;
  padding: 3px;
  border-radius: 4px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.arrow-svg {
  width: 13px;
  height: 13px;
  transition: transform 0.2s;
}

.arrow-rotated {
  transform: rotate(180deg);
  color: #2563eb;
}

.drawer-detail-row {
  background: #f8fafc;
}

.turn-detail-drawer {
  padding: 14px 18px;
  border-bottom: 1px solid #e2e8f0;
}

.drawer-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 16px;
}

.dcol-title {
  font-size: 11px;
  font-weight: 700;
  color: #475569;
  display: block;
  margin-bottom: 5px;
}

.prompt-box {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 9px 12px;
  font-size: 11.5px;
  color: #334155;
  line-height: 1.5;
  max-height: 100px;
  overflow-y: auto;
}

.drawer-metrics-list {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 9px 12px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.dmetric-row {
  display: flex;
  justify-content: space-between;
  font-size: 11.5px;
}

.dm-label {
  color: #64748b;
}

.dm-val {
  font-weight: 600;
  color: #0f172a;
}

.telemetry-empty-box {
  padding: 36px 20px;
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  gap: 6px;
}

.empty-svg {
  width: 28px;
  height: 28px;
  color: #94a3b8;
}

.empty-title {
  font-size: 13px;
  font-weight: 600;
  color: #334155;
}

.empty-desc {
  font-size: 11.5px;
  color: #94a3b8;
}

.empty-icon-circle {
  font-size: 28px;
  margin-bottom: 4px;
}

/* Demo Data Alert Bar */
.demo-data-alert-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  background: #f0fdf4;
  border: 1px solid #bbf7d0;
  border-radius: 10px;
  margin-bottom: 16px;
}

.demo-alert-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.demo-alert-tag {
  font-size: 11px;
  font-weight: 700;
  color: #166534;
  background: #dcfce7;
  padding: 2px 8px;
  border-radius: 6px;
}

.demo-alert-text {
  font-size: 12px;
  color: #15803d;
}

.demo-alert-action {
  font-size: 11.5px;
  font-weight: 600;
  color: #166534;
  background: #ffffff;
  border: 1px solid #86efac;
  padding: 5px 12px;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.15s;
}

.demo-alert-action:hover {
  background: #dcfce7;
  border-color: #4ade80;
}

/* Chart Empty State Container */
.chart-empty-state-box {
  min-height: 210px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #fafbfc;
  border-radius: 10px;
  border: 1px dashed #e2e8f0;
}

.chart-empty-inner {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  gap: 6px;
  padding: 20px;
}

.chart-empty-icon {
  font-size: 26px;
  margin-bottom: 2px;
}

.chart-empty-title {
  font-size: 13px;
  font-weight: 600;
  color: #475569;
}

.chart-empty-desc {
  font-size: 11.5px;
  color: #94a3b8;
  max-width: 280px;
  line-height: 1.4;
}

.btn-chart-demo {
  margin-top: 6px;
  font-size: 11.5px;
  font-weight: 600;
  color: #2563eb;
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  padding: 5px 14px;
  border-radius: 7px;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-chart-demo:hover {
  background: #dbeafe;
  border-color: #93c5fd;
}

/* ==============================================================
   TAB 2: 智能对话 (Chat) - Seamless Full-Bleed Workspace
   ============================================================== */
.page-chat {
  height: calc(100vh - 92px);
  display: flex;
  flex-direction: column;
}

/* When chat tab is active, remove all outer padding & borders for 100% native immersion in all states */
.viewport-content.is-chat-tab {
  padding: 0 !important;
  overflow: hidden !important;
  display: flex;
  flex-direction: column;
}

.is-chat-tab .page-chat {
  height: 100% !important;
  flex: 1;
}

.chat-viewport-running {
  width: 100%;
  height: 100%;
  border-radius: 0;
  border: none;
  box-shadow: none;
  background: #ffffff;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.dsh-seamless-frame {
  flex: 1;
  width: 100%;
  height: 100%;
  border: none;
  display: block;
}

.chat-viewport-loading,
.chat-viewport-error {
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.chat-viewport-stopped {
  height: 100%;
  width: 100%;
  display: flex;
  background: #ffffff;
  overflow: hidden;
  position: relative;
}



/* ==============================================================
   TAB 2: Standby State -> Modern Dedicated Standby Center
   ============================================================== */
.standby-center-viewport {
  width: 100%;
  height: 100%;
  overflow-y: auto;
  background: radial-gradient(circle at 50% 0%, #f0f7ff 0%, #f8fafc 40%, #ffffff 100%);
  display: flex;
  justify-content: center;
  align-items: flex-start;
  padding: 32px 24px;
  box-sizing: border-box;
}

.standby-center-wrapper {
  width: 100%;
  max-width: 960px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

/* 1. Error Banner */
.standby-error-banner {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 14px 20px;
  background: #fef2f2;
  border: 1px solid #fecaca;
  border-radius: 14px;
  box-shadow: 0 4px 12px rgba(239, 68, 68, 0.06);
}
.standby-error-icon {
  font-size: 24px;
  flex-shrink: 0;
}
.standby-error-content {
  flex: 1;
  min-width: 0;
}
.standby-error-title {
  font-size: 14px;
  font-weight: 700;
  color: #991b1b;
  margin-bottom: 2px;
}
.standby-error-desc {
  font-size: 12.5px;
  color: #b91c1c;
  line-height: 1.4;
}
.standby-error-actions {
  display: flex;
  gap: 8px;
  flex-shrink: 0;
}
.standby-btn-retry {
  padding: 7px 14px;
  background: #ef4444;
  color: #ffffff;
  border: none;
  border-radius: 8px;
  font-size: 12.5px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s ease;
}
.standby-btn-retry:hover {
  background: #dc2626;
  box-shadow: 0 2px 8px rgba(239, 68, 68, 0.3);
}
.standby-btn-subtle {
  padding: 7px 12px;
  background: #ffffff;
  color: #475569;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  font-size: 12.5px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s ease;
}
.standby-btn-subtle:hover {
  background: #f8fafc;
  color: #0f172a;
}

/* 2. Hero Presentation Card */
.standby-hero-card {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 20px;
  padding: 30px 24px 26px;
  text-align: center;
  display: flex;
  flex-direction: column;
  align-items: center;
  box-shadow: 0 4px 20px -2px rgba(15, 23, 42, 0.04), 0 1px 3px rgba(15, 23, 42, 0.02);
  position: relative;
  overflow: hidden;
}
.standby-hero-card::before {
  content: "";
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, #38bdf8, #6366f1, #a855f7);
}

.standby-project-logo-glow {
  width: 62px;
  height: 62px;
  border-radius: 17px;
  background: linear-gradient(145deg, #1e293b 0%, #09090b 100%);
  border: 1px solid rgba(255, 255, 255, 0.16);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.25), 0 8px 24px rgba(15, 23, 42, 0.2), 0 0 20px rgba(56, 189, 248, 0.2);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 14px;
  transition: transform 0.2s ease;
}
.standby-project-logo-glow:hover {
  transform: translateY(-2px) scale(1.02);
}
.standby-project-logo-svg {
  width: 36px;
  height: 36px;
}

.standby-main-title {
  font-size: 22px;
  font-weight: 800;
  color: #0f172a;
  letter-spacing: -0.4px;
  margin: 0 0 6px;
}
.standby-subtitle {
  font-size: 13.5px;
  color: #64748b;
  margin: 0 0 16px;
  max-width: 580px;
  line-height: 1.5;
}

.standby-status-pill-wrap {
  display: inline-flex;
  flex-wrap: wrap;
  gap: 8px;
  justify-content: center;
  margin-bottom: 20px;
}
.standby-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 12px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: 600;
}
.pill-ready {
  background: #ecfdf5;
  color: #047857;
  border: 1px solid #a7f3d0;
}
.pill-warning {
  background: #fffbeb;
  color: #b45309;
  border: 1px solid #fde68a;
}
.pill-error {
  background: #fef2f2;
  color: #991b1b;
  border: 1px solid #fecaca;
}
.pill-info {
  background: #f8fafc;
  color: #475569;
  border: 1px solid #e2e8f0;
}
.pill-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}
.dot-green {
  background: #10b981;
  box-shadow: 0 0 8px rgba(16, 185, 129, 0.6);
}
.dot-yellow {
  background: #f59e0b;
  box-shadow: 0 0 8px rgba(245, 158, 11, 0.6);
}
.dot-red {
  background: #ef4444;
  box-shadow: 0 0 8px rgba(239, 68, 68, 0.6);
}

/* Primary Launch CTA - Sleek & Tactile Pill */
.standby-cta-section {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  width: 100%;
}
.btn-standby-launch {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 13px 36px;
  border-radius: 9999px;
  background: linear-gradient(135deg, #2563eb 0%, #3b82f6 100%);
  color: #ffffff;
  border: 1px solid rgba(255, 255, 255, 0.2);
  font-size: 15px;
  font-weight: 600;
  letter-spacing: -0.2px;
  cursor: pointer;
  box-shadow: 0 8px 24px -4px rgba(37, 99, 235, 0.42), 0 2px 6px rgba(37, 99, 235, 0.2);
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
}
.btn-standby-launch:hover {
  transform: translateY(-2px);
  box-shadow: 0 12px 30px -4px rgba(37, 99, 235, 0.52), 0 4px 10px rgba(37, 99, 235, 0.25);
  background: linear-gradient(135deg, #1d4ed8 0%, #2563eb 100%);
}
.btn-standby-launch:active {
  transform: translateY(0);
}
.btn-launch-config {
  background: linear-gradient(135deg, #0284c7 0%, #0ea5e9 100%);
  box-shadow: 0 8px 24px -4px rgba(14, 165, 233, 0.4);
}
.btn-launch-icon-bubble {
  width: 22px;
  height: 22px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.2);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-right: 2px;
}
.launch-btn-svg {
  width: 14px;
  height: 14px;
  flex-shrink: 0;
}
.standby-launch-hint {
  font-size: 12px;
  color: #94a3b8;
  line-height: 1.4;
}

.btn-standby-download {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 6px 16px;
  border-radius: 9999px;
  font-size: 13px;
  font-weight: 500;
  color: #4f46e5;
  background: rgba(79, 70, 229, 0.06);
  border: 1px solid rgba(79, 70, 229, 0.2);
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  text-decoration: none;
}
.btn-standby-download:hover {
  background: rgba(79, 70, 229, 0.12);
  border-color: rgba(79, 70, 229, 0.4);
  transform: translateY(-1px);
}
.btn-standby-download .download-sub-svg {
  width: 14px;
  height: 14px;
  flex-shrink: 0;
}

/* 3. 3-Column Readiness Grid */
.standby-readiness-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
  width: 100%;
}
@media (max-width: 860px) {
  .standby-readiness-grid {
    grid-template-columns: 1fr;
  }
}
.standby-card {
  background: #ffffff;
  border: 1px solid #e8edf5;
  border-radius: 16px;
  padding: 22px 20px;
  display: flex;
  flex-direction: column;
  cursor: pointer;
  transition: all 0.25s cubic-bezier(0.16, 1, 0.3, 1);
  box-shadow: 0 4px 16px rgba(15, 23, 42, 0.035);
  position: relative;
}
.standby-card:hover {
  transform: translateY(-4px);
  border-color: #cbd5e1;
  box-shadow: 0 12px 28px rgba(15, 23, 42, 0.08);
}
.standby-card.card-unconfigured {
  border-color: #fde68a;
  background: #fffdfa;
}
.standby-card.card-unconfigured:hover {
  border-color: #f59e0b;
}
.standby-card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
}
.standby-card-icon-box {
  width: 40px;
  height: 40px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: transform 0.2s ease;
}
.standby-card:hover .standby-card-icon-box {
  transform: scale(1.05);
}
.standby-card-icon-box svg {
  width: 20px;
  height: 20px;
}
.standby-card-icon-box.bg-blue {
  background: linear-gradient(135deg, #eff6ff 0%, #dbeafe 100%);
  color: #2563eb;
  border: 1px solid #bfdbfe;
}
.standby-card-icon-box.bg-purple {
  background: linear-gradient(135deg, #f5f3ff 0%, #ede9fe 100%);
  color: #7c3aed;
  border: 1px solid #ddd6fe;
}
.standby-card-icon-box.bg-amber {
  background: linear-gradient(135deg, #fffbeb 0%, #fef3c7 100%);
  color: #d97706;
  border: 1px solid #fde68a;
}
.standby-card-icon-box.bg-emerald {
  background: linear-gradient(135deg, #ecfdf5 0%, #d1fae5 100%);
  color: #059669;
  border: 1px solid #a7f3d0;
}
.standby-card-tag {
  font-size: 11.5px;
  font-weight: 600;
  color: #64748b;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  padding: 3px 10px;
  border-radius: 9999px;
}
.standby-card-tag.tag-warning {
  color: #b45309;
  background: #fef3c7;
  border-color: #fde68a;
}

.standby-card-body {
  flex: 1;
}
.standby-card-title {
  font-size: 15.5px;
  font-weight: 700;
  color: #0f172a;
  margin: 0 0 8px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  letter-spacing: -0.2px;
}
.standby-card-title.text-amber {
  color: #b45309;
}
.standby-card-metric {
  display: flex;
  align-items: baseline;
  gap: 6px;
  margin-bottom: 10px;
}
.metric-big {
  font-size: 26px;
  font-weight: 800;
  color: #0f172a;
  line-height: 1;
  letter-spacing: -0.5px;
}
.metric-unit {
  font-size: 12.5px;
  font-weight: 500;
  color: #64748b;
}
.metric-warning-text {
  font-size: 13.5px;
  font-weight: 600;
  color: #d97706;
}
.metric-code {
  font-family: var(--ds-font-family-code, monospace);
  font-size: 14px;
  font-weight: 700;
  color: #059669;
  background: #ecfdf5;
  padding: 2px 8px;
  border-radius: 6px;
}

.standby-vram-bar {
  width: 100%;
  height: 6px;
  background: #e2e8f0;
  border-radius: 3px;
  overflow: hidden;
  margin-bottom: 10px;
}
.standby-vram-fill {
  height: 100%;
  border-radius: 3px;
  transition: width 0.3s ease;
}

.standby-tags-row {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  margin-bottom: 8px;
}
.tech-badge {
  font-size: 11px;
  font-weight: 500;
  color: #475569;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  padding: 3px 8px;
  border-radius: 6px;
  line-height: 1.35;
}

.standby-card-desc {
  font-size: 12px;
  color: #64748b;
  line-height: 1.5;
  margin: 8px 0 0;
  min-height: 36px;
}

.standby-card-footer {
  margin-top: 16px;
  padding-top: 12px;
  border-top: 1px solid #f1f5f9;
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 12.5px;
  font-weight: 600;
  color: #2563eb;
  transition: color 0.15s ease;
}
.standby-card:hover .standby-card-footer {
  color: #1d4ed8;
}
.card-arrow {
  transition: transform 0.2s ease;
}
.standby-card:hover .card-arrow {
  transform: translateX(4px);
}

/* 4. Bottom Hint */
.standby-bottom-bar {
  text-align: center;
  font-size: 12px;
  color: #94a3b8;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 4px 0;
}
.standby-hint-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #cbd5e1;
}

/* ==============================================================
   TAB 2: Starting State -> Modern Dedicated Friendly Loading Viewport
   ============================================================== */
.chat-viewport-starting {
  width: 100%;
  height: 100%;
  border-radius: 12px;
  overflow-y: auto;
  background: radial-gradient(circle at 50% 12%, rgba(238, 242, 255, 0.95), #ffffff 65%);
  border: 1px solid #e0e7ff;
  box-shadow: 0 4px 20px rgba(99, 102, 241, 0.05);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 32px 24px;
  box-sizing: border-box;
}

.starting-container {
  max-width: 780px;
  width: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 20px;
}

/* AI Core Hero */
.starting-core-hero {
  position: relative;
  width: 80px;
  height: 80px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.core-pulse-rings {
  position: absolute;
  inset: -14px;
  pointer-events: none;
}

.pulse-ring {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  border: 2px solid rgba(99, 102, 241, 0.35);
  animation: pulse-ring-anim 2.4s cubic-bezier(0.215, 0.61, 0.355, 1) infinite;
}

.pulse-ring.ring-2 {
  animation-delay: 1.2s;
}

@keyframes pulse-ring-anim {
  0% { transform: scale(0.75); opacity: 0.85; }
  100% { transform: scale(1.4); opacity: 0; }
}

.core-central-squircle {
  width: 68px;
  height: 68px;
  border-radius: 20px;
  background: #090d16;
  border: 1.5px solid rgba(56, 189, 248, 0.4);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 8px 24px rgba(15, 23, 42, 0.45), 0 0 20px rgba(56, 189, 248, 0.25);
  position: relative;
  z-index: 2;
}

.core-svg-icon {
  width: 42px;
  height: 42px;
  filter: drop-shadow(0 2px 8px rgba(56, 189, 248, 0.35));
  animation: float-gentle 3s ease-in-out infinite;
}

@keyframes float-gentle {
  0%, 100% { transform: translateY(0); }
  50% { transform: translateY(-3px); }
}

/* Header Text */
.starting-hero-header {
  text-align: center;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
}

.starting-hero-title {
  font-size: 21px;
  font-weight: 700;
  color: #0f172a;
  letter-spacing: -0.02em;
  margin: 0;
}

.starting-hero-subtitle {
  font-size: 13.5px;
  color: #64748b;
  margin: 0;
  max-width: 620px;
  line-height: 1.5;
}

.starting-hero-tags {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  justify-content: center;
  margin-top: 4px;
}

.hero-tag-pill {
  font-size: 11.5px;
  background: #f1f5f9;
  color: #334155;
  border: 1px solid #e2e8f0;
  padding: 3px 10px;
  border-radius: 14px;
  font-weight: 500;
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.tag-dot-green {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #10b981;
  box-shadow: 0 0 6px #10b981;
}

/* Progress Card */
.starting-progress-card {
  width: 100%;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 14px 18px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.02);
  display: flex;
  flex-direction: column;
  gap: 10px;
  box-sizing: border-box;
}

.progress-info-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.progress-step-text {
  font-size: 13.5px;
  font-weight: 600;
  color: #1e293b;
  display: flex;
  align-items: center;
  gap: 8px;
}

.mini-spin-dot {
  width: 13px;
  height: 13px;
  border: 2px solid #cbd5e1;
  border-top-color: #4f46e5;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
  display: inline-block;
}

.progress-percent-val {
  font-size: 15px;
  font-weight: 700;
  color: #4f46e5;
  font-variant-numeric: tabular-nums;
}

.progress-track-wrapper {
  width: 100%;
  height: 8px;
  background: #f1f5f9;
  border-radius: 6px;
  overflow: hidden;
  position: relative;
}

.progress-fill-bar {
  height: 100%;
  background: linear-gradient(90deg, #4f46e5, #6366f1, #06b6d4, #10b981);
  border-radius: 6px;
  transition: width 0.4s ease-out;
  position: relative;
  overflow: hidden;
}

.progress-shimmer {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: linear-gradient(90deg, transparent, rgba(255,255,255,0.45), transparent);
  animation: shimmer-anim 1.8s infinite;
}

@keyframes shimmer-anim {
  0% { transform: translateX(-100%); }
  100% { transform: translateX(100%); }
}

/* 4-Stage Cards Grid */
.starting-stages-grid {
  width: 100%;
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
}

.stage-card {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 12px 14px;
  display: flex;
  align-items: center;
  gap: 12px;
  transition: all 0.25s ease;
  box-sizing: border-box;
}

.stage-card.stage-done {
  border-color: #bbf7d0;
  background: #f0fdf4;
}

.stage-card.stage-active {
  border-color: #c7d2fe;
  background: #f5f7ff;
  box-shadow: 0 0 0 1px #818cf8;
}

.stage-card.stage-pending {
  opacity: 0.65;
  background: #fafafa;
}

.stage-icon-circle {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 13px;
  font-weight: 700;
  flex-shrink: 0;
}

.stage-done .stage-icon-circle {
  background: #10b981;
  color: #ffffff;
}

.stage-active .stage-icon-circle {
  background: #4f46e5;
  color: #ffffff;
}

.stage-pending .stage-icon-circle {
  background: #e2e8f0;
  color: #94a3b8;
}

.stage-text-block {
  flex: 1;
  min-width: 0;
}

.stage-title {
  font-size: 13px;
  font-weight: 600;
  color: #1e293b;
  margin-bottom: 2px;
}

.stage-desc {
  font-size: 11px;
  color: #64748b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.stage-status-badge {
  font-size: 11px;
  font-weight: 500;
  padding: 2px 7px;
  border-radius: 6px;
  flex-shrink: 0;
}

.stage-done .stage-status-badge {
  background: #dcfce7;
  color: #15803d;
}

.stage-active .stage-status-badge {
  background: #e0e7ff;
  color: #3730a3;
}

.stage-pending .stage-status-badge {
  background: #f1f5f9;
  color: #94a3b8;
}

/* Log Terminal Mini Drawer */
.starting-log-terminal {
  width: 100%;
  background: #0f172a;
  border-radius: 10px;
  overflow: hidden;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.08);
}

.terminal-bar {
  background: #1e293b;
  padding: 7px 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.terminal-dots {
  display: flex;
  gap: 5px;
}

.t-dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
}
.t-dot.red { background: #ef4444; }
.t-dot.yellow { background: #f59e0b; }
.t-dot.green { background: #10b981; }

.terminal-title {
  font-size: 11.5px;
  color: #94a3b8;
  font-family: monospace;
}

.terminal-btn-logs {
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.15);
  color: #cbd5e1;
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.15s;
}

.terminal-btn-logs:hover {
  background: rgba(255, 255, 255, 0.15);
  color: #ffffff;
}

.terminal-body {
  padding: 10px 14px;
  max-height: 96px;
  overflow-y: auto;
  font-family: 'Cascadia Code', Consolas, Monaco, monospace;
  font-size: 11px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.terminal-line {
  color: #e2e8f0;
  line-height: 1.4;
  word-break: break-all;
}

.terminal-line.placeholder {
  color: #64748b;
  font-style: italic;
}

.terminal-prefix {
  font-weight: 600;
  margin-right: 6px;
}

.prefix-engine { color: #38bdf8; }
.prefix-guard { color: #4ade80; }
.prefix-dsh { color: #c084fc; }
.prefix-system { color: #fbbf24; }

/* Bottom Bar */
.starting-bottom-bar {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}

.starting-hint-text {
  font-size: 12px;
  color: #64748b;
  line-height: 1.4;
}

.btn-cancel-startup {
  background: #ffffff;
  border: 1px solid #cbd5e1;
  color: #475569;
  font-size: 12px;
  font-weight: 500;
  padding: 6px 14px;
  border-radius: 6px;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s;
}

.btn-cancel-startup:hover {
  background: #fee2e2;
  border-color: #fca5a5;
  color: #b91c1c;
}

/* ==============================================================
   Compact Error Alert Banner (Top area only)
   ============================================================== */
.chat-error-strip {
  background: rgba(254, 242, 242, 0.96);
  border-bottom: 1.5px solid #fecaca;
  padding: 10px 24px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-shrink: 0;
  box-shadow: 0 2px 8px rgba(239, 68, 68, 0.05);
}

.error-strip-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.error-strip-badge {
  font-size: 18px;
  line-height: 1;
}

.error-text-col {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.error-strip-title {
  font-size: 13px;
  font-weight: 700;
  color: #b91c1c;
  letter-spacing: -0.2px;
}

.error-strip-desc {
  font-size: 12px;
  color: #475569;
  line-height: 1.4;
  font-weight: 500;
}

.error-strip-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.btn-error-primary {
  background: #ef4444;
  color: #ffffff;
  border: none;
  border-radius: 6px;
  padding: 5px 12px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-error-primary:hover {
  background: #dc2626;
}

.btn-error-subtle {
  background: #ffffff;
  color: #475569;
  border: 1px solid #fca5a5;
  border-radius: 6px;
  padding: 5px 10px;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-error-subtle:hover {
  background: #fee2e2;
  color: #991b1b;
}

/* Standby Status Strip at Top */
.chat-standby-strip {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 20px;
  background: #f1f5f9;
  border-bottom: 1px solid #e2e8f0;
  flex-shrink: 0;
}

.standby-strip-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.standby-status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #94a3b8;
}

.standby-status-title {
  font-size: 13px;
  font-weight: 600;
  color: #475569;
}

.standby-status-desc {
  font-size: 12px;
  color: #94a3b8;
}

.standby-strip-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.standby-model-badge {
  font-size: 12px;
  color: #64748b;
  background: #e2e8f0;
  padding: 3px 8px;
  border-radius: 6px;
  font-family: monospace;
  max-width: 240px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.btn-standby-start {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 14px;
  background: #3b82f6;
  color: #ffffff;
  border: none;
  border-radius: 6px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  box-shadow: 0 2px 6px rgba(59, 130, 246, 0.25);
  transition: all 0.15s ease;
}

.btn-standby-start:hover {
  background: #2563eb;
  transform: translateY(-1px);
}

.btn-standby-svg {
  width: 12px;
  height: 12px;
}

/* ==============================================================
   DSH Mimic Twin Workspace (1:1 with DSH Web UI)
   ============================================================== */
.dsh-mimic-workspace {
  display: flex !important;
  flex-direction: row !important;
  width: 100%;
  height: 100%;
  background: #ffffff;
  overflow: hidden;
  border-radius: 0;
  border: none;
}

/* 1. Left Sessions Sidebar */
.dsh-mimic-sidebar {
  width: 230px;
  min-width: 230px;
  max-width: 230px;
  height: 100%;
  background: #ffffff;
  border-right: 1px solid #e2e8f0;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  box-sizing: border-box;
}

.dsh-sidebar-top {
  display: flex;
  flex-direction: column;
  padding: 12px 14px 8px;
  gap: 10px;
  overflow-y: auto;
}

.dsh-sidebar-header-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 4px;
}

.dsh-sidebar-title {
  font-size: 15px;
  font-weight: 600;
  color: #0f172a;
  letter-spacing: -0.2px;
}

.dsh-icon-btn {
  background: transparent;
  border: none;
  cursor: pointer;
  color: #64748b;
  padding: 4px;
  border-radius: 4px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.dsh-icon-btn:hover {
  background: #f1f5f9;
  color: #0f172a;
}

.dsh-mini-svg {
  width: 15px;
  height: 15px;
}

.dsh-btn-new-chat {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 8px 12px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  font-size: 13px;
  font-weight: 500;
  color: #0f172a;
  cursor: pointer;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
  transition: all 0.15s ease;
}
.dsh-btn-new-chat:hover {
  background: #f8fafc;
  border-color: #cbd5e1;
  color: #2563eb;
}

.dsh-btn-plus-svg {
  width: 14px;
  height: 14px;
  color: #2563eb;
}

.dsh-plus-circle {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  border: 1px solid #94a3b8;
  font-size: 13px;
  line-height: 1;
  color: #64748b;
}

.dsh-section-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4px 6px 0;
  font-size: 12px;
  font-weight: 500;
  color: #64748b;
}

.dsh-section-actions {
  display: flex;
  gap: 6px;
}

.dsh-icon-link {
  background: none;
  border: none;
  color: #94a3b8;
  cursor: pointer;
  font-size: 12px;
  padding: 2px 4px;
  border-radius: 3px;
}
.dsh-icon-link:hover {
  color: #0f172a;
  background: #f1f5f9;
}

.dsh-sessions-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin-top: 4px;
}

/* Truthful Clean Empty State: NO fake folders or sessions! */
.dsh-empty-sessions {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 32px 14px;
  text-align: center;
  border: 1px dashed #e2e8f0;
  border-radius: 10px;
  background: #f8fafc;
  margin-top: 10px;
}

.dsh-empty-icon-wrap {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 8px;
  color: #94a3b8;
}

.dsh-empty-svg {
  width: 18px;
  height: 18px;
}

.dsh-empty-label {
  font-size: 12.5px;
  font-weight: 600;
  color: #475569;
  margin-bottom: 2px;
}

.dsh-empty-subtext {
  font-size: 11px;
  color: #94a3b8;
}

.dsh-sidebar-bottom {
  border-top: 1px solid #f1f5f9;
  padding: 10px 14px;
}

.dsh-btn-settings {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 8px;
  background: transparent;
  border: none;
  border-radius: 6px;
  font-size: 12.5px;
  color: #64748b;
  cursor: pointer;
  transition: all 0.15s;
}
.dsh-btn-settings:hover {
  background: #f1f5f9;
  color: #0f172a;
}

/* 2. Right Main Chat Area */
.dsh-mimic-main {
  flex: 1;
  height: 100%;
  display: flex;
  flex-direction: column;
  background: #ffffff;
  position: relative;
  overflow: hidden;
}

/* Top Toolbar */
.dsh-main-topbar {
  height: 48px;
  min-height: 48px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  border-bottom: 1px solid #f1f5f9;
  background: #ffffff;
}

.dsh-topbar-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.dsh-chat-title {
  font-size: 14px;
  font-weight: 600;
  color: #0f172a;
}

.dsh-model-engine-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 3px 8px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  font-size: 11.5px;
  color: #475569;
  cursor: pointer;
  white-space: nowrap;
  flex-shrink: 0;
}
.dsh-model-engine-badge:hover {
  background: #f1f5f9;
  border-color: #cbd5e1;
}

.dsh-badge-sep {
  color: #cbd5e1;
}

.dsh-arrow-down {
  font-size: 9px;
  color: #94a3b8;
}

.dsh-tab-switcher {
  display: inline-flex;
  gap: 4px;
  margin-left: 6px;
}

.dsh-subtab {
  background: none;
  border: none;
  padding: 4px 8px;
  font-size: 12.5px;
  border-radius: 5px;
  cursor: pointer;
}
.dsh-subtab.active {
  color: #2563eb;
  font-weight: 600;
  background: #eff6ff;
}
.dsh-subtab.disabled {
  color: #94a3b8;
  cursor: default;
}

.dsh-topbar-right {
  display: flex;
  align-items: center;
  gap: 6px;
}

.dsh-tool-icon {
  background: none;
  border: none;
  color: #64748b;
  font-size: 13px;
  padding: 5px 8px;
  border-radius: 5px;
  cursor: pointer;
}
.dsh-tool-icon:hover {
  background: #f1f5f9;
  color: #0f172a;
}

/* Chat Stream & Hero State */
.dsh-chat-stream {
  flex: 1;
  overflow-y: auto;
  padding: 24px 28px 12px 28px;
  display: flex;
  flex-direction: column;
}

.dsh-stream-inner {
  max-width: 920px;
  width: 100%;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

/* Hero Presentation Card (Modern, Centered, Aesthetic) */
.dsh-welcome-hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  padding: 32px 20px 20px;
  margin-top: 16px;
}

.dsh-hero-avatar-box {
  margin-bottom: 18px;
}

.dsh-hero-avatar-squircle {
  width: 62px;
  height: 62px;
  border-radius: 18px;
  background: #090d16;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 10px 25px -4px rgba(56, 189, 248, 0.3), 0 8px 12px -6px rgba(99, 102, 241, 0.25);
  border: 1px solid rgba(255, 255, 255, 0.12);
  transition: transform 0.25s cubic-bezier(0.16, 1, 0.3, 1), box-shadow 0.25s;
}
.dsh-hero-avatar-squircle:hover {
  transform: scale(1.06) translateY(-2px);
  box-shadow: 0 14px 28px -4px rgba(56, 189, 248, 0.4);
}

.dsh-hero-avatar-svg {
  width: 44px;
  height: 44px;
}

.dsh-hero-title {
  font-size: 21px;
  font-weight: 700;
  color: #0f172a;
  letter-spacing: -0.4px;
  margin: 0 0 8px 0;
}

.dsh-hero-desc {
  font-size: 13.5px;
  color: #64748b;
  margin: 0 0 16px 0;
  max-width: 580px;
  line-height: 1.5;
}

.dsh-hero-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 12px;
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  border-radius: 20px;
  font-size: 11.5px;
  color: #475569;
}

.dsh-chip-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #94a3b8;
  box-shadow: 0 0 6px rgba(148, 163, 184, 0.6);
  flex-shrink: 0;
  transition: all 0.2s ease;
}

.dsh-chip-dot.dot-stopped {
  background: #94a3b8;
  box-shadow: 0 0 6px rgba(148, 163, 184, 0.6);
}

.dsh-chip-dot.dot-starting {
  background: #3b82f6;
  box-shadow: 0 0 6px rgba(59, 130, 246, 0.8);
  animation: pulse-dot 1.5s infinite;
}

.dsh-chip-dot.dot-error {
  background: #ef4444;
  box-shadow: 0 0 6px rgba(239, 68, 68, 0.8);
}

/* Quick-Start 3-Cards Grid */
.dsh-quick-cards-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 12px;
  width: 100%;
  margin-top: 12px;
  padding: 0 4px;
}

.dsh-feature-card {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 14px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.02);
  cursor: pointer;
  text-align: left;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
}
.dsh-feature-card:hover {
  background: #ffffff;
  border-color: #3b82f6;
  transform: translateY(-2px);
  box-shadow: 0 8px 20px -4px rgba(59, 130, 246, 0.12), 0 3px 6px -2px rgba(0, 0, 0, 0.04);
}

.dsh-fcard-icon-wrap {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.bg-blue-subtle {
  background: #eff6ff;
  border: 1px solid #dbeafe;
}
.bg-purple-subtle {
  background: #faf5ff;
  border: 1px solid #f3e8ff;
}
.bg-emerald-subtle {
  background: #ecfdf5;
  border: 1px solid #d1fae5;
}

.dsh-fcard-icon {
  font-size: 17px;
}

.dsh-fcard-texts {
  display: flex;
  flex-direction: column;
  gap: 2px;
  flex: 1;
  min-width: 0;
}

.dsh-fcard-title {
  font-size: 13px;
  font-weight: 600;
  color: #1e293b;
}

.dsh-fcard-desc {
  font-size: 11.5px;
  color: #64748b;
  line-height: 1.4;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.dsh-fcard-arrow {
  font-size: 14px;
  color: #cbd5e1;
  font-weight: 600;
  transition: all 0.15s ease;
  flex-shrink: 0;
}
.dsh-feature-card:hover .dsh-fcard-arrow {
  color: #3b82f6;
  transform: translateX(3px);
}

/* Hero Prominent Model Launch Button & Tip Section */
.dsh-hero-launch-section {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  margin-top: 20px;
  width: 100%;
}

.dsh-hero-btn-launch {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 13px 40px;
  background: linear-gradient(135deg, #2563eb 0%, #1d4ed8 100%);
  color: #ffffff;
  font-size: 15px;
  font-weight: 600;
  border-radius: 12px;
  border: 1px solid rgba(255, 255, 255, 0.18);
  box-shadow: 0 4px 16px rgba(37, 99, 235, 0.38), 0 1px 3px rgba(0, 0, 0, 0.08);
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
}
.dsh-hero-btn-launch:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(37, 99, 235, 0.48);
  background: linear-gradient(135deg, #1d4ed8 0%, #1e40af 100%);
}
.dsh-hero-btn-launch:active:not(:disabled) {
  transform: translateY(0);
  box-shadow: 0 2px 8px rgba(37, 99, 235, 0.3);
}
.dsh-hero-btn-launch:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

.dsh-launch-play-svg {
  width: 16px;
  height: 16px;
}

.dsh-launch-spinner {
  width: 16px;
  height: 16px;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-top-color: #ffffff;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

.dsh-hero-tip-card {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 8px 18px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 20px;
  font-size: 12px;
  color: #64748b;
  max-width: 580px;
  line-height: 1.5;
  text-align: center;
}

.dsh-tip-icon-svg {
  width: 15px;
  height: 15px;
  color: #2563eb;
  flex-shrink: 0;
}

.dsh-stream-footer-line {
  margin-top: 24px;
  font-size: 11.5px;
  color: #94a3b8;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
}

.dsh-status-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #10b981;
}

/* ==============================================================
   Custom Gorgeous Effort Dropdown & Popover (No native select!)
   ============================================================== */
.custom-effort-dropdown-wrapper {
  position: relative;
  display: inline-flex;
  align-items: center;
}

.effort-trigger-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 11px;
  background: #f8fafc;
  border: 1px solid #cbd5e1;
  border-radius: 14px;
  font-size: 11.5px;
  color: #334155;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s ease;
  outline: none;
}

.effort-trigger-btn:hover {
  background: #f1f5f9;
  border-color: #94a3b8;
}

.effort-trigger-btn.is-active {
  background: #eff6ff;
  border-color: #2563eb;
  color: #1d4ed8;
  box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.12);
}

.running-trigger-btn {
  background: #ffffff;
  border: 1px solid #cbd5e1;
  border-radius: 6px;
}

.effort-icon {
  font-size: 12px;
}

.effort-txt-label {
  color: #64748b;
  font-weight: 500;
}

.effort-current-value {
  color: #2563eb;
  font-weight: 700;
  text-transform: lowercase;
}

.chevron-arrow {
  width: 12px;
  height: 12px;
  color: #64748b;
  transition: transform 0.2s ease;
}

.chevron-arrow.rotated {
  transform: rotate(180deg);
}

/* Floating Popover Menu (Upward from dock) */
.effort-popover-menu {
  position: absolute;
  bottom: calc(100% + 8px);
  left: 0;
  min-width: 260px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  box-shadow: 0 12px 28px -4px rgba(0, 0, 0, 0.14), 0 6px 12px -3px rgba(0, 0, 0, 0.08);
  padding: 6px;
  z-index: 1000;
  animation: popover-slide-up 0.15s ease-out;
}

.effort-popover-menu.popover-align-right {
  top: calc(100% + 6px);
  bottom: auto;
  right: 0;
  left: auto;
}

@keyframes popover-slide-up {
  from {
    opacity: 0;
    transform: translateY(4px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.popover-header {
  padding: 6px 10px 4px 10px;
  border-bottom: 1px solid #f1f5f9;
  margin-bottom: 4px;
}

.popover-header-title {
  font-size: 11px;
  font-weight: 600;
  color: #64748b;
}

.popover-items-list {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.effort-option-row {
  width: 100%;
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 8px 10px;
  border-radius: 8px;
  border: none;
  background: transparent;
  text-align: left;
  cursor: pointer;
  transition: all 0.12s ease;
}

.effort-option-row:hover {
  background: #f1f5f9;
}

.effort-option-row.selected {
  background: #eff6ff;
}

.opt-icon {
  font-size: 15px;
  margin-top: 1px;
  flex-shrink: 0;
}

.opt-text-col {
  flex: 1;
  display: flex;
  flex-direction: column;
}

.opt-name-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.opt-name {
  font-size: 13px;
  font-weight: 600;
  color: #1e293b;
}

.effort-option-row.selected .opt-name {
  color: #2563eb;
}

.opt-check-badge {
  font-size: 12px;
  color: #2563eb;
  font-weight: 700;
}

.opt-desc {
  font-size: 11px;
  color: #64748b;
  margin-top: 1px;
}

.mode-dot-gray {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #94a3b8;
}

.input-actions-right {
  display: flex;
  align-items: center;
}

.btn-input-send {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  border: none;
  background: #e2e8f0;
  color: #94a3b8;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.15s;
}

.send-svg {
  width: 14px;
  height: 14px;
}

.chat-dock-disclaimer {
  margin-top: 8px;
  font-size: 11px;
  color: #94a3b8;
  text-align: center;
}

/* Missing Model Warning Alert */
.model-missing-alert {
  padding: 12px 16px;
  background: #fffbeb;
  border: 1px solid #fde68a;
  border-radius: 10px;
  display: flex;
  align-items: flex-start;
  gap: 12px;
  color: #92400e;
  margin-bottom: 18px;
}

.missing-alert-icon {
  font-size: 18px;
  line-height: 1.2;
}

.missing-alert-content strong {
  font-size: 13px;
}

.missing-alert-content p {
  margin: 3px 0 0 0;
  font-size: 12px;
  line-height: 1.5;
}

/* Loading Steps */
.loading-title {
  font-size: 17px;
  font-weight: 700;
  color: #0f172a;
  margin-top: 14px;
}

.loading-subtitle {
  font-size: 12.5px;
  color: #64748b;
  margin-bottom: 20px;
}

.loading-checklist {
  display: flex;
  flex-direction: column;
  gap: 8px;
  text-align: left;
  width: 100%;
  margin-bottom: 20px;
}

.check-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12.5px;
  color: #64748b;
}

.check-item.active {
  color: #0f172a;
  font-weight: 500;
}

.check-icon {
  width: 18px;
  height: 18px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
  background: #f1f5f9;
  color: #94a3b8;
}

.check-item.active .check-icon {
  background: #dcfce7;
  color: #166534;
  font-weight: 700;
}

.mini-live-terminal {
  width: 100%;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 10px 12px;
  text-align: left;
  font-family: monospace;
  font-size: 11px;
  color: #334155;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

/* Spinner */
.orbit-spinner {
  width: 44px;
  height: 44px;
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
}

.orbit-circle {
  position: absolute;
  border-radius: 50%;
  border: 2px solid transparent;
}

.circle-1 {
  width: 44px;
  height: 44px;
  border-top-color: #2563eb;
  animation: spin 1s linear infinite;
}

.circle-2 {
  width: 32px;
  height: 32px;
  border-right-color: #4f46e5;
  animation: spin 0.8s linear infinite reverse;
}

.circle-3 {
  width: 20px;
  height: 20px;
  border-bottom-color: #06b6d4;
  animation: spin 0.6s linear infinite;
}

/* Error Box */
.error-icon-badge {
  font-size: 32px;
  margin-bottom: 10px;
}

.error-h2 {
  font-size: 17px;
  font-weight: 700;
  color: #b91c1c;
  margin-bottom: 6px;
}

.error-desc {
  font-size: 12.5px;
  color: #64748b;
  margin-bottom: 20px;
}

.error-btns-row {
  display: flex;
  gap: 10px;
}

/* ==============================================================
   TAB 3: 显存管理 (VRAM Page)
   ============================================================== */
.page-vram {
  display: flex;
  flex-direction: column;
  gap: 16px;
  max-width: 1100px;
  margin: 0 auto;
}

.vram-telemetry-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
  gap: 14px;
}

.vram-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 10px;
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
  gap: 3px;
  box-shadow: 0 2px 6px rgba(0,0,0,0.02);
}

.vram-card-lbl {
  font-size: 11.5px;
  color: #64748b;
  font-weight: 500;
}

.vram-card-num {
  font-size: 18px;
  font-weight: 700;
  font-family: monospace;
  color: #0f172a;
}

.vram-card-sub {
  font-size: 11px;
  color: #94a3b8;
}

.vram-notice-strip {
  padding: 12px 16px;
  border-radius: 10px;
  display: flex;
  align-items: flex-start;
  gap: 10px;
  font-size: 12.5px;
}

.notice-warn {
  background: #fffbeb;
  border: 1px solid #fde68a;
  color: #92400e;
}

.notice-ok {
  background: #f0fdf4;
  border: 1px solid #bbf7d0;
  color: #166534;
}

.notice-text-group p {
  margin: 4px 0 0 0;
  font-size: 12px;
  line-height: 1.5;
}

.vram-action-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.vram-tabs-group {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.vram-filter-pill {
  padding: 4px 12px;
  font-size: 12px;
  font-weight: 500;
  border: 1px solid #e2e8f0;
  border-radius: 20px;
  background: #ffffff;
  color: #64748b;
  cursor: pointer;
  transition: all 0.15s;
}

.vram-filter-pill:hover {
  color: #0f172a;
  border-color: #cbd5e1;
}

.vram-filter-pill.active {
  background: #edf2fe;
  color: #2563eb;
  border-color: #93c5fd;
  font-weight: 600;
}

.vram-tools-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.vram-search-field {
  padding: 5px 12px;
  font-size: 12px;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  outline: none;
  background: #ffffff;
}

.vram-search-field:focus {
  border-color: #2563eb;
  box-shadow: 0 0 0 2px rgba(37,99,235,0.1);
}

.btn-tool-subtle {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 5px 12px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  font-size: 12px;
  color: #475569;
  cursor: pointer;
}

.btn-tool-subtle:hover {
  background: #f8fafc;
  color: #0f172a;
}

.tool-svg {
  width: 13px;
  height: 13px;
}

.btn-tool-subtle.is-spinning .tool-svg {
  animation: spin 0.8s linear infinite;
}

.vram-table-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 12px;
  box-shadow: 0 2px 8px rgba(0,0,0,0.02);
  overflow: hidden;
}

.vram-data-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12.5px;
  text-align: left;
}

.vram-data-table th {
  background: #f8fafc;
  padding: 10px 14px;
  font-weight: 600;
  color: #64748b;
  font-size: 11.5px;
  border-bottom: 1px solid #eef2f7;
  white-space: nowrap !important;
}

.vram-data-table td {
  padding: 10px 14px;
  border-bottom: 1px solid #f8fafc;
  vertical-align: middle;
  white-space: nowrap !important;
}

.vram-data-table tr:hover td {
  background: #f8fafc;
}

.vram-data-table tr.tr-engine td {
  background: #f0fdf4;
}

.td-rank {
  font-weight: 700;
  font-family: monospace;
  color: #94a3b8;
}

.td-rank-top {
  color: #2563eb;
}

.td-proc-name {
  font-weight: 600;
  color: #0f172a;
}

.cat-pill {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 4px;
  font-weight: 500;
  white-space: nowrap !important;
  display: inline-block !important;
}

.cat-badge-client { background: #eff6ff; color: #2563eb; border: 1px solid #bfdbfe; }
.cat-badge-engine { background: #eff6ff; color: #2563eb; }
.cat-badge-browser { background: #fef3c7; color: #d97706; }
.cat-badge-chat { background: #fdf2f8; color: #db2777; }
.cat-badge-media { background: #f5f3ff; color: #7c3aed; }
.cat-badge-ide { background: #ecfeff; color: #0891b2; }
.cat-badge-system { background: #f1f5f9; color: #475569; }
.cat-badge-driver { background: #f0fdf4; color: #16a34a; }
.cat-badge-other { background: #f8fafc; color: #64748b; }

.td-pid code {
  font-family: monospace;
  font-size: 11.5px;
  color: #64748b;
}

.td-vram-mb {
  font-family: monospace;
  font-weight: 700;
  color: #0f172a;
}

.ratio-bar-box {
  display: flex;
  align-items: center;
  gap: 8px;
}

.ratio-track {
  width: 60px;
  height: 6px;
  background: #f1f5f9;
  border-radius: 3px;
  overflow: hidden;
}

.ratio-fill {
  height: 100%;
  background: #3b82f6;
  border-radius: 3px;
}

.ratio-fill.fill-orange {
  background: #ea580c;
}

.ratio-text {
  font-size: 11px;
  font-family: monospace;
  color: #64748b;
}

.btn-kill-app {
  padding: 4px 10px;
  font-size: 11px;
  border: 1px solid #fecaca;
  border-radius: 4px;
  background: #ffffff;
  color: #dc2626;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-kill-app:hover:not(:disabled) {
  background: #fee2e2;
}

.badge-client-protect {
  font-size: 11px;
  padding: 2px 8px;
  background: #eff6ff;
  color: #2563eb;
  border: 1px solid #bfdbfe;
  border-radius: 4px;
  font-weight: 500;
}

.badge-sys-lock {
  font-size: 11px;
  padding: 2px 8px;
  background: #f1f5f9;
  color: #64748b;
  border-radius: 4px;
}

.badge-engine-tag {
  font-size: 11px;
  padding: 2px 8px;
  background: #dcfce7;
  color: #15803d;
  font-weight: 600;
  border-radius: 4px;
}

.table-empty-row {
  text-align: center;
  padding: 30px;
  color: #94a3b8;
}

.vram-bottom-tip {
  text-align: right;
}

.copy-toast {
  font-size: 12px;
  color: #16a34a;
  font-weight: 600;
}

/* ==============================================================
   DUAL-GPU HYBRID CONTROLS & ARCHITECTURE TABS (Option A Styling)
   ============================================================== */
.hybrid-gpu-banner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: linear-gradient(135deg, rgba(238, 242, 255, 0.85) 0%, rgba(240, 253, 250, 0.85) 100%);
  border: 1px solid #c7d2fe;
  border-radius: 12px;
  padding: 14px 20px;
  box-shadow: 0 3px 12px rgba(99, 102, 241, 0.05);
}

.hybrid-banner-left {
  display: flex;
  align-items: center;
  gap: 14px;
}

.hybrid-pulse-icon {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: linear-gradient(135deg, #4f46e5 0%, #06b6d4 100%);
  color: #ffffff;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  box-shadow: 0 2px 8px rgba(79, 70, 229, 0.25);
  animation: pulse-soft 2.5s infinite;
}

.hybrid-pulse-img {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  object-fit: cover;
  box-shadow: 0 2px 8px rgba(79, 70, 229, 0.25);
  animation: pulse-soft 2.5s infinite;
  flex-shrink: 0;
  display: block;
}

@keyframes pulse-soft {
  0%, 100% { transform: scale(1); }
  50% { transform: scale(1.05); box-shadow: 0 4px 14px rgba(79, 70, 229, 0.4); }
}

.hybrid-banner-info {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.hybrid-banner-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13.5px;
  color: #1e1b4b;
}

.hybrid-badge-live {
  font-size: 10.5px;
  font-weight: 600;
  padding: 1px 7px;
  border-radius: 4px;
  background: #e0e7ff;
  color: #4338ca;
}

.hybrid-banner-desc {
  font-size: 12px;
  color: #475569;
  display: flex;
  align-items: center;
  gap: 8px;
}

.hybrid-arrow-sep {
  color: #6366f1;
  font-weight: 700;
}

.btn-batch-migrate {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 16px;
  border-radius: 8px;
  border: 1px solid #4f46e5;
  background: linear-gradient(135deg, #4f46e5 0%, #3b82f6 100%);
  color: #ffffff;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(79, 70, 229, 0.25);
  transition: all 0.15s ease;
}

.btn-batch-migrate:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(79, 70, 229, 0.35);
  filter: brightness(1.05);
}

.btn-batch-migrate:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

/* Main Dual-GPU Selection Tabs */
.gpu-arch-tabs {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 12px;
}

.gpu-tab-btn {
  position: relative;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 18px;
  border-radius: 10px;
  border: 1px solid #e2e8f0;
  background: #ffffff;
  cursor: pointer;
  transition: all 0.18s ease;
  text-align: left;
}

.gpu-tab-btn:hover {
  background: #f8fafc;
  border-color: #cbd5e1;
}

.gpu-tab-btn.gpu-tab-active {
  background: #ffffff;
  border-color: #3b82f6;
  box-shadow: 0 3px 12px rgba(59, 130, 246, 0.08);
}

.gpu-tab-indicator-bar {
  position: absolute;
  top: 0;
  left: 12px;
  right: 12px;
  height: 3px;
  background: linear-gradient(90deg, #3b82f6 0%, #6366f1 100%);
  border-radius: 0 0 3px 3px;
}

.gpu-tab-icon {
  font-size: 22px;
  line-height: 1;
}

.gpu-tab-custom-icon {
  width: 26px;
  height: 26px;
  border-radius: 6px;
  object-fit: cover;
  display: block;
  flex-shrink: 0;
}

.gpu-tab-label-group {
  display: flex;
  flex-direction: column;
  gap: 2px;
  flex: 1;
}

.gpu-tab-title {
  font-size: 13.5px;
  color: #0f172a;
}

.gpu-tab-sub {
  font-size: 11px;
  color: #64748b;
}

.gpu-tab-count {
  font-size: 11px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 999px;
}

.gpu-tab-count.badge-dgpu {
  background: #eff6ff;
  color: #2563eb;
  border: 1px solid #bfdbfe;
}

.gpu-tab-count.badge-igpu {
  background: #f0fdf4;
  color: #16a34a;
  border: 1px solid #bbf7d0;
}

/* Notice for iGPU tab */
.vram-notice-strip.notice-igpu {
  background: #f0fdf4;
  border: 1px solid #bbf7d0;
}

.vram-notice-strip.notice-igpu .notice-icon {
  color: #16a34a;
}

.vram-notice-strip.notice-igpu strong {
  color: #15803d;
}

/* Table cell enhancements */
.proc-name-cell {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.td-proc-path {
  font-size: 10px;
  color: #94a3b8;
  max-width: 260px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.sub-shared-tag {
  font-size: 10px;
  color: #059669;
  background: #ecfdf5;
  border: 1px solid #a7f3d0;
  padding: 1px 4px;
  border-radius: 3px;
  margin-left: 4px;
}

.pref-badge-group {
  display: flex;
  align-items: center;
}

.pref-pill {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 4px;
  font-weight: 500;
}

.pref-pill.pill-igpu {
  background: #f0fdf4;
  color: #15803d;
  border: 1px solid #bbf7d0;
}

.pref-pill.pill-dgpu {
  background: #eff6ff;
  color: #1d4ed8;
  border: 1px solid #bfdbfe;
}

.pref-pill.pill-default {
  background: #f8fafc;
  color: #64748b;
  border: 1px solid #e2e8f0;
}

.table-action-btns {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  justify-content: flex-end;
}

.btn-switch-gpu {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 10px;
  font-size: 11px;
  font-weight: 600;
  border-radius: 5px;
  cursor: pointer;
  transition: all 0.15s ease;
}

.btn-switch-gpu.to-igpu {
  background: linear-gradient(135deg, #eff6ff 0%, #ecfeff 100%);
  border: 1px solid #93c5fd;
  color: #1d4ed8;
}

.btn-switch-gpu.to-igpu:hover:not(:disabled) {
  background: linear-gradient(135deg, #dbeafe 0%, #cffafe 100%);
  border-color: #3b82f6;
  color: #1e40af;
  box-shadow: 0 2px 6px rgba(59, 130, 246, 0.2);
}

.btn-switch-gpu.to-dgpu {
  background: linear-gradient(135deg, #f0fdf4 0%, #eff6ff 100%);
  border: 1px solid #86efac;
  color: #15803d;
}

.btn-switch-gpu.to-dgpu:hover:not(:disabled) {
  background: linear-gradient(135deg, #dcfce7 0%, #dbeafe 100%);
  border-color: #22c55e;
  box-shadow: 0 2px 6px rgba(34, 197, 94, 0.2);
}

.btn-switch-gpu:disabled {
  opacity: 0.65;
  cursor: not-allowed;
}

.btn-kill-app-mini {
  padding: 4px 8px;
  font-size: 11px;
  border: 1px solid #fecaca;
  border-radius: 4px;
  background: #ffffff;
  color: #dc2626;
  cursor: pointer;
}

.btn-kill-app-mini:hover:not(:disabled) {
  background: #fee2e2;
}

.btn-reset-pref-mini {
  padding: 3px 6px;
  font-size: 12px;
  font-weight: 700;
  border: 1px solid #cbd5e1;
  border-radius: 4px;
  background: #f8fafc;
  color: #64748b;
  cursor: pointer;
}

.btn-reset-pref-mini:hover {
  background: #e2e8f0;
  color: #1e293b;
}

.btn-spinner {
  display: inline-block;
  animation: spin 1s linear infinite;
}

/* ==============================================================
   TAB 4: 模型与计算引擎调度中心 (Model & Universal Engine Orchestrator)
   Studio Design System - Harmonized with Telemetry & VRAM Tabs
   ============================================================== */
.page-model {
  display: flex;
  flex-direction: column;
  gap: 16px;
  max-width: 1100px;
  margin: 0 auto;
  padding-bottom: 40px;
  animation: fadeIn 0.18s ease;
}

/* 1. Top Studio Banner (Harmonized) */
.model-top-banner {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 14px;
  padding: 16px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.02);
  flex-wrap: wrap;
}

.model-top-banner .banner-left {
  display: flex;
  align-items: center;
  gap: 14px;
  min-width: 280px;
  flex: 1;
}

.model-top-banner .banner-icon-squircle {
  width: 44px;
  height: 44px;
  border-radius: 12px;
  background: linear-gradient(135deg, #090d16 0%, #1e293b 100%);
  border: 1px solid rgba(56, 189, 248, 0.35);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 4px 12px rgba(15, 23, 42, 0.12);
  color: #38bdf8;
  flex-shrink: 0;
}

.model-top-banner .banner-title-row {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 2px;
  flex-wrap: wrap;
}

.model-top-banner .banner-title {
  font-size: 16px;
  font-weight: 700;
  color: #0f172a;
  letter-spacing: -0.01em;
  margin: 0;
}

.status-live-pill {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 20px;
}

.status-live-pill.live-online {
  background: #ecfdf5;
  color: #059669;
  border: 1px solid #a7f3d0;
}

.status-live-pill.live-standby {
  background: #f8fafc;
  color: #64748b;
  border: 1px solid #e2e8f0;
}

.pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: currentColor;
}

.live-online .pulse-dot {
  animation: pulseGreen 1.5s infinite;
}

.model-top-banner .banner-desc {
  font-size: 12px;
  color: #64748b;
  margin: 0;
  line-height: 1.4;
}

.model-top-banner .banner-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.action-btn-svg {
  width: 14px;
  height: 14px;
  flex-shrink: 0;
}

.btn-banner-subtle {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  font-weight: 600;
  padding: 7px 12px;
  border-radius: 8px;
  border: 1px solid #e2e8f0;
  background: #ffffff;
  color: #475569;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s;
}

.btn-banner-subtle:hover {
  background: #f8fafc;
  color: #0f172a;
  border-color: #cbd5e1;
}

.btn-banner-autotune {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  font-weight: 600;
  padding: 7px 14px;
  border-radius: 8px;
  border: 1px solid #bfdbfe;
  background: #eff6ff;
  color: #2563eb;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s;
}

.btn-banner-autotune:hover {
  background: #dbeafe;
  color: #1d4ed8;
}

.btn-banner-primary {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  font-weight: 600;
  padding: 7px 16px;
  border-radius: 8px;
  border: 1px solid #1d4ed8;
  background: linear-gradient(135deg, #3b82f6 0%, #1d4ed8 100%);
  color: #ffffff;
  cursor: pointer;
  box-shadow: 0 2px 6px rgba(37, 99, 235, 0.25);
  white-space: nowrap;
  transition: all 0.15s;
}

.btn-banner-primary:hover {
  filter: brightness(1.08);
}

.btn-banner-accent {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12.5px;
  font-weight: 600;
  padding: 7px 16px;
  border-radius: 8px;
  border: 1px solid #059669;
  background: linear-gradient(135deg, #10b981 0%, #059669 100%);
  color: #ffffff;
  cursor: pointer;
  box-shadow: 0 2px 6px rgba(16, 185, 129, 0.25);
  white-space: nowrap;
  transition: all 0.15s;
}

.btn-banner-accent:hover {
  filter: brightness(1.08);
}

/* Save Toast */
.model-save-toast {
  align-self: center;
  display: inline-flex;
  align-items: center;
  gap: 8px;
  background: #0f172a;
  color: #f8fafc;
  padding: 8px 18px;
  border-radius: 20px;
  font-size: 12.5px;
  font-weight: 600;
  box-shadow: 0 4px 16px rgba(15, 23, 42, 0.2);
  border: 1px solid #334155;
}

.toast-check-icon {
  color: #10b981;
  font-weight: 800;
}

/* Studio Setup Banner (Modern Frosted Amber Card) */
.studio-setup-banner {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  background: linear-gradient(135deg, rgba(254, 243, 199, 0.6) 0%, rgba(255, 251, 235, 0.9) 60%, rgba(254, 240, 138, 0.35) 100%);
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
  border: 1px solid rgba(245, 158, 11, 0.32);
  border-radius: 14px;
  padding: 16px 22px;
  margin-bottom: 18px;
  box-shadow: 0 4px 18px -2px rgba(245, 158, 11, 0.12), 0 1px 3px rgba(0, 0, 0, 0.03);
  overflow: hidden;
  transition: all 0.25s ease;
}

.studio-setup-banner:hover {
  box-shadow: 0 6px 22px -2px rgba(245, 158, 11, 0.18), 0 2px 5px rgba(0, 0, 0, 0.04);
  border-color: rgba(245, 158, 11, 0.45);
}

.banner-left-glow {
  position: absolute;
  top: 0;
  left: 0;
  bottom: 0;
  width: 4px;
  background: linear-gradient(180deg, #f59e0b 0%, #d97706 100%);
  border-top-left-radius: 14px;
  border-bottom-left-radius: 14px;
}

.banner-content {
  display: flex;
  align-items: center;
  gap: 16px;
  flex: 1;
}

.banner-icon-wrapper {
  width: 40px;
  height: 40px;
  min-width: 40px;
  border-radius: 10px;
  background: linear-gradient(135deg, #fef3c7 0%, #fde68a 100%);
  border: 1px solid rgba(245, 158, 11, 0.35);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 3px 8px rgba(245, 158, 11, 0.15);
  color: #d97706;
}

.banner-svg-icon {
  width: 20px;
  height: 20px;
}

.banner-text-content {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.banner-header-line {
  display: flex;
  align-items: center;
  gap: 10px;
}

.banner-headline {
  font-size: 14.5px;
  font-weight: 700;
  color: #92400e;
  margin: 0;
  letter-spacing: -0.2px;
}

.banner-status-tag {
  display: inline-flex;
  align-items: center;
  font-size: 11px;
  font-weight: 600;
  color: #b45309;
  background: rgba(245, 158, 11, 0.16);
  border: 1px solid rgba(245, 158, 11, 0.28);
  padding: 2px 8px;
  border-radius: 9999px;
  white-space: nowrap;
}

.banner-subtext {
  font-size: 12.5px;
  line-height: 1.5;
  color: #78350f;
  margin: 0;
  opacity: 0.92;
}

.banner-action-area {
  display: flex;
  align-items: center;
  flex-shrink: 0;
}

.banner-cta-button {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 9px 18px;
  background: linear-gradient(135deg, #2563eb 0%, #1d4ed8 100%);
  color: #ffffff;
  font-size: 13px;
  font-weight: 600;
  border-radius: 9px;
  border: none;
  cursor: pointer;
  box-shadow: 0 3px 12px rgba(37, 99, 235, 0.25);
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  white-space: nowrap;
}

.banner-cta-button:hover {
  background: linear-gradient(135deg, #1d4ed8 0%, #1e40af 100%);
  box-shadow: 0 5px 16px rgba(37, 99, 235, 0.35);
  transform: translateY(-1px);
}

.banner-cta-button:active {
  transform: translateY(0);
}

.btn-cta-svg {
  width: 15px;
  height: 15px;
}

/* Alert Transition */
.alert-slide-enter-active,
.alert-slide-leave-active {
  transition: all 0.25s cubic-bezier(0.16, 1, 0.3, 1);
}
.alert-slide-enter-from,
.alert-slide-leave-to {
  opacity: 0;
  transform: translateY(-6px);
}

/* Studio Card Container */
.model-cards-stack {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.studio-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 14px;
  padding: 18px 22px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.02);
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.studio-card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 14px;
  padding-bottom: 12px;
  border-bottom: 1px solid #f1f5f9;
}

.head-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.head-icon-box {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.head-icon-box svg {
  width: 18px;
  height: 18px;
}

.studio-card-title {
  font-size: 15px;
  font-weight: 700;
  color: #0f172a;
  margin: 0 0 2px 0;
}

.studio-card-sub {
  font-size: 11.5px;
  color: #64748b;
  line-height: 1.4;
}

.studio-card-body {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

/* Studio Path Picker */
.studio-path-box {
  width: 100%;
}

.path-input-group {
  display: flex;
  align-items: center;
  gap: 8px;
  position: relative;
}

.path-icon-affix {
  position: absolute;
  left: 12px;
  color: #94a3b8;
  display: flex;
  align-items: center;
  pointer-events: none;
}

.folder-svg {
  width: 16px;
  height: 16px;
}

.path-text-input {
  flex: 1;
  padding: 9px 12px 9px 38px;
  font-family: var(--font-mono);
  font-size: 13px;
  border: 1.5px solid #cbd5e1;
  border-radius: 8px;
  background: #ffffff;
  color: #0f172a;
  outline: none;
  transition: all 0.15s;
}

.path-text-input:focus {
  border-color: #2563eb;
  box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.1);
}

.path-btn-clear {
  position: absolute;
  right: 140px;
  background: none;
  border: none;
  color: #94a3b8;
  font-size: 13px;
  cursor: pointer;
  padding: 4px;
}

.path-btn-clear:hover {
  color: #475569;
}

.path-btn-browse {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 9px 18px;
  background: #f8fafc;
  border: 1.5px solid #cbd5e1;
  border-radius: 8px;
  font-size: 12.5px;
  font-weight: 600;
  color: #334155;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s;
}

.path-btn-browse:hover {
  background: #edf2f7;
  color: #0f172a;
  border-color: #94a3b8;
}

.browse-svg {
  width: 14px;
  height: 14px;
}

/* Spec Inspection Panel (Tiles Grid) */
.spec-inspection-panel {
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5fd 100%);
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.inspection-top-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.inspection-left {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
  overflow: hidden;
}

.inspection-live-dot {
  color: #10b981;
  font-size: 10px;
}

.inspection-badge-title {
  font-size: 12px;
  font-weight: 700;
  color: #059669;
  white-space: nowrap;
}

.inspection-filename {
  font-size: 12px;
  font-weight: 600;
  color: #334155;
  font-family: var(--font-mono);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.inspection-format-pill {
  font-size: 11px;
  font-weight: 700;
  color: #2563eb;
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  padding: 2px 8px;
  border-radius: 4px;
  white-space: nowrap;
}

.inspection-tiles-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 10px;
  padding: 8px 0;
  border-top: 1px solid #eef2f6;
  border-bottom: 1px solid #eef2f6;
}

@media (max-width: 800px) {
  .inspection-tiles-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

.spec-tile {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 8px;
  padding: 8px 12px;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.tile-label {
  font-size: 11px;
  color: #64748b;
}

.tile-value {
  font-size: 13.5px;
  font-weight: 700;
  font-family: var(--font-mono);
}

.tile-value-id {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  display: block;
  font-family: var(--font-mono);
  font-size: 13px;
  font-weight: 700;
  color: #334155;
  cursor: default;
}

.tile-inline-input {
  border: 1px solid #cbd5e1;
  border-radius: 4px;
  padding: 2px 6px;
  font-size: 12px;
  font-family: var(--font-mono);
  background: #f8fafc;
  color: #0f172a;
  outline: none;
  width: 100%;
}

.tile-inline-input:focus {
  border-color: #2563eb;
  background: #ffffff;
}

.inspection-meta-strip {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  padding: 8px 0 2px;
}

.meta-strip-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  padding: 4px 11px;
  font-size: 11.5px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.02);
}

.meta-strip-badge .badge-k {
  color: #64748b;
  font-weight: 500;
}

.meta-strip-badge .badge-v {
  color: #0f172a;
  font-weight: 700;
  font-family: var(--font-mono);
}

.meta-strip-badge.badge-match-success {
  background: #ecfdf5;
  border-color: #a7f3d0;
  color: #047857;
  font-weight: 600;
}

.badge-dot-green {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #10b981;
}

/* 2x2 Parameters Grid */
.studio-params-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

@media (max-width: 860px) {
  .studio-params-grid {
    grid-template-columns: 1fr;
  }
}

.param-card-cell {
  background: #ffffff;
  border: 1.5px solid #e8edf5;
  border-radius: 14px;
  padding: 16px 18px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  box-shadow: 0 2px 8px rgba(15, 23, 42, 0.02);
}

.param-card-cell:hover {
  border-color: #cbd5e1;
  box-shadow: 0 6px 18px rgba(15, 23, 42, 0.04);
}

.param-cell-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.param-cell-title {
  font-size: 13px;
  font-weight: 700;
  color: #1e293b;
}

.param-cell-tag {
  font-size: 11px;
  font-weight: 600;
  padding: 2px 9px;
  border-radius: 9999px;
  font-family: var(--font-mono);
  border: 1px solid transparent;
}
.param-cell-tag.text-blue {
  background: #eff6ff;
  border-color: #bfdbfe;
  color: #1d4ed8;
}
.param-cell-tag.text-purple {
  background: #f5f3ff;
  border-color: #ddd6fe;
  color: #7c3aed;
}
.param-cell-tag.text-emerald {
  background: #ecfdf5;
  border-color: #a7f3d0;
  color: #047857;
}
.param-cell-tag.text-indigo {
  background: #eef2ff;
  border-color: #c7d2fe;
  color: #4338ca;
}

/* Custom Dual KVD Selector */
.kvd-dual-selector {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.kvd-dual-card {
  border: 1.5px solid #e2e8f0;
  border-radius: 12px;
  padding: 12px 14px;
  background: #ffffff;
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  display: flex;
  flex-direction: column;
  gap: 6px;
  position: relative;
}

.kvd-dual-card:hover {
  border-color: #93c5fd;
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(37, 99, 235, 0.06);
}

.kvd-dual-card.active {
  border-color: #2563eb;
  background: linear-gradient(135deg, #eff6ff 0%, #ffffff 100%);
  box-shadow: 0 0 0 1.5px #2563eb, 0 4px 16px rgba(37, 99, 235, 0.12);
}

.dual-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
}

.dual-title-group {
  display: flex;
  align-items: center;
  gap: 8px;
}

.dual-radio-indicator {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  border: 2px solid #cbd5e1;
  background: #ffffff;
  display: inline-block;
  transition: all 0.2s ease;
  flex-shrink: 0;
}

.kvd-dual-card.active .dual-radio-indicator {
  border-color: #2563eb;
  background: #2563eb;
  box-shadow: inset 0 0 0 3px #ffffff;
}

.dual-title {
  font-size: 13.5px;
  font-weight: 750;
  color: #0f172a;
  font-family: var(--font-mono);
}

.dual-pill-badge {
  font-size: 10.5px;
  font-weight: 700;
  padding: 2px 7px;
  border-radius: 6px;
  white-space: nowrap;
}

.badge-blue {
  background: #eff6ff;
  color: #1d4ed8;
  border: 1px solid #bfdbfe;
}

.badge-emerald {
  background: #ecfdf5;
  color: #047857;
  border: 1px solid #a7f3d0;
}

.dual-desc {
  font-size: 11.5px;
  color: #64748b;
  line-height: 1.4;
  margin: 0;
}

/* Studio Input Wrappers & Pills */
.studio-input-wrap {
  position: relative;
  display: flex;
  align-items: center;
}

.studio-text-input {
  width: 100%;
  padding: 9px 14px;
  padding-right: 140px;
  font-family: var(--font-mono);
  font-size: 13.5px;
  font-weight: 700;
  border: 1.5px solid #e2e8f0;
  border-radius: 9px;
  background: #ffffff;
  color: #0f172a;
  outline: none;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
}

.studio-text-input:focus {
  border-color: #2563eb;
  box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.12);
}

.studio-input-unit {
  position: absolute;
  right: 10px;
  font-size: 11.5px;
  font-weight: 600;
  color: #64748b;
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  padding: 3px 8px;
  border-radius: 6px;
  pointer-events: none;
  font-family: var(--font-mono);
}

.studio-pills-row {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 2px;
}

.studio-pill-btn {
  border: 1px solid #e2e8f0;
  background: #ffffff;
  color: #475569;
  padding: 4px 10px;
  border-radius: 7px;
  font-size: 11px;
  font-weight: 500;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s cubic-bezier(0.16, 1, 0.3, 1);
}

.studio-pill-btn:hover {
  background: #f8fafc;
  color: #0f172a;
  border-color: #94a3b8;
  transform: translateY(-1px);
}

.studio-pill-btn.active {
  background: linear-gradient(135deg, #2563eb 0%, #1d4ed8 100%);
  border-color: #1d4ed8;
  color: #ffffff;
  font-weight: 700;
  box-shadow: 0 2px 8px rgba(37, 99, 235, 0.28);
  transform: translateY(-1px);
}

.param-cell-note {
  font-size: 11.5px;
  color: #64748b;
  line-height: 1.5;
  margin: 4px 0 0;
}

.protect-callout-strip {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  padding: 10px 12px;
  background: linear-gradient(135deg, #ecfdf5 0%, #f0fdf4 100%);
  border: 1px solid #a7f3d0;
  border-radius: 8px;
  margin-top: 4px;
}

.protect-icon {
  font-size: 14px;
  flex-shrink: 0;
  line-height: 1.4;
}

.protect-text {
  font-size: 11.5px;
  color: #065f46;
  line-height: 1.45;
  font-weight: 500;
}

/* Accordion Collapsible Ports */
.card-accordion {
  padding: 16px 22px;
  border-radius: 16px;
  border: 1px solid #e2e8f0;
  background: #ffffff;
  box-shadow: 0 2px 10px rgba(15, 23, 42, 0.02);
}

.accordion-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  user-select: none;
}

.accordion-head.clickable {
  cursor: pointer;
}

.accordion-arrow {
  font-size: 10px;
  color: #64748b;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 22px;
  height: 22px;
  border-radius: 6px;
  background: #f1f5f9;
  transition: all 0.2s ease;
}

.accordion-arrow.rotated {
  transform: rotate(90deg);
  background: #eff6ff;
  color: #2563eb;
}

.accordion-title {
  font-size: 13.5px;
  font-weight: 700;
  color: #1e293b;
}

.accordion-sub {
  font-size: 12px;
  color: #64748b;
  margin-left: 8px;
}

.accordion-badge {
  font-size: 11.5px;
  font-weight: 600;
  color: #2563eb;
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  padding: 3px 10px;
  border-radius: 6px;
  transition: all 0.15s ease;
}

.accordion-head:hover .accordion-badge {
  background: #dbeafe;
}

.accordion-body {
  margin-top: 14px;
  padding-top: 14px;
  border-top: 1px solid #f1f5f9;
}

.ports-six-grid {
  display: grid;
  grid-template-columns: repeat(6, 1fr);
  gap: 12px;
}

@media (max-width: 1024px) {
  .ports-six-grid {
    grid-template-columns: repeat(3, 1fr);
  }
}
@media (max-width: 640px) {
  .ports-six-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

.port-tile {
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 10px 12px;
  transition: all 0.2s ease;
}

.port-tile:hover {
  border-color: #93c5fd;
  background: #ffffff;
  box-shadow: 0 4px 12px rgba(37, 99, 235, 0.05);
}

.port-label {
  font-size: 11.5px;
  font-weight: 600;
  color: #475569;
  margin-bottom: 6px;
  display: block;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.port-input {
  width: 100%;
  padding: 6px 10px;
  font-size: 13.5px;
  font-weight: 700;
  font-family: var(--font-mono);
  border: 1.5px solid #e2e8f0;
  border-radius: 6px;
  background: #ffffff;
  color: #0f172a;
  outline: none;
  transition: all 0.15s ease;
}

.port-input:focus {
  border-color: #2563eb;
  box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.1);
}

/* Bottom Studio Action Bar */
.model-bottom-bar {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(16px);
  border: 1px solid #e2e8f0;
  border-radius: 16px;
  padding: 14px 22px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
  box-shadow: 0 6px 24px rgba(15, 23, 42, 0.06);
}

.bottom-left-hw {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.hw-chip-pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  font-weight: 600;
  color: #1e293b;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  padding: 5px 14px;
  border-radius: 9999px;
}

.hw-chip-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #10b981;
  box-shadow: 0 0 0 3px rgba(16, 185, 129, 0.2);
}

.hw-sub-info {
  font-size: 11.5px;
  color: #64748b;
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  padding: 3px 8px;
  border-radius: 6px;
}

.bottom-right-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-left: auto;
  flex-wrap: wrap;
}

.btn-svg-sm {
  width: 14px;
  height: 14px;
  flex-shrink: 0;
}

.btn-studio-subtle {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 8px 16px;
  font-size: 12.5px;
  font-weight: 600;
  background: #ffffff;
  border: 1.5px solid #cbd5e1;
  border-radius: 8px;
  color: #475569;
  cursor: pointer;
  transition: all 0.15s ease;
}

.btn-studio-subtle:hover {
  background: #f8fafc;
  color: #0f172a;
  border-color: #94a3b8;
}

.btn-studio-primary {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 9px 22px;
  font-size: 13px;
  font-weight: 700;
  background: linear-gradient(135deg, #2563eb 0%, #1d4ed8 100%);
  border: none;
  border-radius: 8px;
  color: #ffffff;
  cursor: pointer;
  box-shadow: 0 3px 12px rgba(37, 99, 235, 0.28);
  transition: all 0.15s ease;
}

.btn-studio-primary:hover {
  transform: translateY(-1px);
  box-shadow: 0 5px 16px rgba(37, 99, 235, 0.38);
}

.btn-studio-accent {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 9px 20px;
  font-size: 13px;
  font-weight: 700;
  background: linear-gradient(135deg, #10b981 0%, #059669 100%);
  border: none;
  border-radius: 8px;
  color: #ffffff;
  cursor: pointer;
  box-shadow: 0 3px 12px rgba(16, 185, 129, 0.28);
  transition: all 0.15s ease;
}

.btn-studio-accent:hover {
  transform: translateY(-1px);
  box-shadow: 0 5px 16px rgba(16, 185, 129, 0.38);
}

/* ==============================================================
   TAB 5: 运行日志 (Logs)
   ============================================================== */
.page-logs {
  height: calc(100vh - 92px);
  display: flex;
  flex-direction: column;
}

.logs-card-box {
  height: 100%;
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 12px;
  box-shadow: 0 2px 8px rgba(0,0,0,0.02);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.logs-toolbar {
  padding: 10px 16px;
  border-bottom: 1px solid #f1f5f9;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: #f8fafc;
}

.logs-filters-group {
  display: flex;
  gap: 6px;
}

.logs-tab-btn {
  padding: 4px 10px;
  font-size: 12px;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  background: #ffffff;
  color: #64748b;
  cursor: pointer;
}

.logs-tab-btn.active {
  background: #edf2fe;
  color: #2563eb;
  border-color: #bfdbfe;
  font-weight: 600;
}

.logs-actions-group {
  display: flex;
  align-items: center;
  gap: 12px;
}

.auto-scroll-checkbox {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: #475569;
  cursor: pointer;
}

.logs-terminal-viewport {
  flex: 1;
  padding: 14px 16px;
  overflow-y: auto;
  font-family: monospace;
  font-size: 11.5px;
  line-height: 1.6;
  background: #ffffff;
}

.terminal-log-row {
  margin-bottom: 4px;
  word-break: break-all;
}

.terminal-log-row.row-error {
  color: #b91c1c;
  background: #fef2f2;
  border-radius: 4px;
  padding: 2px 4px;
}

.log-ts {
  color: #94a3b8;
  margin-right: 8px;
}

.log-src-tag {
  padding: 1px 5px;
  border-radius: 3px;
  font-size: 10.5px;
  margin-right: 8px;
  font-weight: 600;
}

.tag-engine { background: #eff6ff; color: #2563eb; }
.tag-guard { background: #ecfdf5; color: #059669; }
.tag-dsh { background: #f5f3ff; color: #7c3aed; }
.tag-system { background: #f1f5f9; color: #475569; }

.logs-empty-tip {
  text-align: center;
  color: #94a3b8;
  padding: 40px;
  font-family: sans-serif;
}

/* Animations & Utilities */
.bg-blue-subtle { background: #eff6ff; }
.bg-purple-subtle { background: #f5f3ff; }
.text-blue { color: #2563eb; }
.text-purple { color: #7c3aed; }
.text-green { color: #16a34a; }
.text-gray { color: #64748b; }
.highlight { color: #2563eb; font-weight: 700; }

@keyframes spin {
  to { transform: rotate(360deg); }
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

/* ==============================================================
   PRESTIGE GLASS MODAL (Antigravity Global Modal System)
   ============================================================== */
.custom-modal-overlay {
  position: fixed;
  inset: 0;
  z-index: 99999;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(15, 23, 42, 0.45);
  backdrop-filter: blur(8px);
  -webkit-backdrop-filter: blur(8px);
  padding: 24px;
}

.custom-modal-box {
  position: relative;
  width: 100%;
  max-width: 480px;
  background: #ffffff;
  border-radius: 20px;
  border: 1px solid rgba(226, 232, 240, 0.9);
  box-shadow: 0 25px 60px -15px rgba(15, 23, 42, 0.28), 0 0 0 1px rgba(255, 255, 255, 0.9) inset;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.modal-glow-strip {
  height: 4px;
  width: 100%;
}
.glow-danger { background: linear-gradient(90deg, #ef4444, #f43f5e); }
.glow-warning { background: linear-gradient(90deg, #f59e0b, #d97706); }
.glow-success { background: linear-gradient(90deg, #10b981, #059669); }
.glow-info { background: linear-gradient(90deg, #3b82f6, #6366f1); }

.modal-header {
  display: flex;
  align-items: center;
  padding: 20px 24px 16px;
  gap: 14px;
}

.modal-icon-badge {
  width: 44px;
  height: 44px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.modal-svg {
  width: 22px;
  height: 22px;
}

.badge-danger {
  background: #fef2f2;
  color: #dc2626;
  border: 1px solid #fee2e2;
  box-shadow: 0 2px 8px rgba(239, 68, 68, 0.15);
}

.badge-warning {
  background: #fffbeb;
  color: #d97706;
  border: 1px solid #fef3c7;
  box-shadow: 0 2px 8px rgba(245, 158, 11, 0.15);
}

.badge-success {
  background: #ecfdf5;
  color: #059669;
  border: 1px solid #d1fae5;
  box-shadow: 0 2px 8px rgba(16, 185, 129, 0.15);
}

.badge-info {
  background: #eff6ff;
  color: #2563eb;
  border: 1px solid #dbeafe;
  box-shadow: 0 2px 8px rgba(37, 99, 235, 0.15);
}

.modal-title-wrap {
  flex: 1;
  min-width: 0;
}

.modal-title {
  font-size: 16.5px;
  font-weight: 700;
  color: #0f172a;
  margin: 0;
  line-height: 1.35;
}

.modal-sub {
  font-size: 11px;
  color: #94a3b8;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.modal-close-icon-btn {
  background: transparent;
  border: none;
  cursor: pointer;
  color: #94a3b8;
  width: 32px;
  height: 32px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.18s ease;
}

.modal-close-icon-btn:hover {
  background: #f1f5f9;
  color: #334155;
}

.close-svg {
  width: 17px;
  height: 17px;
}

.modal-body {
  padding: 0 24px 22px;
}

.modal-message {
  font-size: 14px;
  line-height: 1.65;
  color: #334155;
  white-space: pre-line;
  word-break: break-word;
}

.modal-detail-card {
  margin-top: 12px;
  padding: 10px 12px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  font-family: monospace;
  font-size: 12px;
  color: #475569;
  word-break: break-all;
}

.modal-footer {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 12px;
  padding: 16px 24px;
  background: #f8fafc;
  border-top: 1px solid #f1f5f9;
}

.modal-action-btn {
  padding: 9px 20px;
  border-radius: 10px;
  font-size: 13.5px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  border: none;
  outline: none;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.modal-btn-cancel {
  background: #ffffff;
  color: #475569;
  border: 1px solid #cbd5e1;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
}

.modal-btn-cancel:hover {
  background: #f1f5f9;
  color: #1e293b;
  border-color: #94a3b8;
}

.modal-btn-confirm.btn-danger {
  background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%);
  color: #ffffff;
  box-shadow: 0 4px 14px rgba(239, 68, 68, 0.35);
}

.modal-btn-confirm.btn-danger:hover {
  background: linear-gradient(135deg, #dc2626 0%, #b91c1c 100%);
  box-shadow: 0 6px 18px rgba(239, 68, 68, 0.45);
  transform: translateY(-1px);
}

.modal-btn-confirm.btn-warning {
  background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%);
  color: #ffffff;
  box-shadow: 0 4px 14px rgba(245, 158, 11, 0.35);
}

.modal-btn-confirm.btn-warning:hover {
  background: linear-gradient(135deg, #d97706 0%, #b45309 100%);
  box-shadow: 0 6px 18px rgba(245, 158, 11, 0.45);
  transform: translateY(-1px);
}

.modal-btn-confirm.btn-success {
  background: linear-gradient(135deg, #10b981 0%, #059669 100%);
  color: #ffffff;
  box-shadow: 0 4px 14px rgba(16, 185, 129, 0.35);
}

.modal-btn-confirm.btn-success:hover {
  background: linear-gradient(135deg, #059669 0%, #047857 100%);
  box-shadow: 0 6px 18px rgba(16, 185, 129, 0.45);
  transform: translateY(-1px);
}

.modal-btn-confirm.btn-info {
  background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%);
  color: #ffffff;
  box-shadow: 0 4px 14px rgba(37, 99, 235, 0.35);
}

.modal-btn-confirm.btn-info:hover {
  background: linear-gradient(135deg, #2563eb 0%, #1d4ed8 100%);
  box-shadow: 0 6px 18px rgba(37, 99, 235, 0.45);
  transform: translateY(-1px);
}

.modal-action-btn:active {
  transform: translateY(0);
}

/* Modal Transition Animation */
.modal-bounce-enter-active {
  transition: all 0.24s cubic-bezier(0.16, 1, 0.3, 1);
}

/* ==============================================================
   GPU Architecture Selector & Mismatch Banner Styles
   ============================================================== */
.arch-mismatch-banner {
  background: linear-gradient(135deg, rgba(254, 226, 226, 0.65) 0%, rgba(254, 242, 242, 0.92) 60%, rgba(255, 251, 235, 0.9) 100%);
  border-color: rgba(244, 63, 94, 0.32);
}

.glow-rose {
  background: linear-gradient(180deg, #f43f5e 0%, #e11d48 100%) !important;
}

.icon-rose {
  background: rgba(244, 63, 94, 0.12) !important;
  color: #e11d48 !important;
  border-color: rgba(244, 63, 94, 0.24) !important;
}

.tag-rose {
  background: rgba(244, 63, 94, 0.12) !important;
  color: #be123c !important;
  border-color: rgba(244, 63, 94, 0.25) !important;
}

.btn-cta-rose {
  background: linear-gradient(135deg, #e11d48 0%, #be123c 100%) !important;
  box-shadow: 0 4px 12px rgba(225, 29, 72, 0.28) !important;
}

.btn-cta-rose:hover {
  background: linear-gradient(135deg, #be123c 0%, #9f1239 100%) !important;
  box-shadow: 0 6px 16px rgba(225, 29, 72, 0.38) !important;
}

.card-arch-mismatch {
  border-color: rgba(245, 158, 11, 0.45) !important;
  box-shadow: 0 8px 24px -6px rgba(245, 158, 11, 0.12) !important;
}

.bg-emerald-subtle {
  background: rgba(16, 185, 129, 0.1);
}

.bg-amber-subtle {
  background: rgba(245, 158, 11, 0.12);
}

.head-right-status {
  display: flex;
  align-items: center;
}

.arch-status-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 5px 12px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}

.pill-arch-ok {
  background: rgba(16, 185, 129, 0.1);
  color: #047857;
  border: 1px solid rgba(16, 185, 129, 0.25);
}

.pill-arch-warn {
  background: rgba(245, 158, 11, 0.14);
  color: #b45309;
  border: 1px solid rgba(245, 158, 11, 0.32);
}

.arch-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.dot-ok {
  background: #10b981;
  box-shadow: 0 0 6px rgba(16, 185, 129, 0.6);
}

.dot-warn {
  background: #f59e0b;
  box-shadow: 0 0 6px rgba(245, 158, 11, 0.6);
}

.gpu-auto-inspect-panel {
  padding: 16px 18px;
  border-radius: 12px;
  background: linear-gradient(180deg, #f8fafc 0%, #f1f5f9 100%);
  border: 1.5px solid #e2e8f0;
  margin-bottom: 12px;
}

.gpu-inspect-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 14px;
}

.gpu-inspect-item {
  background: #ffffff;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 12px 14px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  box-shadow: 0 1px 2px rgba(15, 23, 42, 0.03);
}

.inspect-label {
  font-size: 11.5px;
  font-weight: 600;
  color: #64748b;
}

.inspect-val {
  font-size: 13.5px;
  font-weight: 700;
  color: #0f172a;
  line-height: 1.35;
  word-break: break-word;
}

.inspect-foot-tag {
  align-self: flex-start;
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 999px;
  margin-top: auto;
}

.tag-ok {
  background: rgba(16, 185, 129, 0.12);
  color: #047857;
  border: 1px solid rgba(16, 185, 129, 0.25);
}

.tag-warn {
  background: rgba(245, 158, 11, 0.14);
  color: #b45309;
  border: 1px solid rgba(245, 158, 11, 0.32);
}

.tag-info {
  background: rgba(59, 130, 246, 0.1);
  color: #1d4ed8;
  border: 1px solid rgba(59, 130, 246, 0.22);
}

.driver-config-panel {
  padding: 14px 18px;
  border-radius: 12px;
  background: #f8fafc;
  border: 1.5px solid #e2e8f0;
  transition: all 0.2s ease;
}

.driver-panel-highlight {
  background: linear-gradient(135deg, rgba(254, 243, 199, 0.5) 0%, rgba(255, 251, 235, 0.85) 100%);
  border-color: rgba(245, 158, 11, 0.55);
  box-shadow: 0 4px 14px rgba(245, 158, 11, 0.1);
}

.engine-binary-picker-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 14px;
  flex-wrap: wrap;
}

.engine-label-col {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.engine-lbl {
  font-size: 13px;
  font-weight: 700;
  color: #1e293b;
}

.engine-arch-hint {
  font-size: 12px;
  color: #d97706;
  font-weight: 600;
}

.engine-arch-ok-hint {
  font-size: 12px;
  color: #64748b;
}

.engine-input-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  min-width: 280px;
  max-width: 540px;
}

.engine-path-input {
  flex: 1;
  height: 34px;
  padding: 0 12px;
  border-radius: 8px;
  border: 1px solid #cbd5e1;
  background: #ffffff;
  font-size: 12px;
  color: #1e293b;
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}

.btn-engine-reset {
  height: 34px;
  padding: 0 12px;
  border-radius: 8px;
  border: 1px solid #cbd5e1;
  background: #ffffff;
  color: #475569;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
}

.btn-engine-reset:hover {
  background: #f1f5f9;
  color: #0f172a;
}

.btn-engine-browse {
  height: 34px;
  padding: 0 14px;
  border-radius: 8px;
  border: 1px solid rgba(59, 130, 246, 0.32);
  background: rgba(59, 130, 246, 0.1);
  color: #2563eb;
  font-size: 12.5px;
  font-weight: 600;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s ease;
}

.btn-engine-browse:hover {
  background: #2563eb;
  color: #ffffff;
}

.btn-engine-browse-warn {
  background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%);
  color: #ffffff;
  border-color: transparent;
  box-shadow: 0 3px 10px rgba(245, 158, 11, 0.3);
}

.btn-engine-browse-warn:hover {
  background: linear-gradient(135deg, #d97706 0%, #b45309 100%);
  color: #ffffff;
}

.modal-bounce-leave-active {
  transition: all 0.18s cubic-bezier(0.4, 0, 1, 1);
}

.modal-bounce-enter-from {
  opacity: 0;
}

.modal-bounce-enter-from .custom-modal-box {
  opacity: 0;
  transform: scale(0.94) translateY(8px);
}

.modal-bounce-leave-to {
  opacity: 0;
}

.modal-bounce-leave-to .custom-modal-box {
  opacity: 0;
  transform: scale(0.97) translateY(4px);
}

/* =========================================
   SETTINGS PAGE & NAVIGATION (Pill Switcher)
   ========================================= */
.page-settings {
  padding: 24px 32px 48px 32px;
  overflow-y: auto;
  height: 100%;
}

.settings-top-nav-wrap {
  display: flex;
  justify-content: flex-start;
  margin-bottom: 24px;
}

/* Floating Pill Tab Bar matching Image 3 */
.settings-pill-nav {
  display: inline-flex;
  align-items: center;
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  border-radius: 999px;
  padding: 4px;
  gap: 4px;
  box-shadow: 0 1px 3px rgba(15, 23, 42, 0.04);
}

.settings-pill-tab {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 7px 18px;
  border-radius: 999px;
  border: 1px solid transparent;
  background: transparent;
  color: #475569;
  font-size: 13.5px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.18s cubic-bezier(0.4, 0, 0.2, 1);
}

.settings-pill-tab:hover {
  color: #1e293b;
  background: rgba(255, 255, 255, 0.6);
}

.settings-pill-tab.active {
  background: #ffffff;
  color: var(--accent-blue);
  font-weight: 600;
  border-color: rgba(0, 0, 0, 0.04);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.08);
}

.settings-tab-svg {
  width: 15px;
  height: 15px;
}

.settings-content-wrap {
  max-width: 960px;
}

.settings-group-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
}

.group-header-icon {
  width: 18px;
  height: 18px;
  color: var(--accent-blue);
}

.settings-group-header h2 {
  font-size: 15px;
  font-weight: 600;
  color: #1e293b;
  margin: 0;
}

.settings-box-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 12px;
  box-shadow: 0 1px 3px rgba(15, 23, 42, 0.03);
  overflow: hidden;
}

.pref-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 20px;
  border-bottom: 1px solid #f1f5f9;
  transition: background 0.15s;
}

.pref-row:last-child {
  border-bottom: none;
}

.pref-row:hover {
  background: #fbfcfe;
}

.pref-lead {
  display: flex;
  align-items: center;
  gap: 14px;
}

.pref-icon-box {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: #f1f5f9;
  color: #475569;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.pref-icon-svg {
  width: 17px;
  height: 17px;
}

.pref-text {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.pref-title {
  font-size: 14px;
  font-weight: 600;
  color: #0f172a;
}

.pref-desc {
  font-size: 12px;
  color: #64748b;
}

/* Segmented Control matching Image 2 */
.pill-segmented {
  display: inline-flex;
  align-items: center;
  background: #f1f5f9;
  border-radius: 999px;
  padding: 3px;
  gap: 3px;
}

.pill-seg-item {
  padding: 5px 14px;
  border-radius: 999px;
  border: none;
  background: transparent;
  color: #64748b;
  font-size: 12.5px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
  white-space: nowrap;
}

.pill-seg-item:hover {
  color: #1e293b;
}

.pill-seg-item.active {
  background: #ffffff;
  color: var(--accent-blue);
  font-weight: 600;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.08);
}

/* Theme color cluster matching Image 2 */
.theme-colors-cluster {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.color-choice-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 14px;
  border-radius: 999px;
  border: 1px solid #e2e8f0;
  background: #ffffff;
  color: #334155;
  font-size: 12.5px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
}

.color-choice-pill:hover {
  border-color: #cbd5e1;
  background: #f8fafc;
}

.color-choice-pill.choice-active {
  background: var(--accent-blue);
  border-color: var(--accent-blue);
  color: #ffffff;
  font-weight: 600;
  box-shadow: 0 2px 6px rgba(37, 99, 235, 0.25);
}

.color-dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  flex-shrink: 0;
}

.dot-default {
  background: transparent;
  border: 1.8px solid currentColor;
}

.dot-crimson {
  background: #e11d48;
}

.dot-emerald {
  background: #10b981;
}

.dot-ocean {
  background: #0284c7;
}

.btn-pref-ghost {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 14px;
  border-radius: 8px;
  border: 1px solid #e2e8f0;
  background: #ffffff;
  color: #334155;
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-pref-ghost:hover {
  background: #f8fafc;
  color: #0f172a;
  border-color: #cbd5e1;
}

/* About Pmod Tab Styling */
.about-hero-card {
  background: linear-gradient(135deg, #ffffff 0%, #f8fafc 100%);
  border: 1px solid #e2e8f0;
  border-radius: 16px;
  padding: 28px;
  box-shadow: 0 2px 8px rgba(15, 23, 42, 0.04);
  margin-bottom: 20px;
}

.about-hero-glow {
  display: flex;
  align-items: center;
  gap: 24px;
}

.about-logo-wrapper {
  width: 68px;
  height: 68px;
  border-radius: 18px;
  background: linear-gradient(145deg, #1e293b 0%, #09090b 100%);
  border: 1px solid rgba(255, 255, 255, 0.15);
  box-shadow: 0 6px 18px rgba(15, 23, 42, 0.2);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.about-logo-svg {
  width: 42px;
  height: 42px;
}

.about-hero-details {
  flex: 1;
}

.about-hero-title-row {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 6px;
}

.about-hero-title {
  font-size: 20px;
  font-weight: 700;
  color: #0f172a;
  letter-spacing: -0.02em;
  margin: 0;
}

.about-version-badge {
  font-size: 11px;
  font-weight: 600;
  color: var(--accent-blue);
  background: var(--accent-blue-light);
  border: 1px solid var(--accent-blue-border);
  padding: 2px 8px;
  border-radius: 999px;
}

.about-hero-subtitle {
  font-size: 13px;
  color: #64748b;
  line-height: 1.5;
  margin: 0 0 12px 0;
}

.about-hero-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.tech-tag {
  font-size: 11px;
  font-weight: 500;
  color: #475569;
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  padding: 2px 8px;
}

.about-highlights-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 14px;
}

.highlight-feature-card {
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 12px;
  padding: 16px 18px;
  box-shadow: 0 1px 3px rgba(15, 23, 42, 0.03);
  transition: transform 0.15s, box-shadow 0.15s;
}

.highlight-feature-card:hover {
  transform: translateY(-1px);
  box-shadow: 0 4px 10px rgba(15, 23, 42, 0.06);
}

.feature-icon-box {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: #f1f5f9;
  color: #475569;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 12px;
  transition: all 0.15s;
}

.feature-svg {
  width: 17px;
  height: 17px;
}

.feature-info-title {
  font-size: 14px;
  font-weight: 600;
  color: #0f172a;
  margin-bottom: 4px;
}

.feature-info-desc {
  font-size: 12px;
  color: #64748b;
  line-height: 1.5;
}

.about-community-card {
  margin-top: 18px;
  background: #ffffff;
  border: 1px solid #eef2f7;
  border-radius: 12px;
  padding: 14px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  box-shadow: 0 1px 3px rgba(15, 23, 42, 0.03);
  gap: 16px;
  flex-wrap: wrap;
}

.community-info-cluster {
  display: flex;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
}

.community-meta-item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
}

.meta-label {
  color: #64748b;
  font-weight: 500;
}

.meta-val {
  color: #0f172a;
  font-weight: 600;
}

.meta-val.meta-highlight {
  color: var(--accent-primary-text, #2563eb);
  font-family: monospace;
  font-weight: 700;
}

.meta-sep {
  width: 1px;
  height: 14px;
  background: #e2e8f0;
}

.btn-join-qq {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 7px 16px;
  border-radius: 8px;
  background: var(--accent-primary-subtle, #eff6ff);
  color: var(--accent-primary-text, #2563eb);
  border: 1px solid var(--accent-primary-border, #bfdbfe);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s ease;
}

.btn-join-qq:hover {
  background: var(--accent-primary, #2563eb);
  color: #ffffff;
  border-color: var(--accent-primary, #2563eb);
  box-shadow: 0 2px 6px var(--accent-primary-shadow, rgba(37, 99, 235, 0.25));
}

.community-action-group {
  display: flex;
  align-items: center;
  gap: 10px;
}

.btn-check-update {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 7px 16px;
  border-radius: 8px;
  background: #f8fafc;
  color: #334155;
  border: 1px solid #cbd5e1;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  line-height: 1;
  transition: all 0.15s ease;
  box-sizing: border-box;
}

.btn-check-update:hover:not(:disabled) {
  background: #f1f5f9;
  color: #0f172a;
  border-color: #94a3b8;
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.05);
}

.btn-check-update:disabled {
  opacity: 0.65;
  cursor: not-allowed;
}

.btn-check-svg {
  width: 14px !important;
  height: 14px !important;
  max-width: 14px !important;
  max-height: 14px !important;
  flex-shrink: 0;
  display: inline-block;
  vertical-align: middle;
}

.btn-spinner-dot {
  width: 12px;
  height: 12px;
  border: 2px solid #94a3b8;
  border-top-color: transparent;
  border-radius: 50%;
  animation: btn-spin 0.8s linear infinite;
  display: inline-block;
}

.btn-join-svg {
  width: 15px;
  height: 15px;
}

.arrow-svg {
  width: 13px;
  height: 13px;
}

.about-page-footer {
  margin-top: 24px;
  text-align: center;
  padding-bottom: 24px;
}

.footer-license-note {
  font-size: 11.5px;
  color: #94a3b8;
}

/* Sidebar settings button */
.menu-item-settings {
  margin-bottom: 6px;
  border-top: 1px solid #f1f5f9;
  padding-top: 10px;
  border-radius: 8px;
}
</style>
