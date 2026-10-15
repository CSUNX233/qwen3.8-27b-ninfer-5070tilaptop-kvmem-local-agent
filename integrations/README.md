# dsh、搜索、浏览器与缓存续接

`setup.ps1` 在本目录的 `dsh/` 用固定 lockfile 安装 `@deepseek-ai/dsh@0.1.5-rc.3`；程序生成独立的 `runtime/dsh-home`，不接管已有 dsh 安装。仅对匹配版本应用分享包的 compaction / tool-result-pruner 修复，保留 DeepSeek MIT 许可和本地原文件备份。

`scripts/configure-dsh.mjs` 生成本地模型路由、256K 上下文、73728 最大输出、约 184320 压缩阈值及 32768 摘要上限。启动前同步安装路径，保留与本安装器无关的 patch 项。若修改模型/上下文，需同时核对输出预留和压缩策略；不能把逻辑上限当成实际可用输入长度。

`cache_guard.mjs` 保持已发送前缀稳定。默认关闭基于近似前缀比例的修复；保留上游“严格等价消息”以及特定工具裁剪标记的恢复逻辑。后者可能把明确由 pruner 裁短的内容恢复为原先已发送的全文，以换取缓存复用，受 210000-token 守卫上限控制。真正编辑、压缩、替换图片等应重新计算相应前缀；守卫不能让不同 token 前缀安全共享 KV。

`local-web/web-provider.mjs` 在本机访问公开 Bing 页面/RSS并提取网页；它依赖真实互联网和网页结构，不是 SearXNG 实例，不包含离线搜索索引。无付费 API。`local-web/browser-server.mjs` 为 stdio MCP，提供打开、快照、动作、evaluate、截图和状态工具；浏览器由本机 Edge 运行，按需启动、空闲退出。DOM 文本可供纯文字模型使用；截图保存到本地不表示纯文字模型能看图。

`local-agent/todo-completion.mjs` 在普通回合交付时仍有未完成待办的情况下追加至多一次提醒。由模型依据真实结果更新列表，补丁不自动把工作标为完成；计划模式、取消、失败与截断不触发强制收尾。

安装不包含女娲或人物技能；生成的 AGENTS 明确仅在用户点名时才允许启用女娲。浏览器核显偏好由用户在 Windows 中配置，脚本不改全局代理或 GPU 设置。

许可：dsh 及其派生 patch 文件为 MIT；新接入代码为 Apache-2.0；npm 依赖保留各自许可证。详情见根目录 NOTICE 与 `licenses/`。
