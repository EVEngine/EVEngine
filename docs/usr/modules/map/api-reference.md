# map：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Fov

- `getBlockEmpty()` → `bool`；[绑定实现 L516](../../../../src/modules/map/Map.cpp#L516)。
- `getCliffBlock()` → `float`；[绑定实现 L525](../../../../src/modules/map/Map.cpp#L525)。
- `getCornerPeek()` → `bool`；[绑定实现 L511](../../../../src/modules/map/Map.cpp#L511)。
- `getDetectionMargin()` → `float`；[绑定实现 L546](../../../../src/modules/map/Map.cpp#L546)。
- `getEyeOffset()` → `float`；[绑定实现 L527](../../../../src/modules/map/Map.cpp#L527)。
- `getPerceptionRadiusScale()` → `float`；[绑定实现 L544](../../../../src/modules/map/Map.cpp#L544)。
- `getVerticalRange()` → `int`；[绑定实现 L529](../../../../src/modules/map/Map.cpp#L529)。

## Pathfinder

- `getBlockEmpty()` → `bool`；[绑定实现 L485](../../../../src/modules/map/Map.cpp#L485)。
- `getDiagonal()` → `bool`；[绑定实现 L480](../../../../src/modules/map/Map.cpp#L480)。

## TileLayer

- `clearTileVisuals()` → `void`；[绑定实现 L428](../../../../src/modules/map/Map.cpp#L428)。
- `depthYAt(tx: int, ty: int)` → `float`；[绑定实现 L453](../../../../src/modules/map/Map.cpp#L453)。
- `getAutoReload()` → `bool`；[绑定实现 L449](../../../../src/modules/map/Map.cpp#L449)。
- `getConfigPath()` → `string`；[绑定实现 L450](../../../../src/modules/map/Map.cpp#L450)。
- `getMapHeight()` → `int`；[绑定实现 L383](../../../../src/modules/map/Map.cpp#L383)。
- `getMapWidth()` → `int`；[绑定实现 L382](../../../../src/modules/map/Map.cpp#L382)。
- `getReceiveLight()` → `bool`；[绑定实现 L443](../../../../src/modules/map/Map.cpp#L443)。
- `getTileHeight()` → `float`；[绑定实现 L385](../../../../src/modules/map/Map.cpp#L385)。
- `getTileVisualCount()` → `int`；[绑定实现 L429](../../../../src/modules/map/Map.cpp#L429)。
- `getTileWidth()` → `float`；[绑定实现 L384](../../../../src/modules/map/Map.cpp#L384)。
- `getTilesetColumns()` → `int`；[绑定实现 L435](../../../../src/modules/map/Map.cpp#L435)。
- `getTilesetFirstGid()` → `int`；[绑定实现 L434](../../../../src/modules/map/Map.cpp#L434)。
- `getTilesetTexture()` → `Texture`；[绑定实现 L433](../../../../src/modules/map/Map.cpp#L433)。
- `getVisualScaleX()` → `float`；[绑定实现 L394](../../../../src/modules/map/Map.cpp#L394)。
- `getVisualScaleY()` → `float`；[绑定实现 L395](../../../../src/modules/map/Map.cpp#L395)。
- `loadConfig(path: string)` → `bool`；[绑定实现 L446](../../../../src/modules/map/Map.cpp#L446)。
- `reloadConfig()` → `bool`；[绑定实现 L447](../../../../src/modules/map/Map.cpp#L447)。
- `setAutoReload(enable: bool)` → `void`；[绑定实现 L448](../../../../src/modules/map/Map.cpp#L448)。
- `setCamera(camera: Camera2D)` → `void`；[绑定实现 L437](../../../../src/modules/map/Map.cpp#L437)。
- `setCanvas(canvas: Canvas)` → `void`；[绑定实现 L436](../../../../src/modules/map/Map.cpp#L436)。
- `setLayer(layer: int)` → `void`；[绑定实现 L438](../../../../src/modules/map/Map.cpp#L438)。
- `setOrigin(x: float, y: float)` → `void`；[绑定实现 L379](../../../../src/modules/map/Map.cpp#L379)。
- `setReceiveLight(receive: bool)` → `void`；[绑定实现 L442](../../../../src/modules/map/Map.cpp#L442)。
- `setTileSize(tileW: float, tileH: float)` → `void`；[绑定实现 L386](../../../../src/modules/map/Map.cpp#L386)。
- `setTileVisual(gid: int, x: int, y: int, width: int, height: int, pivotX: float, pivotY: float, sortBias: float)` → `void`；[绑定实现 L427](../../../../src/modules/map/Map.cpp#L427)。
- `setTilesetTileSize(tileW: int, tileH: int)` → `void`；[绑定实现 L426](../../../../src/modules/map/Map.cpp#L426)。
- `setTint(r: float, g: float, b: float, a: float)` → `void`；[绑定实现 L444](../../../../src/modules/map/Map.cpp#L444)。
- `setVisualScale(scaleX: float, scaleY: float)` → `void`；[绑定实现 L393](../../../../src/modules/map/Map.cpp#L393)。
- `tileToWorldX(tx: int, ty: int)` → `float`；[绑定实现 L451](../../../../src/modules/map/Map.cpp#L451)。
- `tileToWorldY(tx: int, ty: int)` → `float`；[绑定实现 L452](../../../../src/modules/map/Map.cpp#L452)。
- `worldToTileX(wx: float, wy: float)` → `int`；[绑定实现 L454](../../../../src/modules/map/Map.cpp#L454)。
- `worldToTileY(wx: float, wy: float)` → `int`；[绑定实现 L455](../../../../src/modules/map/Map.cpp#L455)。

## 接收者见绑定源码

- `dualGridFrame(mask: int)` → `int`；[绑定实现 L638](../../../../src/modules/map/Map.cpp#L638)。
- `dualGridMaskAt(logic: TileLayer, dx: int, dy: int, filledGid: int)` → `int`；[绑定实现 L637](../../../../src/modules/map/Map.cpp#L637)。
- `getCollisionRectCount()` → `int`；[绑定实现 L601](../../../../src/modules/map/Map.cpp#L601)。
- `getLastAtlasCount()` → `int`；[绑定实现 L597](../../../../src/modules/map/Map.cpp#L597)。
- `getLastCustomVisualCount()` → `int`；[绑定实现 L596](../../../../src/modules/map/Map.cpp#L596)。
- `getLastVisibleTileCount()` → `int`；[绑定实现 L595](../../../../src/modules/map/Map.cpp#L595)。
- `getLayerCount()` → `int`；[绑定实现 L606](../../../../src/modules/map/Map.cpp#L606)。
- `getName()` → `string`；[绑定实现 L571](../../../../src/modules/map/Map.cpp#L571)。
- `getObjectCount()` → `int`；[绑定实现 L608](../../../../src/modules/map/Map.cpp#L608)。
- `getObjectGid(i: int)` → `int`；[绑定实现 L615](../../../../src/modules/map/Map.cpp#L615)。
- `getObjectHeight(i: int)` → `float`；[绑定实现 L614](../../../../src/modules/map/Map.cpp#L614)。
- `getObjectName(i: int)` → `string`；[绑定实现 L609](../../../../src/modules/map/Map.cpp#L609)。
- `getObjectType(i: int)` → `string`；[绑定实现 L610](../../../../src/modules/map/Map.cpp#L610)。
- `getObjectWidth(i: int)` → `float`；[绑定实现 L613](../../../../src/modules/map/Map.cpp#L613)。
- `getObjectX(i: int)` → `float`；[绑定实现 L611](../../../../src/modules/map/Map.cpp#L611)。
- `getObjectY(i: int)` → `float`；[绑定实现 L612](../../../../src/modules/map/Map.cpp#L612)。
- `lastDualGridError()` → `string`；[绑定实现 L641](../../../../src/modules/map/Map.cpp#L641)。
- `pollConfigs()` → `int`；[绑定实现 L594](../../../../src/modules/map/Map.cpp#L594)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
