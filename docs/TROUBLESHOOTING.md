# 整合包排错与纠错提示词

先区分安装、引擎、缓存守卫、dsh、工具执行五层。保留最早失败日志和发生时间，不要连续重试覆盖证据；本包不含维护者的登录信息和会话。

| 现象 | 检查与处理 |
|---|---|
| 校验慢、安装窗口一段时间不动 | 两包默认只校验程序、依赖和部署文件，大量npm文件仍会耗时，可观察磁盘读取；默认不扫描约21GiB模型。程序/依赖缺失或哈希不符时重下受损包并完整解压，不用未知DLL补齐 |
| 删除不用的模型后还能校验吗 | 可以。04-verify不要求任何模型，缺少某个未选用的文字/视觉模型不报错。部署/启动只检查当前所选模型；若提示Model file is missing，核对配置路径或改选已有模型。需要检查仍保留的模型，可手动运行scripts/verify-bundle.ps1 -CheckModels；缺失模型跳过，存在但损坏的原包模型会报错 |
| 30/40系报 no kernel image / invalid device function | 本次预编译附件为sm120a、适配50系；30/40系须使用相应架构的已验证引擎或按BUILD.md编译。社区12GB可用反馈不代表此exe包含所有显卡架构 |
| 无模型包提示输入路径 | 使用01-setup-your-model.cmd，输入已有text+MTP v3 .ninfer文件完整路径；GGUF不能直接使用。路径含空格也可用，命令行参数须用双引号包住 |
| 模型不接收图片 / 视觉启用后仍是文字 | 发布默认GSQ纯文字，不启用视觉。完整包先03-stop，再09-enable-cpu-vision，随后02-start并新开会话；该入口同时切到Huihui text,vision,mtp制品和VisionCPU=true。视觉编码走CPU，文字推理仍用GPU；09-disable-vision-gsq切回GSQ文字。无模型版不含视觉权重 |
| 缺VCRUNTIME/MSVCP DLL，或引擎help失败 | 运行 `00-install-vc-runtime.cmd` 安装包内Microsoft签名VC++ x64运行库；按要求重启。检查 `runtime/engine` 内固定cuBLAS和FFmpeg，不混用GPL/其他ABI文件 |
| 找不到Node/npm | 一键入口优先使用 `runtime/node`；运行 `runtime/node/node.exe --version` 应为v24.14.1。勿复制自己的全局node_modules或把旧dsh home混入包 |
| dsh报“exists and is not a symlink or dsh-managed module proxy” | 通常是自制隔离目录/旧profiles依赖映射冲突，发生在模型请求之前也会出现；按本包全新runtime/dsh-home生成配置，保留旧home备份，不把它判作CUDA或模型错误，不递归删除指向真实依赖的junction |
| 1234/1236/3080被占用 | 查监听PID及实际路径。本包只用03-stop关闭自己记录且身份匹配的进程；旧安装需使用其自己的停止入口，不批杀node.exe |
| CUDA OOM、Graph捕获或页池不足 | 按顺序：优先开启混合显卡模式 → 针对引擎设置Prefer No Sysmem Fallback关闭CUDA系统内存回退 → 关闭无关后台应用 → 仍不足优先07-kv-rk4v4 → 再不足08-pool-small配套设34560/1536。保留7GiB Host KV，检索变小需重新验证长任务召回。详细驱动路径见整合包说明，不自动改全局设置；驱动空闲不等于CUDA可分配 |
| pi-ai stream idle timeout after 300000ms | 是客户端未收到可计时模型事件的watchdog；排队/长预填充/生成长工具参数/后端故障都需结合日志。SSE注释心跳未必重置计时。配置器已提供30分钟streamIdleTimeoutMs，但具体dsh版本是否采用需检查；本次没有群友日志，未确诊或根治，不盲目无限延长 |
| 输出提前结束、误调用或反复重试 | 记录finish_reason、output/context/thinking额度、工具错误、实际引擎hash。v0.1.5保留v0.1.4的解析及采样边界修复，不能保证全部误调用消失。半截参数现在明确报malformed_tool_call，不应当作完整命令执行；可让模型拆小任务或根据错误重试 |
| 思考里有答案、最终仍调工具 | answer计数对固定夹具有改善，模型/量化/前向责任仍未完全区分。可停止本包、设AnswerPenaltyScope=carry-over后作小范围对照；保留日志与其他参数，不删会话伪装恢复 |
| 网页能打开但工具不执行 | 查 `runtime/logs/dsh.stderr.log`、守卫与引擎日志；Edge浏览器工具需要系统Edge。网页200只能证明服务可访问，不能证明模型生成或实际工具链通过 |
| 移动文件夹或切模型后异常 | 先03-stop，再运行相应01-setup入口重新生成路径。切换模型新开会话；旧会话可保留。不要同时加载两个27B模型 |

日志目录：`runtime/logs/engine.stderr.log`、`engine.stdout.log`、`cache_guard.log`、`dsh.stderr.log`、`dsh.stdout.log`。对外提供前删除登录URL中的token、cookie、API密钥、私人任务与完整会话内容；只截错误附近必要行。诊断用 `04-verify.cmd`，不发送模型请求。

## 交给Agent的纠错提示词

```text
请排查NInfer v0.1.5整合包，目录：<填实际路径>。
现象和大约发生时间：<填写>。
先读AGENTS.md、docs/TROUBLESHOOTING.md、docs/OFFLINE_BUNDLE.md及更新说明。
保留模型、会话和配置；先只读检查文件哈希、端口/PID/路径、GPU/RAM占用及相关错误日志。
按安装→引擎→守卫→dsh→具体工具分层定位，不把网页200当作全链路通过。
不要复制全局依赖、递归删junction目标、批杀进程、改全局代理/驱动或升级到latest。
没有原始日志就明确哪些只是可能原因。区分模型自然EOS、额度耗尽、工具解析错误、客户端超时和CUDA故障。
保持xhigh、MTP、模型和页预算，必要参数调整说明影响并留下回退方法。
先提出有证据支持的最小修复，已授权范围内执行；测试结束报告通过、失败及未覆盖范围。
不得输出登录token或公开完整私有会话；女娲/人物技能仅明确要求时启用。
```

技术记录：[流空闲超时](DSH_STREAM_TIMEOUT.md) · [工具错误回执](TOOL_PARSER_EDGE_CASES.md) · [独立计数边界](ANSWER_PENALTY_SCOPE.md) · [显存与安装](INSTALL.md)
