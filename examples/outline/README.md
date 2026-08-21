# Outline — 屏幕空间描边实验室

基于 G-buffer 深度 + 法线的屏幕空间模型描边（t3ssel8r 风格）：
颜色、线宽、深度/法线阈值、柔和度都可在脚本顶部配置并热重载。

## 运行

```bash
make run/<platform>-debug GAME=examples/outline
```

## 操作

| 输入 | 作用 |
|---|---|
| `1` | 开关描边 |
| `[` / `]` | 减小 / 增大线宽 |
| `C` | 循环描边颜色（墨 / 纸 / 红 / 蓝） |

调整 `main.nut` 顶部的 `outline*` 变量保存，热重载即时生效。
