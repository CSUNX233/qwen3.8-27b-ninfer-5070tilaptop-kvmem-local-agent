# 本发行版的修改范围

基础引擎和大多数内核来自上游及原分享包；本页描述后续本机工作。逐项 patch 和验证记录在 `benchmarks/evidence/`。继承的 Windows/TMA、KVMem-ring、头部保留、MTP 取消恢复、one-dot 检索等修复也随源码保留，不将其归为本轮原创。

v0.1.5（2026-10-24）将 [C2.1 长锚点与批量回迁移植到 v0.1.4](C2P1_V014_PORT.md)，保留 v0.1.4 的采样、工具和 GDN 修复。12 项定向回归及两个多轮长上下文夹具通过；同一二进制现作为v0.1.5发布；保留增量构建边界。构建来源、正确性范围与代价见该记录。

| 后续改动 | 目的 | 关键位置 |
|---|---|---|
| 检索索引移到 Host RAM | 释放显存，保留原 FP32 索引与打分数学 | `engine/src/core/mapped_host_buffer.*`、`ops/kvmem/mean_k_index.*` |
| 大缓存接管与页生命周期 | 共享不可变历史、尾页 COW、统一主模型/MTP/临时页预算、按需恢复、释放顺序 | `models/qwen3_5/program/storage/`、`planning/request_plan.cpp`、`transactions/materialization.cpp` |
| 检索分数异步回传 | 减少 GPU 结束后再启动 CPU 选页的串行等待 | `ops/kvmem/kvmem_select.*`、`kvmem_score.h` |
| 双 scratch 预取索引 | 一个缓冲打分时提前上传下一层索引；最终 scratch 共 32 MiB | `ops/kvmem/mean_k_index.*`、`execution/text.cpp` |
| 检索上下文与检查点 | Program 独占工作区；RAM 分片 COW 快照；索引与排名恢复；绝对 token 追加、真实 count 与选页版本 | `program_impl.*`、`storage/context.cpp`、`prefill.cpp`、`transactions/`、`ops/kvmem/` |
| decode历史索引与缓存问句 | 仅追加主模型已提交历史，隔离MTP头；合法缓存问句重评分，精确问句末尾保留原排名 | `decode.cpp`、`prefill.cpp`、`speculative/mtp.cpp`、`kvmem_score.h`、`mean_k_index.*` |
| 融合采集旋转前K | 非查询评分分块恢复norm/RoPE融合并同核采集，独立FP64及精确Q/K位验证 | `execution/attention.cpp`、`ops/rmsnorm_rope/` |
| 5070 Ti 小矩阵调度 | 只改变指定 IQ2_S / IQ3_XXS / Q2_K、T=1/4 的线程与行分组 | `engine/src/ops/linear/gguf/ggml_bridge_vec.cuh` |
| v0.1.4：工具输出解析 | 代码中的工具示例保留为文本；自然结束的未闭合思考中，仅恢复完整且已声明的末尾调用；增加诊断 | `frontend/tool_call_parser.*`、`frontend/output_session.cpp`、`serve/*log*` |
| dsh 接入与公开部署 | 相对路径、隔离 home、固定依赖和哈希、严格守卫、搜索/浏览器、收尾提醒、工具验证执行规则 | 根目录脚本、`scripts/`、`integrations/` |

既有小矩阵 CUDA 调度只匹配 compute capability 1200 且 46 SM 的设备以及实际验证过的形状，共 11 个路由；其他形状保留原调度。每行数学、量化解码、累加和归约顺序保持原样，无新增 GPU buffer。启动前设置 `NINFER_VEC5070_DISABLE=1` 可以关闭这项调度；必须重启，不能对已经捕获的 CUDA Graph 热切换。

v0.1.0–v0.1.2 的上述优化未改变权重、主模型量化、rk4v4、context、MTP 3、检索选择预算、生产采样和默认 ngram copy draft。v0.1.3 将公开默认 KV 改为 NVFP4，并提供 `KVDType=rk4v4` 回退选项；权重、采样及页预算继续保留。`--lm-head-draft` 没有启用，避免额外常驻 proposal 权重。没有启用本地 profiling 探针。

2026-10-15 增加的 dsh 工具验证执行规则属于提示词引导，保留 xhigh 和输出额度；不修改引擎、不保证解决所有死循环。接入方式与开关见 [集成说明](../integrations/README.md)，独立开发任务结果见 [实测报告](BENCHMARKS.md#dsh-工具验证执行规则2026-10-15)。CUDA 系统内存回退的禁用建议由用户在驱动的程序设置中手动配置，公开脚本不自动修改。

随后补充截图后的尾部验收续接信息，以及缓存代理的断连取消传播。静态系统规则、旧消息和模型参数保持原样；新增离线 HTTP 与真实 dsh 工具流水线回归测试。这些检查覆盖接入与生命周期行为，不能代替约 177K 原任务的模型行为复测。

代码从本机源码快照导出；已记录的修改文件有醒目修改说明，完整列表在 `SOURCE_PROVENANCE.json`。原工作部署是保留静态库后的增量重编译/链接，首版 v0.1.0 二进制 SHA-256 为 `940e68278ae91c60e7ed8e70fc495efec1686fb16b7362f0002d6498e609413d`；公开源码增加归属注释和部署文件。发布验证不冒充对所有上游测试的干净全量重跑。

2026-10-15 本地进一步安装原生检索修复，验证引擎 SHA-256 为 `34fd593d76bd332c1b8470ce5ffe4fd9e7e9820229558cffd1cf9ddaa386224b`。原始 KV 编码、采样、xhigh、MTP 3、Device/Host 页预算及 5070 Ti 内核调度保持原样；修正的是检索索引的状态语义。新快照在普通 RAM，未新增 GPU buffer。本轮源码、框架补丁、9 项回归测试和脱敏证据随 v0.1.1 发布，安装器下载相同哈希的新引擎；v0.1.0 资产不覆盖。此次以状态正确性和缓存续接稳定性为目标，没有统计上成立的总体提速结论。

参考 [kvmem/kvmem](https://github.com/kvmem/kvmem/tree/0af10ece3d78edb905c62357e52a62147df69d58) 的上下文截断、检索恢复与 tail checkpoint 契约，独立实现本引擎的 Program/COW 方案，没有移植该仓库源码。[ActKV](https://arxiv.org/abs/2609.31395) 使用面向动作的 KV 压缩；其平均准确率并非与 FullKV 等同，本轮没有采用淘汰或自适应缩减预算。验证与已知边界见 [实测](BENCHMARKS.md#原生检索上下文修复2026-10-15) 和 [部署手册](DEPLOYMENT_AGENT_GUIDE.md)。

2026-10-16的v0.1.2进一步发布生成历史索引、缓存问句保护与融合采集；当前引擎哈希以SOURCE_PROVENANCE.json为准。公开默认保持36864/3840起步，39168/6144作为进阶档单独报告显存与召回。诊断导出仅显式环境变量开启，日常不启用。新一轮预填充实验另行验证，不混入本次已验证包。[实测及成本](BENCHMARKS.md#语义历史修复与检索预算2026-10-16)

2026-10-19本地增加[SM120 NVFP4 prompt 调度](NVFP4_SM120.md)：8 warp、成对解码、跳过全屏蔽页，保留原内核和启动开关 `NINFER_NVFP4_SM120=0`。不更改 KV 编码、Q 量化、检索预算或 decode/small-T 内核；稀疏跳页可能有浮点尾差，不承诺逐位相同。独立数值/Graph 和 128K/200K 召回通过；初次累计长输出失败另由[Host压力修复](KV_HOST_PRESSURE.md)处理：释放重复Host副本，补上Host-only闲置owner的既有LRU回收。保留活动历史、预算和复用保护；共享引用、pins、部分尾页及异步H2D受保护。完整v2通过累计512行、六工具及18/18召回，已部署本机并备份旧exe。本次随v0.1.3发布当前exe、源码、脱敏证据和新下载清单，旧v0.1.2资产保持原样。默认NVFP4相对旧RK4V4多占一些显存；紧张时可通过`KVDType=rk4v4`重启切换，同一exe保留通用修复。仍为本机增量构建，未合入后续只读评估的上游修复。

2026-10-20 第一批参考修复已在本地完成：[工具输出解析](TOOL_OUTPUT_RECOVERY.md)。参考上游 Markdown 保护及未闭合思考恢复提交，适配本地容错接口并补齐多反引号、转义与 tilde 首行保护。下游日志 schema 升为 28。205124-token 合成召回为 10/10，后续工具与结果续接命中约 207K 缓存；定向检查通过，当时继承测试原有 9 项失败前后均存在，无新增失败。未新增 CUDA buffer，增加 CPU 解析与开放思考文本 RAM；不改变 NVFP4、采样、xhigh、MTP3 或页预算，不是所有 EOS 提前结束的通用修复。该批 exe 为 `8b449db9fe029d3ed442ef8e6ad1fdac6d3ea2dd61aaf3ee09315cc06bd87edd`。尚未提交、推送或发布，公开安装器仍获取原 v0.1.3；MTP 分区、anchor 和共享缓存候选修复尚未应用。

同日第二批[逐项修复原有 9 项失败](TOOL_PARSER_EDGE_CASES.md)：移除两次解析失败后的猜测式参数补全/截短，修正封套说明文字误拦截，保留边界明确的容错及上一批全部行为。两个完整测试程序及隔离函数检查通过，解析器 39/39、前端 54/54。实际模型 49452-token 召回 10/10，长历史工具/结果、截断回执及下一轮重试通过。半截参数改为明确错误，可能需要额外生成轮次；没有新增 GPU buffer。当前本地 exe 为 `d6c4f860c1d47680c68306aefba969d8c39f8ec792a49d6c743f51898d5fc96f`，唯一备份更新为第一批的 `8b449d…`，仍未发布。

随后完成[扩展验证](EXPANDED_VALIDATION.md)：新增执行的 18 组 Host 测试、分块/UTF-8、HTTP、Python 和守卫/dsh 离线检查通过；真实模型 17 类场景中 16 类通过。约 98.6K 带工具输入的直接回答出现可重复失败，思考中的事实正确，最终却在文字要求禁止调用时重复发起工具调用，明确纠正后仍未恢复。本轮保留失败与复现脚本，没有修改引擎，尚未定位模型/生成/MTP 等因素的根因。

之后实现未安装的[底层采样边界候选](LONG_CONTEXT_TOOL_LOOP.md)：Scheduler 传递思考关闭 token，MTP 神经及 n-gram proposal 在验证前截到标记之前；标记后单步确认分隔符，由已提交前端状态切换回答采样。保持合法引用、raw 规范前缀和耗尽预算拒绝，不新增 GPU buffer，不改变 KV、模板、采样值或页预算。闭合附近可能增加 target 步并改变采样序列。本地候选原 98K、旧失败纠正与三轮工具/结果续接通过，短测 8/10 相比原版 9/10 没有改善，误调用仍在正确回答采样下产生；无草拟贪心诊断也失败。仅实现 MTP 屏障，未声称 DFlash 或总体质量改善，未部署、提交或发布。

同日继续移植 [NInfer PR #38 耦合采样](COUPLED_SAMPLING.md)：point proposal 每个位置采用 target 的统一 decode-key 抽样，普通解码与 Graph 输入同步对齐 cache+1；DFlash2 sparse-q 拒绝采样保持原样。独立草拟/FP64采样、MTP状态、Graph及CPU回归通过，原冷/热四请求全部正确且同seed逐字相同；十seed为9/10，仍有误调用。98K召回、六工具/结果及旧失败历史纠正通过。候选保留未安装，未作总体质量或性能改善结论。

随后继续移植同页记录的上游 NVFP4 验证列分区修复：按物理列宽固定归约窗口，缩短有效列时共同输出的 2 处逐位差异归零。新编原源码也会触发的 h16/kv2、129-split 非法访问，对自动循环展开敏感；限制 NVFP4 reducer 的循环展开后，完整关系、独立数值、Graph 和宽输入测试均通过。未获得有效 Sanitizer 检查，未修改驱动。短测仍为9/10，十次文本均与耦合候选相同，不能将分区问题当作本机短任务误调用根因。

2026-10-20 随后按用户要求将最终 `b1f832…` 候选替换为本机日常试用引擎，原日常 `d6c4f8…` 更新为唯一备份；更早 `8b449d…` 备份被替换。安装后冷启动/缓存两次10/10、正文和思考一致；启动、模板、采样及KV预算未变，未发布。此状态更新以[安装记录](COUPLED_SAMPLING.md)为准，前述未部署状态属于历史阶段。

2026-10-21 新建未安装的 [FP16 GDN 一致性候选](GDN_FP16_CONSISTENCY.md)：修复投机验证及 ReplaySSM 提交跳过 token 间 FP16 状态舍入的问题，使同输入的输出/提交状态与普通逐 token 解码一致。补充 FP16 精确关系、独立 FP64 与 Graph 回归；没有新增 GPU buffer、调整 KV/采样/页预算。真实短测仍为9/10，失败从 short-8 转为 short-6，不能声称总体质量改善；日常与唯一备份保持原版。

随后进行[短任务回答阶段惩罚诊断](SHORT_TASK_SAMPLING_DIAGNOSIS.md)，本轮没有生产源码修改。仅请求覆盖回答 presence=0 的同候选配对，Huihui同题十seed由9/10变为10/10，GSQ由7/10变为9/10，思考前后相同；确认跨阶段惩罚计数会改变JSON/工具候选排序。局部采样和完整计数核对正常，原始工具XML在解析前已存在；剩余失败未根治，独立前向验证因下载未完成而未执行。Huihui功能与工具步骤前后各12/12、Huihui98K组7/7，不代表长正文重复风险已验证。日常Huihui及原采样已恢复，候选未安装，未提交或发布。

继续验证[回答阶段独立惩罚计数](ANSWER_PENALTY_SCOPE.md)：仅在私有Host覆盖中新增进程级opt-in，在已提交思考闭合处复用现有memset清零当前lane计数，回答presence保留1.0；默认carry-over不变，无新增GPU常驻buffer或CUDA重编。最小语义补丁已导出供审阅，但未应用到正式源码，1699个正式源码文件与本轮开始快照hash一致。Huihui同题十seed10/10、GSQ9/10；32步完整计数/CPU采样核对通过。功能组Huihui12/12、GSQ三策略均11/12且同题失败，代码及重复字段JSON通过，Huihui98K组7/7；六篇新版正文仅结构通过，仍有事实性错误，原正文题两策略均在思考中耗尽额度。边界测试未根治误调用，独立前向、并发多lane、正式每请求API及受控性能未验收。日常原引擎、模型、采样及服务已恢复，唯一回滚保留，候选未安装、未提交或发布。

### 2026-10-21 无探针独立计数日常试用

正式源码增加已提交思考闭合的 `begin_post_thinking_sampling` 接口，普通 `update_sampling` 继续保留计数。进程级 `NINFER_POST_THINKING_PENALTY_SCOPE=answer|carry-over` 选择计数范围；缺省carry-over，本地试用answer。重用当前lane计数数组并在原CUDA stream清零，无新GPU常驻buffer，presence/frequency共享范围；模型上下文仍保留。相对上一日常同时纳入FP16 GDN record/fold舍入一致性修复。

新日常 `8804cb04470214f68e01e38cb07004c90e748507a77134995335d1edbd768472`，唯一最近备份 `b1f8326fceeed5d9c5bcd9db30fc166947c1382599358b964fa5500da582f941`。无探针组29/29、缺省短组9/10和准确对象Host55/39组通过；真实dsh夹具依赖映射启动失败，尚未发送模型请求，用户选择自行测试。安装后仅做只读启动/完整性核对。清理7exe、8.751 GiB及两处测试junction，真实依赖保留，服务未停止/重启；未提交或发布。历史各节状态按当轮理解，当前详见[试用说明](ANSWER_PENALTY_SCOPE.md)和[脱敏安装证据](../benchmarks/evidence/long-context-tool-loop/answer-scope-trial-installation.json)。


## v0.1.4公开整理

上述未发布阶段记录保留为历史证据；本次发布同一8804cb…引擎及正式修复源码。新增部署AnswerPenaltyScope配置、便携Node识别、离线文件校验与双模型选择，默认xhigh/量化/页预算保留。可选34560/1536小页池仅作配置容量核对，无新增实模资格结论。修复发布CI的JavaScript文件路径检查（git返回字符串而非FileInfo）。实测收益、代价及尚未解决问题统一见[本版更新](RELEASE_NOTES.md)。
