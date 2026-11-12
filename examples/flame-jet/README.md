# 火焰射流 / Flame Jet

四层二维粒子效果：高速白黄火芯、扩散橙红火舌、速度拉伸火星和上飘烟雾。喷口向右略向上喷射，压力随时间起伏。

运行：`make run/macosx-debug GAME=examples/flame-jet`（其他平台替换平台名）。

`jet.effect.json` 可通过 `particles.newEffectFromFile` 复用；`setPosition(x, y)` 设置喷口，`setFloatParameter("intensity", value)` 调节排放强度。粒子使用固定随机种子和固定模拟步长；压力使用传入的 dt，不承诺跨后端逐像素一致。

参数：`speed` 控制射程，`spread` 控制张角，`particleLife` 控制存活时间，`colorOverLife` 控制燃烧渐变。所有资源在初始化时加载。

颜色使用加载器的 `colorOverLife` 字段；64×64 径向柔边纹理抑制方形粒子边界，降低核心排放避免叠加饱和。

## 2026-11-11 视觉改善

修正颜色字段为加载器识别的 colorOverLife，并添加由 generate_texture.py 生成的柔边透明纹理；降低火芯叠加强度。新增原生回归验证橙红渐变、末端透明度和纹理资源存在。

[当前实帧](../../docs/images/examples/review-2026-11-11/next10/flame-jet.png) · [前后对照与验证](../../docs/images/examples/QUALITY_REVIEW.md)
