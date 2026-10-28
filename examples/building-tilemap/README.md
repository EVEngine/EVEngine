# C14 等距地表与建筑放置

使用用户提供的 **C14 45° 地表素材**，保留 `PlacementWorld.bindTileLayer`、GID 地形语义和 `PlacementSession` 放置校验。

## 准备与运行

原目录有 120 张透明 PNG（0–120，缺少 66），全部编入图集。原始画布为 800×600，透明边裁掉后菱形宽高比约 1.41:1，因此地图采用 **64×46** 投影，视觉 pivot 在上顶点。不要直接使用原示例的 64×32 比例。

```powershell
# 需要 Pillow；原素材不修改，运行资源留在本地 assets/。
python examples/building-tilemap/prepare_assets.py "path/to/C14 45°地表素材"
make run/win32-debug GAME=examples/building-tilemap
```

`assets/provenance.json` 记录每个 GID 的原文件和处理方式；生成的 `catalog.nut` 显式映射文件编号，避免漏号导致地表错位。选择第三方素材模式前必须完成提取；第三方 PNG 与提取输出不提交源码仓库。

## 操作

- `1`–`6` 或面板选择草地、石板路、鹅卵石、沙地、浅水、深水；左键拖动铺设。
- `V` 或按钮切换同类材质。每格使用稳定的镜像变体，不每帧重新随机。
- 点击房屋、码头、塔楼按钮进入建筑模式；`B` 回到当前建筑，左键单击放置。
- `R` 旋转码头；右键拆除；`T` 显示网格。
- `N` 或重置按钮恢复两栋示范建筑，并生成新的地表样式分布。

C14 提供地表；石基木屋（2×2）和石质哨塔（1×1）由内置 image_gen 生成透明原图，位于 `art/cottage-master.png` 与 `art/tower-master.png`，完整提示词保存在 `art/prompts.json`。`art/pack.py` 只裁掉透明边、缩放和打包；`art/buildings.png` 是实际运行图集。码头保留为代码绘制的木板示意图。

建筑 TileLayer 按脚点深度排序；已放置建筑与半透明预览共用区域与 pivot。PlacementWorld 是建筑占用的唯一来源，显示层每帧从它重建，不维护第二份建筑状态。BuildingFx 在这里仅用于网格辅助线，不再创建占位色块。启动时会放置一间木屋和一座哨塔，直接展示实际画面。

另一个 `examples/building` 是使用 Cainos 的俯视方格城镇。

## 数据与验证

- TileLayer 是地形 GID 的唯一来源，PlacementWorld 懒解析其陆地/水域语义。
- 占用格禁止刷地形；先拆除后才允许修改。房屋只允许陆地，码头只允许水域。
- 等距拾取必须把 X、Y 一起传给 `layer.worldToTileX/Y`，不能使用只接收单轴的正交便捷方法。
- 重置先销毁 session、从旧 world 分离 FX，再销毁 world；地图 layer 重用，避免重置时累积隐藏图层。
- 已用真实 Vulkan 运行与引擎截图验证，并通过全部 168 个格子中心投影回查、房屋放置、占用格地形保护、移除后刷水、房屋禁水、码头水域放置及连续三次重置检查。

生成建筑已通过真实 Vulkan 截图与运行检查：放置、重叠拒绝、拆除后显示清理、房屋禁水、连续三次重置以及无残留 BuildingFx 占位模型。

## 干净检出运行

默认 `config.assetSource = "starter"` 使用仓库自带的程序绘制基础素材，无需外部资源即可启动。运行第三方素材提取脚本后，将其改为 `"assets"` 使用完整素材。基础素材可用 `python scripts/generate_building_starter_assets.py`（需要 Pillow）重新生成。
