# HD2D 河畔场景

22×16 河畔地图、三名像素角色，以及使用引擎内部 HouseGen + Procgen 生成的房屋。

```sh
make run/win32-debug GAME=examples/hd2d-riverside
```

也可使用已安装的 SDK：`eve run examples/hd2d-riverside`。
WASD/方向键移动，1/2/3 选择角色，Q/E 环绕相机，V 分步旋转，4/5/6/7 切换四种房屋。
角色占据候选房屋占地时，切换会被拒绝。

## 房屋生成

编辑 `house-settings.nut` 中的尺寸、层数、屋顶、配色和种子。当前外观配方支持矩形布局，
宽 3–10 格、深 3–4 格、1–3 层，以及 gable/shed/flat 屋顶。hip 和不规则平面尚未实现。
实际尺寸还必须落在地图预留地块内；当前地图可容纳 6×3 格。

`house-native.nut` 消费内部 HouseGen 的 HouseLayout 组件实例，复用原有门窗选择规则。
几何使用内部 Procgen cube/ramp/stairs 配方，木纹使用内部纹理配方；EveScript 只组织外观与材质。
无需 Python、OBJ 导入或离线房屋生成。旧的房屋 Python 脚本和 OBJ/MTL/ZIP 已移除。

HouseLayout 是布局和碰撞的共同来源。地图 raised 层 GID 10 仅标记允许建造的地块，
较小房屋腾出的格子可以通行；GID 9 高台继续阻挡。四个候选在初始化时生成并按材质合并，
切换只改变可见性和当前布局，不在每帧重建网格。种子确定布局及屋瓦色差。

组件库与许可证见 `assets/house/README.md`。本次没有新增 C++ 生成器或模块。

## 验证

启动调试场景后，通过引擎 MCP 的 `eve_eval` 执行以下 QA 入口（游戏本身不依赖 eval）：

```squirrel
(function(){ dofile("verify.nut"); return verifyRiverside(); })()
(function(){ dofile("verify-house.nut"); return verifyNativeHouses(); })()
```

检查覆盖场景、四种布局、门窗间距、网格索引/法线/退化三角形、固定种子重复性、
HouseLayout JSON 往返、碰撞映射和无效参数拒绝。实景截图在 `qa/house-native-*.png`，
通过正常按键事件切换后由引擎截图接口保存。

## 地图与角色素材

`map-data.nut` 与 `assets/riverside.json` 为已有地图数据；`build_assets.py` 仍是可选的地图维护工具。
角色原图、提示词及来源记录保留在各自资产目录。地图/角色工具不参与房屋生成，运行场景无需 Python。

## 2026-11-12 画面改善

补河岸石块与芦苇、桥栏、树木、长椅和广场边缘装饰，调低曝光/景深/泛光；树木与长椅的同一 authored 位置也供行走阻挡使用，保留原角色移动、房屋生成和旋转观察。

[当前实帧](../../docs/images/examples/review-2026-11-12/nine-improved/hd2d-riverside.png) · [前后与状态对照](../../docs/images/examples/review-2026-11-12/nine-improved/README.md)
