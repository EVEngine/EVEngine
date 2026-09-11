# Building 放置示例

演示 `eve.Building()`：JSON 建筑定义、地形约束、道路邻接、鬼影预览、旋转与拆除。

```bash
make run/<platform>-debug GAME=examples/building
# 或
make building
```

## 素材与画面

这是 **top-down 俯视方格** 示例；`building-tilemap` 是另一套等距投影示例。

使用本地 Cainos **Pixel Art Top Down - Basic**：8 种草地、6 种石板路切片，按格子坐标稳定混合；石屋由原包的石墙、石地面和木门组合，摊位、仓储箱与码头也使用原包切片。水面、动态波纹与岸线由示例代码绘制，不冒充原包素材。开场通过正常放置校验生成 13 个建筑，重置恢复相同场景。

素材 PNG 不进入源码仓库。在已安装 Pillow 的 Python 环境执行一次：

```powershell
python examples/building/prepare_assets.py "你的路径/Pixel Art Top Down - Basic.unitypackage"
```

提取程序保留像素边缘，不需要 Unity 编辑器。`assets/provenance.json` 记录来源；缺失素材会给出明确错误。`town_art.nut` 只负责显示，地形和占用始终来自 `PlacementWorld`，GPU 贴图在脚本重载时重新加载。

## 操作

| 输入 | 作用 |
|------|------|
| 右侧按钮或 `1`–`5` | 选择建筑（路 / 石屋 / 摊位 / 码头 / L 形仓） |
| 鼠标移动 | 鬼影跟随并吸附到格子 |
| `R` | 旋转鬼影（90°） |
| 左键 | 确认放置（校验通过且金币足够） |
| 右键 | 拆除指针下建筑 |
| `T` | 显示或隐藏网格 |
| `Space` | 重置地图与金币 |

右侧建造面板也提供旋转和重置按钮。地图输入仅在网格区域生效，点击面板不会穿透到地图。

## 演示要点

- **石屋**：2×2，陆地，花费木材/金币
- **摊位**：必须邻接带 `road` 标签的建筑
- **码头**：只能建在水域格子上
- **L 仓**：不规则 `footprintMask`
- 鬼影绿色 = 可放，红色 = 失败（HUD 显示 reason）
