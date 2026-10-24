# v0.1.4 — 工具调用、长上下文生成与状态一致性修复

本版将维护者当前使用的引擎及对应源码整理发布。主要改善工具输出解析和思考转回答边界，并修复 NVFP4 与 FP16 GDN 的一致性问题；新增回答阶段独立惩罚计数。本版定位是正确性与任务续接，未测得可对外承诺的通用提速倍率。

## 改了什么

- **工具示例不再误当调用：** Markdown围栏、行内代码和转义中的工具XML保留为文字。只在自然结束、思考未闭合且末尾确有完整已声明调用时恢复结构化工具；不对取消或额度耗尽进行此恢复。
- **截断参数不再被猜补执行：** 去掉无明确边界的第三次抢救解析；半截命令/写文件参数返回 `malformed_tool_call`，保留之前完整调用。原有8项解析器及1项前端失败已定位修复，可能需要客户端重试一轮。
- **思考转回答使用正确采样阶段：** MTP验证前截住跨越思考关闭标记的草稿，确认分隔符后再用回答采样；普通、Graph与point-proposal使用一致的位置抽样。移植并适配上游耦合采样，不整体升级上游。
- **NVFP4验证列分区：** 按物理列宽固定producer/reducer窗口，并限制reducer循环展开，修复固定输入对照的列差异和可复现的特定形状非法访问。编码与KV精度不变。
- **FP16 GDN状态一致性：** record/fold在每个token的状态存储边界模拟FP16舍入，避免省略舍入后累积偏离普通路径；独立FP64、Graph及长链资格验证通过。
- **回答独立惩罚计数：** 已提交思考闭合时可清零当前lane的presence/frequency计数，回答仍保留presence=1。引擎默认carry-over；本版部署脚本默认 `AnswerPenaltyScope=answer`，旧配置缺少字段也采用answer。设为 `carry-over` 并正常重启即可恢复跨阶段计数，保留其他修复。
- **便携整合包支持：** 增加离线安装、完整性校验、便携Node识别、两模型选择及配置切换入口；整合包由维护者另行提供网盘下载，GitHub引擎附件不含权重。

## 实测改善与范围

RTX5070Ti Laptop12GB，Huihui Swift-1.5 GSQ-RCO IQ3_XXS+MTP，NVFP4、xhigh、MTP3及原页预算。以下是最终无探针二进制的资格检查：

| 检查 | 结果与含义 |
|---|---|
| 同一短任务十seed | carry-over 9/10，answer 10/10；仅说明该固定任务改善，不能外推整体成功率 |
| 六类直接任务和工具/结果步骤 | 12/12 |
| 98,592-token输入及三工具/三结果续接 | 7/7，最终十项召回全对；六次续接整轮约1.02–1.35秒，非受控提速对照 |
| 最终二进制开启组汇总 | 29/29，缺省carry-over短组9/10 |
| 与最终对象匹配的Host回归 | frontend55组、parser39组通过 |
| 较早独立计数原型的另一模型对照 | 原版GSQ同题7/10→9/10，仍有误调用；此行是原型证据，不冒充最终二进制复测 |

各阶段测试不能全部当作最终引擎重复实测。早期205K工具续接、GDN数值及NVFP4关系测试有各自二进制/条件记录，见[完整说明](ANSWER_PENALTY_SCOPE.md)、[GDN修复](GDN_FP16_CONSISTENCY.md)、[耦合采样与NVFP4分区](COUPLED_SAMPLING.md)、[工具解析](TOOL_PARSER_EDGE_CASES.md)和[最终安装证据](../benchmarks/evidence/long-context-tool-loop/answer-scope-trial-installation.json)。

## 代价和未解决问题

发布安装验收通过：整合包35,053文件及两模型哈希、引擎help、便携Node/dsh、离线配置与两模型切换、独立端口认证网页；9项工具管线测试、2项Python测试、16组合启动配置和5类完整性校验通过。没有新增模型推理或完整Agent任务实测，日常服务未重启。[发布验收](../benchmarks/evidence/publication/verification-v014.json)。

没有新增本轮常驻GPU计数数组；重用已有计数并在边界清零。GDN按原FP16状态存储精度计算，可能改变输出及投机接受率；采样边界也可能增加少量target步骤。独立计数有意改变回答分布，无法保证所有模型/写作任务都更好。工具恢复额外使用少量CPU处理和随思考长度增长的RAM文本。没有通用decode速度或显存降低承诺。

满256K、多lane、其他GPU、非零frequency和DFlash工作流未验收；仍有模型误调用、事实错误及部分耗尽思考额度的案例。真实dsh隔离夹具在发送模型请求前因依赖映射启动失败，最终完整Agent任务由维护者继续实际使用验证。网页200不代表Agent全链路通过。群友的 `pi-ai stream idle timeout` 仍缺对应日志，不能宣称本次根治；见[排错说明](TROUBLESHOOTING.md)。

构建为受影响单元增量重编及复用原库，未完成干净全量构建或全部上游测试。当前预编译目标Windows x64/sm120a，直接验证设备为RTX5070Ti Laptop12GB。

## 下载、升级及回退

使用v0.1.4源码和引擎附件，原安装先完成任务、运行其 `stop.cmd`，保留会话、配置与模型，再更新源码并重跑 `setup.ps1 -ModelPath "<现有模型.ninfer>"`。只更新源码不会替换已下载exe。历史Release资产保留供回退；回退引擎时同时使用匹配版本启动脚本。

新部署保持完整保守组合DeviceKV36864 / Retrieve3840 / HostKV7168MiB、sink12288、recent19456，逻辑容量262144。**NVFP4相对此前RK4V4配置增加一些显存占用；显存紧张可设置 `KVDType=rk4v4` 并正常重启，同一引擎支持两者。** 不能把驱动整卡空闲当作CUDA可分配预算，切KV或模型后缓存重新建立。

显存处理建议按顺序进行：优先混合显卡模式 → 对引擎关闭CUDA系统内存回退 → 关闭无关后台应用 → 优先切RK4V4 → 再配套适当缩小页池和检索窗口。保留7GiB KVMem Host KV，关闭驱动溢出不等于关闭历史缓存。小页池34560/1536档未做新的实模质量验收，缩检索可能降低召回。

夸克一键整合包已包含引擎、两个ninfer模型和用户态运行依赖，无需GitHub即可安装。整合包详见[离线整合包说明](OFFLINE_BUNDLE.md)，包含安装提示词、纠错提示词、依赖及两模型许可。Swift-1.5衍生权重适用Swift Open License v1.0（含商业使用门槛），不能套用仓库代码Apache-2.0许可；原版GSQ-RCO模型另按其Apache-2.0条款。

特别感谢Bilibili [0xFFFF7](https://search.bilibili.com/upuser?keyword=0xFFFF7)提供基础分享版本；保留NInfer、KVMem、Qwen、ISTA-DASLab、Huihui、UkisAI及各依赖作者归属。

引擎SHA-256：`8804cb04470214f68e01e38cb07004c90e748507a77134995335d1edbd768472`。附件ZIP校验见 `SHA256SUMS`。GitHub仅分发源码/引擎；网盘包不含私人会话、登录信息或本机配置。
