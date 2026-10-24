# 交给Agent的 v0.1.5 整合包安装提示词

先完整解压，修改安装目录和包类型，再复制下面内容。

```text
请部署NInfer v0.1.5离线整合包。
安装目录：D:\AI\ninfer-v015
包类型：完整版 / 无模型版（填写其一）
优先模型：完整版Huihui或gsq；无模型版填写自己的文字+MTP v3 .ninfer路径。

先读本目录AGENTS.md、docs/OFFLINE_BUNDLE.md、docs/TROUBLESHOOTING.md及更新说明。
核对Windows x64、显卡型号/显存、RAM、驱动、Edge与端口1234/1236/3080。
社区反馈30系以后12GB卡可用，16GB可选更大IQ3_S；本包exe仅sm120a/50系。
若是30/40系，先识别并改用已验证的对应架构构建，不能把本exe改名当作兼容版。
识别已有服务，只停止本包启动的进程；保留会话、模型和用户配置。

核对offline-bundle.json和SHA-256；两包均已含引擎、Node/dsh与用户态依赖，
不要从GitHub重下或擅自升级npm。完整版含双文字模型及完整Huihui视觉制品，
使用scripts/setup-bundle.ps1 -Model huihui（或gsq），不要求重新转换模型。
无模型版使用scripts/setup-bundle.ps1 -ModelPath <自己的.ninfer>，必要时指定ModelId。
缺VC++运行库可用包内Microsoft签名安装器；驱动/Edge仍是系统前置。

默认只启用文字+MTP，VisionCPU=false；不要因为包里有视觉文件就启用。
用户明确需要视觉时，停止后用09-enable-cpu-vision.cmd；必须完全CPU视觉，
参数--vision-cpu --vision-max-merged 256，dsh输入text,image；文字/MTP仍使用GPU。
切回文字使用05-use-huihui或05-use-gsq，确保同时关闭视觉。

保留NVFP4、xhigh、MTP3、262144逻辑容量、DeviceKV36864/Retrieve3840、
HostKV7168MiB、sink12288/recent19456、AnswerPenaltyScope=answer，及C2.1默认。
显存准备顺序：优先Hybrid/Optimus混合模式，再针对实际引擎exe关闭CUDA系统内存回退，
即CUDA - Sysmem Fallback Policy=Prefer No Sysmem Fallback，保留KVMem HostKV。
之后关闭后台，仍不足优先RK4V4，再不足配套缩页池/检索；包内小档34560/1536。
缩检索可能损失历史召回；不要只缩池、降思考、删历史或强制结束来隐藏问题。
不要自动修改驱动/全局GPU/代理，按用户授权启动02-start.cmd。

按引擎、守卫、认证dsh网页分层检查；HTTP200不等于真实Agent工具任务成功。
不要重复占用正在使用的GPU跑长测。记录版本、实际hash、通过/失败与未覆盖项，
日志先脱敏，不公开登录URL/token/用户会话。女娲及人物技能只在明确要求时启用。
```
