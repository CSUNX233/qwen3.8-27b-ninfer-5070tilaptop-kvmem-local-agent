# dsh、搜索、浏览器与缓存续接

首次部署、排查变慢或工具故障时，先看 [部署与维护交接手册](../docs/DEPLOYMENT_AGENT_GUIDE.md)，按引擎 → 守卫 → dsh → 工具逐层验收。

`setup.ps1` 在本目录的 `dsh/` 用固定 lockfile 安装 `@deepseek-ai/dsh@0.1.5-rc.3`；程序生成独立的 `runtime/dsh-home`，不接管已有 dsh 安装。仅对匹配版本应用分享包的 compaction / tool-result-pruner 修复，保留 DeepSeek MIT 许可和本地原文件备份。

`scripts/configure-dsh.mjs` 生成本地模型路由、256K 上下文、73728 最大输出、约 184320 压缩阈值及 32768 摘要上限。启动前同步安装路径，保留与本安装器无关的 patch 项。若修改模型/上下文，需同时核对输出预留和压缩策略；不能把逻辑上限当成实际可用输入长度。

`cache_guard.mjs` 保持已发送前缀稳定。默认关闭基于近似前缀比例的修复；保留上游“严格等价消息”以及特定工具裁剪标记的恢复逻辑。后者可能把明确由 pruner 裁短的内容恢复为原先已发送的全文，以换取缓存复用，受 210000-token 守卫上限控制。真正编辑、压缩、替换图片等应重新计算相应前缀；守卫不能让不同 token 前缀安全共享 KV。

客户端断开未完成的请求时，守卫会关闭对应上游连接，覆盖排队、等待首包和流式生成阶段；正常完成不触发取消。上游流中途失败时终止连接，避免继续等待或在 SSE 后拼入 JSON 错误。引擎仍须实际响应断连，守卫不直接终止进程。

`local-web/web-provider.mjs` 在本机访问公开 Bing 页面/RSS并提取网页；它依赖真实互联网和网页结构，不是 SearXNG 实例，不包含离线搜索索引。无付费 API。`local-web/browser-server.mjs` 为 stdio MCP，提供打开、快照、动作、evaluate、截图和状态工具；浏览器由本机 Edge 运行，按需启动、空闲退出。DOM 文本可供纯文字模型使用；截图保存到本地不表示纯文字模型能看图。

`local-agent/todo-completion.mjs` 在普通回合交付时仍有未完成待办的情况下追加至多一次提醒。由模型依据真实结果更新列表，补丁不自动把工作标为完成；计划模式、取消、失败与截断不触发强制收尾。

## 工具验证与分步执行

`local-agent/tool-grounded-execution.mjs` 为 `ninfer-local` Agent 注册固定系统规则：下一步明确时先读取、实现或测试，以新证据继续推理；从可验证的小步推进到完整交付。复杂算法、架构、相互约束和风险仍应充分思考，验证、计划模式和授权规则继续适用。其他 preset/provider 和没有 Agent 的辅助请求不加入这段规则。

**保留 xhigh、采样参数和 73728 最大输出额度。** 插件不设置思考长度上限、不强制工具调用、不截断生成，也不额外发起模型请求。它是行为引导，不能保证所有任务收敛或质量完全等价。固定规则在本机 count-only 接口测得增加 **272 个提示 token**；已有会话首次换用新系统提示可能重算发生变化的前缀，后续工具轮次文本保持一致，不引入动态计数提醒。插件不分配 GPU buffer，模型仍需处理新增提示 token。

安装和每次启动通过 `scripts/configure-dsh.mjs` 自动接入，模块 URL 带内容哈希，重复生成不会重复注册。默认启用；要**持久停用**，在本安装的 `config/local.json` 中加入或修改 `"ToolGroundedExecution": false`，再执行 `node .\scripts\configure-dsh.mjs`，或下次启动时自动应用。网页 profile 支持 live patch reload。改回 `true` 可恢复；旧配置缺少此字段时默认启用。不要仅删除生成的 patch 项，下一次启动会重新生成。

真实 dsh 隔离开发任务中，两种条件都通过 7 项验收，实际规则只在启用轮进入请求；还检查了网页/命令行识别、其他路由隔离、固定文本及卸载重载。每种条件只跑一轮，首次写文件反而更晚，不据此宣称普遍提速或质量不变。[完整测量边界与数据](../docs/BENCHMARKS.md#dsh-工具验证执行规则2026-10-15)。

后续补充：成功执行 `mcp__browser__browser_screenshot` 且仅返回截图路径时，在工具结果**尾部追加一次验收续接信息**，带上当前轮最近成功修改的文件定位；原系统规则保持逐字相同，不改旧消息。提醒区分“截图已保存”和“已看图验证”，要求从当前进度继续，存在具体失败证据或新需求时仍可重新修改。失败、取消、计划模式、其他模型 preset 及已有图像结果不添加这条信息，也不自动宣布任务完成。每次匹配的截图增加少量提示 tokens，不新增模型请求、不限制思考深度；无法保证所有长任务都不再重新规划。

HTTP 回归测试无需 GPU 或 npm 依赖；完整 dsh 流水线测试需先完成本目录 dsh 安装：

```powershell
node --test integrations/tests/cache-guard.test.mjs
node --test integrations/tests/tool-grounded-execution.test.mjs
```

测试覆盖断连传播、正常/异常流、既有消息前缀、真实 Cordis/Tools 提醒接入及卸载重载。截图后原长任务的行为与选择性 KV 召回质量仍需单独验证，不能将离线流水线检查当作长上下文质量证明。

安装不包含女娲或人物技能；生成的 AGENTS 明确仅在用户点名时才允许启用女娲。浏览器核显偏好由用户在 Windows 中配置，脚本不改全局代理或 GPU 设置。

许可：dsh 及其派生 patch 文件为 MIT；新接入代码为 Apache-2.0；npm 依赖保留各自许可证。详情见根目录 NOTICE 与 `licenses/`。
