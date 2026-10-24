# Stylized Meadow / 三阶风格化草坪

本例使用 [默认 Stylized Grass style](../../assets/styles/stylized-grass/README.md)：CPU 矢量草叶 → 内存四帧遮罩 → CPU 蓝噪声根点 → GPU 实例化 → 同帧测光与三阶配色 → 地面补色。

**不携带草的图片、原始纹理、SPIR-V 或草根数据文件。** style 脚本与地形代码随游戏分发，CPU 在初始化时生成遮罩与实例数据；shader 代码内嵌在引擎中。需要本次引擎构建，旧 SDK 不支持。

```powershell
make run/win32-debug GAME=examples/stylized-meadow
```

本工作区验证构建：

```powershell
$env:PATH = "$PWD\build\meadow-native;$env:PATH"
.\build\meadow-native\src\engine\eve.exe run examples/stylized-meadow
```

- `V`：全景、近景、俯视。
- `Up / Down`：四档蓝噪声覆盖区间切换，无帧内重新生成。
- `Space`：暂停/继续；`A`：切换稀疏变色。

`config.nut` 的 `grassScatter` 可覆盖散点设置，例如 `grassScatter={coverageRadii=[0.1633,0.27,0.36,0.48], width=0.64, height=0.65}`。它与窗口 width/height 分开，避免把窗口尺寸当成草尺寸。默认最密档当前生成 27,618 簇。

`frameSeconds`、`swayPeriodMultipliers`、`darkThreshold`、`lightThreshold`、`darkGreen` / `green` / `youngGreen` 可在 config 中覆盖 style 默认值。四组默认周期为 3.2、4.0、4.8、5.6 秒。每簇只使用一种绿色，不烘焙草色。

参考地面独立测光，快照后再补画可见地面的三阶绿色，所以空隙保持草色且不改变草的分档；小径保留本色。稀疏点仍执行深绿→绿、嫩绿→绿、绿→深绿。

将 `verifyGrass=true` 可运行初始化契约检查：固定种子重建一致、蓝噪声最小间距、独立稀疏点间距、两种草型，以及非法矩阵和未知 pass 拒绝。成功日志为 `GRASS_CPU_CONTRACT_PASS`。默认关闭检查。

公共脚本维护源在 `assets/styles/stylized-grass/`；这里的 `styles/stylized-grass/` 是发布副本。脚本修改后用目录复制同步，不额外生成部署素材。原生 CPU 生成、所有权、成本及接入方式见公共 style 文档。
