# 火焰射流 / Flame Jet

四层二维粒子效果：高速白黄火芯、扩散橙红火舌、速度拉伸火星和上飘烟雾。喷口向右略向上喷射，压力随时间起伏。

运行：`make run/macosx-debug GAME=examples/flame-jet`（其他平台替换平台名）。

`jet.effect.json` 可通过 `particles.newEffectFromFile` 复用；`setPosition(x, y)` 设置喷口，`setFloatParameter("intensity", value)` 调节排放强度。粒子使用固定随机种子和固定模拟步长；压力使用传入的 dt，不承诺跨后端逐像素一致。

参数：`speed` 控制射程，`spread` 控制张角，`particleLife` 控制存活时间，`colorOverLifetime` 控制燃烧渐变。所有资源在初始化时加载。
