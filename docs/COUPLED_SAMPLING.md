# 上游耦合采样候选（2026-10-20）

**本机安装状态更新：** 2026-10-20 按用户要求，最终 NVFP4 分区候选 `b1f832…` 已作为日常试用引擎安装；本次替换前日常版 `d6c4f8…` 为唯一回滚备份。安装后冷启动/缓存两次均10/10且输出一致，恢复入口已作只读演练。仅本机替换，尚未提交、推送或发布；短任务误调用仍未根治。详见[安装验证](../benchmarks/evidence/long-context-tool-loop/local-installation.json)。下文“未安装”是各阶段验证发生时的记录。

本轮继续排查[短任务与长上下文误调用](LONG_CONTEXT_TOOL_LOOP.md)，移植 [NInfer PR #38](https://github.com/iamwavecut/ninfer-all/pull/38) 的耦合验证，精确实现提交为 `bc53c472725c37b03b68d22a8e5d96f73407a1c8`。[KVMem issue #129](https://github.com/kvmem/kvmem/issues/129) 记录了相同问题及上游引入情况。本候选保留上一轮 MTP 思考后采样屏障。

## 改动与契约

原 point-proposal 验证分别使用接受、纠正和 bonus 随机数。相同 seed、相同位置即使具有相同 target 分布，只要草拟不同，就可能走不同随机数用途并产生不同输出。本轮让每个验证列用 `(seed, old_length + column + 1, Decode)` 直接抽取 target token；与草拟相等则继续接受，否则提交这个 target token。普通解码同步使用输入缓存位置加一，CUDA Graph 的代表输入也填充该字段。普通采样及验证共用相同的逆 CDF 选择函数。

该契约限定于相同 target 分布。不同验证宽度、批量和浮点归约仍可能改变 logits，不承诺整模型普通解码与 MTP 逐字一致，也不保证模型遵守工具指令。point/greedy proposal 的接受边际仍为 target 对草拟 token 的概率；DFlash2 sparse-q 保留原拒绝及残差采样。

内部 Op 与 launcher 同步改名为 `speculative_accept_coupled_drafts`，所有本地调用者及基准更新，未保留旧内部符号。调用这些 C++ Op 的外部自建消费者需要重新编译。此改名不改变 HTTP 协议或模型文件格式。

NVFP4、模型权重、xhigh、模板、生产采样值、MTP3、n-gram15、262144 上下文上限、页池及检索预算保持原值。普通解码 ingress 增加八个 int32 采样位置，即 32 字节字段；Host/Device 控制布局随之变化，未新增大块 GPU 工作区或缩减 KV。启动日志仍报告 CUDA 余量 64.0 MiB，不能将其当作所有负载的峰值保证。固定 seed 的输出与接受率可能变化。

## 已完成的对照

| 检查 | 结果 | 能支持的结论 |
|---|---|---|
| 完整草拟采样内核程序 | 通过 | 独立 CPU oracle、普通采样耦合检查，以及惩罚、mask、批量、Graph、非有限数等契约通过 |
| 前端、解析器与 admission 完整程序 | 全部通过 | 保留既有恢复、解析与预算行为 |
| 原冷/热顺序四请求 | 4/4 | `short-2` 三次正文、思考逐字相同，缓存命中 813 tokens 后未再误调用 |
| 原十 seed 短任务 | 9/10 | 上轮边界候选为 8/10，日常旧版为 9/10；本轮仍有一个新的失败 seed |
| 临时无草拟对照 | 3/3 | 新失败 `short-8` 冷/热两次正确且逐字一致；只是诊断，不作为部署方案 |
| 98,592-token 直接回答 | 10/10 项答案 | 最终正文符合清单与计算要求 |
| 长历史工具调用与结果 | 六请求全部通过 | 包含合法重复读取；六次均复用约 100K 历史 |

新失败 seed 为 `20359331`。它在思考中得到了正确事实与计算，也明确表示不应调用工具，但最终仍产生 `read_record`，正文为空。没有将思考中的正确 JSON 当成最终答案。原来的两个失败 seed 通过，新的失败仍按失败记录；整体短任务成绩没有超过日常旧版，不能宣称误调用已经根治。

本机已完成 71 个 Host 替换单元重编，以及 sampling、speculative 两个 sm120a CUDA 单元重编，复用前轮七个 CUDA 替换对象和其余原静态库。不是干净全量构建。候选 SHA-256 为 `ec4b053ce072070369a2c30027c4e76b4c5994e1c40cd28c9ee32e3ea8124f4c`。

生成文本不同，且长输入预填充期间进行了 Host/CUDA 编译，因此本轮不构成受控性能对照。短十测观测约 86–116 tokens/s，临时无草拟约 44 tokens/s；这些数字不代表固定性能收益。满 256K、其他模型/设备、真实 dsh/浏览器全链路及 DFlash 整模型未验收。

候选尚未安装、提交、推送或发布。日常引擎仍为 `d6c4f860c1d47680c68306aefba969d8c39f8ec792a49d6c743f51898d5fc96f`，唯一回滚备份仍为 `8b449db9fe029d3ed442ef8e6ad1fdac6d3ea2dd61aaf3ee09315cc06bd87edd`。最终追加验证与脱敏记录见 [coupled-verification.json](../benchmarks/evidence/long-context-tool-loop/coupled-verification.json)；维护脚本及原始合成流位于本机忽略的 `runtime/long-context-loop/`。

## 后续：NVFP4 验证列分区

继续移植上游提交 [`ddacb967`](https://github.com/iamwavecut/ninfer-all/commit/ddacb9673e5702232b96be220920b44b3bbaa82d)：NVFP4 Small-T 的 producer 与 reducer 按首位置加物理列宽划分 KV，受可见容量约束；有效列减少时保持共同列的归约边界，并禁止加载 live end 以后的键。新增的公共窗口函数没有改变 BF16、INT8、FP8、K8V4 的分区，也未移植 DFlash eager envelope 改动。

固定 Q/K/V、历史、物理宽度和 envelope，只缩短 `valid_columns` 的逐位对照，在原静态库上发现 2 列不同：h16/kv2、上下文 8190、物理宽度 16、有效列 8 对 16。本机模型 h24/kv4、上下文 818/853 的对应测试未发现这一差异，因此不能将它认定为短任务误调用的根因。

新编 NVFP4 单元时另遇到 h16/kv2、129 splits 的 reducer 非法访问。未改分区的原源码重编也失败；单独增加同步无效，设备 printf 会掩盖失败。限制最后合并循环为 `#pragma unroll 1` 后，不加打印或同步即可通过。该修改保留逐项求和顺序；目前只确认故障对 CUDA 13.0/sm120a 生成代码敏感，没有定位具体错误机器指令。Compute Sanitizer 因 WDDM 调试接口初始化失败，未能提供有效的显存检查；未修改驱动设置。

仅给旧分区增加循环限制后，相同完整对照仍有 2 列不同；同时修复分区后归零。两种几何的 44 组关系测试、NVFP4 独立数值、Graph 与宽输入测试均通过，无 CUDA 错误。最终候选在耦合候选上额外重编一个 NVFP4 CUDA 单元，仍复用其余原库，非干净全量构建，SHA-256 为 `b1f8326fceeed5d9c5bcd9db30fc166947c1382599358b964fa5500da582f941`。本阶段未增加持久 GPU buffer 或改变 KV 编码、模型及预算；分区和循环代码生成可能影响浮点结果与速度，需以实模验证为准。

最终候选另完成 22 个实模请求：短十 seed **9/10**，冷/热穿插 **4/4**，长任务组 **8/8**。十次短任务的思考、正文与前一耦合候选逐字相同，失败仍为 `short-8`；同 seed 的三次 `short-2` 逐字相同，热缓存命中 813 tokens。98,592-token 请求最终答案 10/10，三轮显式工具及三轮结果全部正确，六次复用 100,729–101,247 tokens、整轮约 1.02–1.51 秒；旧失败历史纠正最终 10/10。冷长输入约 260.44 秒，纠正分支重读约 170.17 秒；短十测约 84.7–116.6 tokens/s。这些是单次观测，没有构成受控性能或总体质量结论。

完整脱敏记录见 [partition-verification.json](../benchmarks/evidence/long-context-tool-loop/partition-verification.json)。剩余短任务误调用尚未根治，最终候选未替换日常引擎、提交或发布。已清理被其替代的旧候选与临时测试 exe，保留最新候选、源码、构建对象、日志，以及原有唯一回滚引擎。满 256K、其他设备/模型和真实 dsh/浏览器全链路仍未验收。
