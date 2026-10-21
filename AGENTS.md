# Deployment and maintenance agents

部署、启动、排障、升级或性能任务，先读 [部署与维护交接手册](docs/DEPLOYMENT_AGENT_GUIDE.md)，再核对相关脚本与实际运行状态。普通文档修改只需读取相关部分。

- 版本依据：`config/downloads.json`、两个 npm lockfile、模型文档；记录源码 commit 与引擎哈希。不要把未验证的新上游版本直接混入已验证部署。
- 先识别已有实例、GPU/RAM 占用和端口；停止本安装使用 `stop.cmd`。不要批量终止 Node/Edge/Python，不自动修改全局代理、驱动策略或显卡模式。
- 保留 xhigh、量化、采样、页池、检索及缓存基线。改变这些条件须说明影响、记录对照并提供回滚；不要以降低质量或删历史来伪装提速。
- 新部署先用保守组合 `DeviceKV=36864 / RetrieveTokens=3840`；不要直接套用维护者的 `39168 / 6144`。先清理实际独显占用，通过短任务、缓存、工具续接后逐档增加，保持页池与 sink/recent/retrieve/MTP/临时页预算一致，不能仅缩小池而保留过大的窗口。
- 显存处理顺序：优先混合显卡模式；针对实际引擎关闭CUDA Sysmem Fallback（保留KVMem Host KV）；关闭无关后台；仍不足优先RK4V4；再配套缩页池/检索窗口并说明召回代价。离线包34560/1536小档是容量选项，未新增实模验收。
- v0.1.4 部署默认 `AnswerPenaltyScope=answer`，引擎本身默认carry-over。关闭新策略设 `carry-over` 后正常重启，其他修复保留。测试数据仅是固定夹具，不宣称所有循环、超时或模型误调用已解决。离线包先读 `docs/OFFLINE_BUNDLE.md`、`docs/TROUBLESHOOTING.md`。
- v0.1.3 默认 `KVDType=nvfp4`，旧配置缺省也使用 NVFP4；本机相对 RK4V4 有一些额外显存占用。显存很紧张时可由使用者在 `config/local.json` 设置 `KVDType=rk4v4` 并重启；同一引擎支持两者，无需回滚 exe。切换后 GPU/Host 缓存重新建立，不能承诺两种量化质量相同或固定的显存差值。
- 有明确下一步时通过实际文件检查、最小实现和测试推进，保留复杂问题所需的充分思考。验收区分引擎、守卫、dsh 和工具；长上下文修改检查长输出后的工具续接与召回。
- 运行数据在 `runtime/` 与本机忽略目录；模型、会话、登录 token、compaction dumps 和原始私有日志不公开。清理前确认路径、用途和数据保留要求。
- 女娲（huashu-nuwa）及人物视角仅在用户明确要求时启用；普通部署、debug、Skill 工作和修改女娲设置不属于启用请求。

本文不代替用户授权和当前环境的权限规则；实际检查未通过时，明确报告问题与未验证项。
