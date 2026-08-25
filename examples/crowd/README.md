# Crowd — 群体行为示例（流场寻路 + Boids）

演示 `crowd` 模块：2000 个单位在带障碍的流场中行军、平滑转向，
以及 Boids 鸟群（分离 / 对齐 / 聚合）。

## 运行

```bash
make run/<platform>-debug GAME=examples/crowd
# 例如 Windows：make run/win32-debug GAME=examples/crowd
```

## 操作

- `1`：流场行军（`flow`）——全场单位沿流场朝目标移动
- `2`：寻点（`seek`）——每单位独立寻点，带 arrive 减速
- `3`：鸟群（`boids`）——分离 + 对齐 + 聚合 + 目标偏置
- 鼠标左键：把目标点改到鼠标位置（flow 模式会重建流场）
- `空格`：暂停 / 继续

绘制说明：每个单位是一个小方块，颜色随朝向变化（方便观察转向），
白色小点表示单位头部方向。
