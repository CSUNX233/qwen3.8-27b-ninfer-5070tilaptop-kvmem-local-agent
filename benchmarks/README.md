# 测试数据与复现

完整方法、硬件和解释见 [实测报告](../docs/BENCHMARKS.md)。`evidence/` 保留各阶段摘要和源码 patch；这些是阶段性证据，不能把不同版本、workload 和日期相加成总提速。

v0.1.4公开发布和离线包安装验收见 [verification-v014.json](evidence/publication/verification-v014.json)：35,053文件/两模型哈希、便携运行依赖、离线安装、配置切换和隔离认证网页通过，没有新增真实模型任务。下方候选/未发布字样按各阶段历史理解。

`run.py` 使用 Python 标准库，直接请求本地 API，提供 smoke、固定短任务、约 62K 冷/热输入和合成 Agent 缓存压力夹具。它不读取用户会话或文件，只创建自己的验证码文件；结构化 `read_file` 的工具结果由测试宿主执行读取后写回模型。

```powershell
python .\benchmarks\run.py --mode smoke
python .\benchmarks\run.py --mode short --label on-a
python .\benchmarks\run.py --mode long --label long-a
python .\benchmarks\run.py --mode agent --label agent-a
```

输出写入 `runtime/benchmarks/<label>/`，默认不进 Git。先确认 GPU 空闲。完整输出相等需要相同启动状态、请求顺序、权重与采样；短请求先后也可能改变投机/缓存状态。

`qualification/qualify_vec.cpp` 与 `matrices.json` 保留数值资格验证源。程序独立 CPU 解包实际模型量化值、用 FP64 累加，与公开 Linear/LinearAdd/SwiGLU 的 FP32/BF16 输出对照。编译需引擎库、CUDA 和对应模型；测试结果在 `evidence/decode-5070/qualification.json`。

110K 早期测量、CPU 视觉和真实 dsh 联网验证属于各自旧阶段；最新公开打包的额外验证见 `evidence/publication/verification.json`。合成重复文本的高接受率不能当成普通任务吞吐。

2026-10-20 的[扩展验证](../docs/EXPANDED_VALIDATION.md)包含协议、随机分块、UTF-8、异常恢复及真实模型场景。`qualification/tool_parser_properties.cpp` 保留解析器属性夹具；`qualification/tool_choice_long_context.py` 可对已运行服务复现合成长上下文的工具选择检查，默认约 98K 输入，失败退出 2。它不启动服务或执行工具。本轮发现的重复工具调用尚未修复，不能将思考内答案当成成功的最终输出。

同日[采样阶段边界候选](../docs/LONG_CONTEXT_TOOL_LOOP.md)及脱敏记录在 `evidence/long-context-tool-loop/verification.json`。MTP 验证前限制草拟跨越思考关闭标记，原 98K 请求及工具续接通过，但短测仍存在误调用，候选未安装；关闭草拟/贪心仅为诊断对照，不代表部署参数或总体质量统计。

追加的 12 次短请求区分冷启动、缓存命中、穿插请求、贪心与无草拟；证据中的 `short_cache_followup` 保留局部 target logits 与独立随机采样核对。未新增生产补丁，不能把该组冷/热输出不同直接当成状态污染，也不能把无草拟三次通过当成通用修复。

追加[上游耦合采样候选](../docs/COUPLED_SAMPLING.md)，证据为 `evidence/long-context-tool-loop/coupled-verification.json`：原冷/热四请求4/4并逐字一致，十seed9/10仍有误调用；98K、六工具续接和纠正通过。普通采样FP64分布、草拟CPU oracle和耦合关系、MTP状态、Graph及CPU回归均通过。原参数不变，无草拟仅作诊断，候选未安装；不构成总体质量或受控性能结论。

后续 NVFP4 分区与 reducer 循环展开修复仍记录在同页，证据为 `evidence/long-context-tool-loop/partition-verification.json`。原分区的2处逐位差异在44组关系测试中归零，独立数值、Graph与宽输入通过；只限制旧分区循环展开的对照仍有2处差异。实模短测9/10、冷/热4/4，98K及六次工具续接、旧失败纠正组成的长任务组8/8；短十测文本与耦合候选相同，剩余误调用未解决。生成代码敏感的非法访问及 Sanitizer 未能运行的限制均单独记录，候选未安装。

2026-10-21 的 [FP16 GDN 状态舍入候选](../docs/GDN_FP16_CONSISTENCY.md)及
`evidence/long-context-tool-loop/gdn-fp16-verification.json`记录新的record/fold数值一致性修复。
短测前后均9/10但失败seed不同；相同输入Op关系修复不能等同于总体工具选择改善。
非 Huihui GSQ-RCO IQ3_XXS 同请求对照为日常5/10、候选7/10，仍复现误调用；逐项证据在
`evidence/long-context-tool-loop/short-model-comparison.json`。十seed共用一个任务，不能外推总体模型质量或仅凭换模型确定引擎根因。

后续[回答阶段惩罚诊断](../docs/SHORT_TASK_SAMPLING_DIAGNOSIS.md)及
`evidence/long-context-tool-loop/answer-penalty-diagnosis.json`记录同一 GDN 候选的参数配对：只把回答 presence 从1改为0，Huihui同题十seed由9/10变为10/10，非Huihui GSQ由7/10变为9/10，思考文本前后相同。
局部采样重算及完整计数核对吻合，本夹具官方/增强模板渲染一致；剩余一例原始logit已偏向工具，独立前向交叉验证尚未完成。
Huihui六类任务及工具步骤前后各12/12，98K组7/7；未覆盖长正文重复风险。本轮无生产源码修改，日常已恢复，默认惩罚不变。

随后实际验证[回答阶段独立惩罚计数候选](../docs/ANSWER_PENALTY_SCOPE.md)：思考闭合后清零计数，回答presence仍为1。Huihui同题十seed为10/10、GSQ为9/10，与取消回答惩罚相同；32个回答位置的完整计数和CPU采样核对通过。功能组Huihui12/12，GSQ三种策略均11/12，失败为同一引号抄写题；48函数代码、96行重复字段JSON、六篇新版正文结构及Huihui98K组7/7通过。正文仍有事实错误，原版字数目标题两策略均耗尽思考额度；边界诊断仍有误调用。证据与最小原型补丁见 `evidence/long-context-tool-loop/answer-scope-verification.json` 和 `answer-scope-prototype.patch`；正式源码未变、候选未安装，日常已恢复，不代表所有循环或总体质量已改善。

2026-10-21 [无探针日常试用安装记录](evidence/long-context-tool-loop/answer-scope-trial-installation.json)补充正式独立计数API、进程开关及本地安装状态：[试用说明](../docs/ANSWER_PENALTY_SCOPE.md)。相同无探针二进制开启组29/29、缺省短组9/10，准确对象frontend55组/parser39组通过；只读确认安装及服务，真实dsh隔离夹具在模型请求前因依赖映射启动失败，用户选择自行测试。7个重复/可再生exe已清理，源码、对象、原始证据和唯一最近回滚保留；没有继续推理测试或发布。
