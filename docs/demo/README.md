# 鹈鹕骑自行车：本机模型实际输出

![预览](../../output/playwright/pelican-preview.png)

- 提供者 / 本机维护者：**CSUNX233**。
- 文件：[`tihuriding.html`](tihuriding.html)，按维护者提供的原文件保存，没有修改画面或修正动画。
- 生成耗时：维护者报告 **约 7 分钟**；不是本次发布整理重新运行所得。
- 项目模型：Qwen3.8-27B GSQ-RCO IQ3_XXS + MTP，本地 NInfer / KVMem；目标机器为 RTX 5070 Ti Laptop 12GB。
- 推荐思考档位：**xhigh**；原运行的提示词、token 数、采样和完整日志未提供，不将建议档位当作该次运行已核验的参数。
- 页面由 SVG、CSS、JavaScript 构成，无外部依赖。下载 HTML 后双击即可运行。

预览图在发布整理时从原 HTML 的浏览器实际渲染截图得到。示例只展示一次实际生成结果，不是正式模型评测分数，也不能从 7 分钟总耗时推算 decode token/s。

特别感谢 Bilibili **0xFFFF7** 提供基础架构版本并解决此前死循环问题；引擎、KVMem、模型和量化的其他上游贡献见 [来源说明](../UPSTREAM_DOWNSTREAM.md)。
