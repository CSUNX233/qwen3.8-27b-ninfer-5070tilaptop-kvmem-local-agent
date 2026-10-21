# 交给Agent的整合包安装提示词

先把压缩包完整解压；把下面的安装目录改为实际位置，然后整段交给Agent。

```text
请帮我部署NInfer v0.1.4两模型离线整合包。
安装目录：D:\AI\ninfer-v014
优先模型：Huihui（需要原版则选择gsq）

先读目录内AGENTS.md、docs/OFFLINE_BUNDLE.md、docs/TROUBLESHOOTING.md和docs/RELEASE_NOTES.md。
检查Windows x64、GPU型号/显存、可用RAM、兼容CUDA13的驱动、Edge和1234/1236/3080端口；
识别已有服务，不杀其他Node、浏览器或模型实例。保留现有会话、模型、配置和驱动策略。

这是离线整合包：核对offline-bundle.json及文件哈希，不要从GitHub重新下载引擎、
不要升级npm依赖，不要要求重新下载或转换两份现成ninfer模型。
引擎、两个模型和Node/dsh/运行DLL均已包含，只下载夸克包即可，不依赖GitHub在线部署。
使用scripts/setup-bundle.ps1 -Model huihui（或gsq）；它使用本包便携Node与已有依赖。
若缺VC++ x64运行库，使用包内Microsoft签名安装器，说明需管理员安装和可能重启。
驱动/Edge缺失则给出对应官网下载入口，不自动更改驱动或全局GPU/代理设置。

保留NVFP4、xhigh、MTP3、262144逻辑容量、DeviceKV36864、Retrieve3840、
HostKV7168MiB、sink12288、recent19456及AnswerPenaltyScope=answer。
建议的显存处理顺序：先优先开启Hybrid/Optimus混合显卡模式，桌面/浏览器使用核显；
再针对本包runtime/engine/ninfer-serve.exe关闭CUDA系统内存回退，
即NVIDIA程序设置CUDA - Sysmem Fallback Policy=Prefer No Sysmem Fallback，
保留KVMem HostKV7168MiB，不把两者混淆；设置由使用者按说明完成，不自动改全局驱动。
之后关闭无关后台应用；仍显存不足优先改KVDType=rk4v4并正常重启；
再不足才适当缩小页池。包内08-pool-small将DeviceKV/Retrieve配套设为34560/1536，
sink12288/recent19456保留，名义临时页余量仍1280token；检索变小可能降低历史召回。
不能只缩页池而保留装不下的窗口，也不能把驱动整卡空闲当作CUDA可分配余量。

用户明确要求启动时再运行02-start.cmd；这会独占较多显存。
按引擎1234、守卫1236、认证dsh网页3080逐层检查。
不要把HTTP200当成实际工具任务成功；若要实测，先说明检查内容和资源影响。
不要自动改模型、降低思考强度、删历史或缩上下文来隐藏问题。
报错按纠错文档定位，记录准确版本/哈希、报错层级与未验证项，日志先脱敏。
女娲或人物技能仅在我明确要求时启用。
```
