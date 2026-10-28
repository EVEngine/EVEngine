# Tensor — 张量编译管线

演示 `eve.TF()` 的 AITemplate 风格编译管线，一个窗口展示四类负载：

1. 游戏 AI 策略网络（matmul + bias + relu + softmax，编译一次反复执行）；
2. 地形高度场平滑（conv2d）；
3. 语音风格注意力（fused SDPA）；
4. 批量模拟更新（广播算术）。

## 运行

```bash
make run/<platform>-debug GAME=examples/tensor
```

纯计算示例，无绘制内容；结果打印到 stdout（策略每 60 帧输出一次动作概率）。
