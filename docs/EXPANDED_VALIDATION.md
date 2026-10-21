# 扩展验证：协议、分块、异常恢复及长上下文（2026-10-20）

本轮发现一个可重复的长上下文输出异常：模型的思考中列出了正确答案，最终却违反“本轮不要调用工具”的文字要求，调用了 `read_record`，正文为空。两次独立冷启动重现，补充明确纠正后仍重复同一调用。此问题尚未修复，也没有足够证据将根因归到模型、MTP、NVFP4 或解析器补丁中的某一项。

本轮只新增测试、复现材料和说明，没有修改或重新部署引擎。当前 exe 仍为 `d6c4f860c1d47680c68306aefba969d8c39f8ec792a49d6c743f51898d5fc96f`；唯一回滚引擎仍为 `8b449db9fe029d3ed442ef8e6ad1fdac6d3ea2dd61aaf3ee09315cc06bd87edd`。此前修复的原有 9 项失败保持已解决状态。

## 测试范围和结果

| 范围 | 实际结果 |
|---|---|
| 新增执行的 C++ Host 测试组 | 18/18：Chat、Responses、响应存储、Anthropic、PromptInput、采样默认、启动参数、请求日志、load/metrics、HTTP、准入、上下文成本、KV 容量、前缀缓存、资源管理、CPU 结构化语法 |
| 工具解析器分块属性检查 | 3,207 个有效 UTF-8 输入，12,825 次整段/1/2/7 字节及随机分块对照，零失败；包含 64 KiB 参数 |
| 正式输出链的损坏 UTF-8 检查 | 537 个输入，整段/逐字节/7 字节共 1,611 次解码，零异常，输出一致且可序列化 |
| Python 离线检查 | 16/16：serving corpus、benchmark matrix、speed-of-light 统计 |
| 缓存守卫与 dsh 流水线 | 9/9：HTTP 断连传播、上游失败、截断 SSE、正文及头保留、真实工具后处理和提示词组装；不请求模型 |
| 损坏 HTTP/JSON 与无效资源 | 11/11：残缺 JSON、数组封套、无效 UTF-8、孤立代理字符、NaN、字段类型错误、缺失 response/route |
| 真实模型场景 | 17 类中 16 类通过；长上下文直接回答场景失败，两次冷启动及一次纠正续接均未给出最终答案。共记录 28 个完成的生成请求 |

真实模型通过的场景包括三种 API 的流式/非流式文本、Responses 存储/续接/删除、中文及嵌套类型工具参数、双工具唯一 ID、三种 API 的工具结果续接、输出限长、自定义 stop、断连后恢复、改动历史分支后的正确性，以及超出上下文上限后的拒绝和恢复。

8 个非法参数请求经过核对接口约定后全部返回预期 400。初测将 Chat `max_tokens=-1` 当成非法值的预期有误：该引擎明确将它解释为“不限输出，只受剩余上下文约束”。保留初次记录，改用 `-2` 完成非法参数验证，没有为测试修改引擎行为。

C++ 测试从当前源码编译测试对象，链接已验证的本机增量构建对象和原静态库；不是干净全量构建，也没有运行全项目 CTest。KV=NVFP4、xhigh、MTP3、262144 context、39168 Device KV、7168 MiB Host KV、12288/19456/6144 窗口及生产采样保持原样。只有强制工具截断接口检查按既有约定使用单请求 `reasoning_effort=none`。

## 可复现的长上下文失败

合成档案不含用户历史。无工具定义时计数为 98,322 tokens，实际带工具生成的 prompt 为 98,592 tokens，seed 为 `20359323`，输出额度为 8192。

用户要求从内联的 8 条权威清单给出校验码和两项计算，并明确写明 `Do not call tools yet.`。两次冷启动请求体相同，均在思考里准确列出 8 个校验码及计算结果 `155`、`136`，最终却输出：

```json
{"name":"read_record","arguments":{"record_id":"expanded-recall-98304-20261019"}}
```

正文为空，按最终回答评分为 **0/10**。思考内可核实的事实为 10/10，仅能作为诊断，不能冒充任务已完成。

测试宿主没有执行真实查找，只回传“未执行查找，权威数据已在内联清单中”，再明确要求直接回答且不要调用工具。该轮复用了 100,541 tokens，只新增预填充 93 tokens，但仍重复同一函数和参数，正文为空。初次输出 1950 tokens，纠正轮输出 333 tokens，均远低于额度；底层完成原因是自然 `stop_token`。

三次日志均走正常工具解析路径：`recovered_from_reasoning=false`、`recovered=false`、`forced_call_closed=false`、`malformed_call_reported=false`。因此这次没有触发新加的未闭合思考恢复或猜测式补全，也没有记录到 CUDA 错误、页池耗尽或输出限长。正常解析诊断仍不足以排除生成或推测解码问题；本轮没有旧引擎、raw-token 或关闭 MTP 的配对对照。

由于直接回答检查失败，原计划其后的三轮显式长历史工具调用及最终工具结果审计没有执行。不能把短上下文工具续接通过写成这部分也已通过。

[复现脚本](../benchmarks/qualification/tool_choice_long_context.py)只请求已运行的服务，生成相同合成档案并保存请求、流式响应聚合及评分，不执行工具、不修改服务配置。与该次环境一致的服务启动后运行：

```powershell
python benchmarks/qualification/tool_choice_long_context.py --output-dir runtime/tool-choice-reproduction
```

默认目标为 98304 tokens、xhigh、8192 输出额度及相同 seed；最终回答失败时退出 2。其他硬件、模型或生成状态可能得到不同结果；此脚本的流式采集及夹具生成沿用本次实际执行的代码。

## 另外确认的行为边界

- **stop 可以截断思考。** 本轮人为指定的 stop 字符串在思考阶段命中，正文尚未生成，因此返回空正文。这符合当前匹配范围；没有证据表明此前用户任务的提前结束都由它造成。
- **历史分支会失去缓存。** 约 16K 公共历史顺序续接命中 16,149 tokens；修改较早的用户分支后命中为 0，需重读约 18 秒，答案仍正确。当前 `rolling`、`max_shared_prefixes=0`、释放分歧检查点的配置保留单条续接性能，未保留可复用的另一分支。本轮没有调整缓存策略。
- **截断调用仍需要重试。** 强制调用在 1/8/16/64-token 额度下均返回 `malformed_tool_call`；256-token 额度下返回完整、参数准确的真实调用。API 的 finish reason 都可为 `tool_calls`，底层限长诊断仍需另看。
- **底层解析器要求有效 UTF-8。** 人为破坏字节后绕过 OutputSession 直调解析器，初测出现 5 次 JSON 字符串编码异常。正式输出链会先修复 UTF-8；将全部 537 个损坏输入通过这条链重测均通过，HTTP 输入中的非法字节也返回 400。本轮没有据此新增解析器补丁。

120 次约 5 秒间隔的 `nvidia-smi` 采样中，显存最高已用 11,491 MiB，最低空闲 454 MiB。它们是 WDDM 观测值，不是 CUDA 可分配余量或保证的瞬时峰值；不同请求状态不能与旧轮结果直接相减作为新增占用。

未执行满 256K 的模型生成、多个独立 seed 的质量统计、完整真实 dsh/浏览器任务，或旧引擎配对质量比较。超长输入的正确拒绝不代表满 256K 推理已验收。测试服务已停止，未提交、推送或发布。

脱敏材料：[总记录](../benchmarks/evidence/expanded-validation/verification.json)、[长上下文失败](../benchmarks/evidence/expanded-validation/long-context-failure.json)、[18 组 Host 结果](../benchmarks/evidence/expanded-validation/host-test-results.json)、[生成请求诊断](../benchmarks/evidence/expanded-validation/request-results.json)。本机原始日志、夹具和维护脚本保留在 `runtime/expanded-validation/`。

后续[底层采样边界定位](LONG_CONTEXT_TOOL_LOOP.md)发现 MTP 同轮跨过思考结束后仍使用旧采样值的问题，并实现验证前草拟屏障。候选原 98K 请求、旧失败纠正及三轮工具/结果续接通过，但十个短测仍有误调用，尚未替换日常引擎。无草拟的贪心诊断也出现误调用，因此不能把全部现象归因于 n-gram 或 MTP；原始失败记录保留。
