# Waterfall — 瀑布流动着色器

垂直瀑布面片：向下流动条纹、湍流、顶部水唇泡沫与底部溅落池，
含颜色 / 反射强度 / 太阳强度等可调参数。

## 运行

```bash
make run/<platform>-debug GAME=examples/waterfall-demo
```

启动约 1.5 秒后自动把当前帧保存到 `/tmp/waterfall_demo.png`，也可按 `F` 随时保存。

## 2026-11-11 视觉改善

保留真实流动水幕 shader，新增低多边形岩壁、岸边植被、水潭与落水泡沫参照，并补全景和落水区域近景。场景参照为示例造景，不是新增水体物理求解。

[当前实帧](../../docs/images/examples/review-2026-11-11/next10/waterfall-demo.png) · [前后对照与验证](../../docs/images/examples/QUALITY_REVIEW.md)
