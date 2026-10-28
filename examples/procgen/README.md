# Procgen — 程序化地牢与纹理

运行时程序化生成演示：BSP / Cellular / Drunkard / Maze / 噪声地形 / WFC 六种地图算法，
以及土壤 / 石材 / 大理石 / 水面 / 云层等纹理配方与法线预览。

## 运行

```bash
make run/<platform>-debug GAME=examples/procgen
```

## 操作

| 输入 | 作用 |
|---|---|
| `R` | 换种子重新生成 |
| `1`–`6` | BSP / Cellular / Drunkard / Maze / 地形 / WFC |
| `T` | 循环纹理配方 |
| `N` | 开关法线预览 |
| `-` / `=` | 纹理种子减 / 增 |
