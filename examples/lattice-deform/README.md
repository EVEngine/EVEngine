# Lattice Deform — 3D 晶格缩放变形

演示 `anim` 模块的 `AnimLattice`：用 3×3×3 晶格包住 UV 球，做三种程序化 3D 变形动画——
整体 squash & stretch、局部鼓起（控制点 scale + offset 摆动）、按高度波浪。

## 运行

```bash
make run/<platform>-debug GAME=examples/lattice-deform
```

## 操作

| 输入 | 作用 |
|---|---|
| `1` / `2` / `3` | 切换三种变形模式 |
| `Space` | 暂停 / 继续动画 |
| `R` | 重置晶格 |
