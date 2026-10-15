# 测试数据与复现

完整方法、硬件和解释见 [实测报告](../docs/BENCHMARKS.md)。`evidence/` 保留各阶段摘要和源码 patch；这些是阶段性证据，不能把不同版本、workload 和日期相加成总提速。

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
