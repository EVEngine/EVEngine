# building：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Ghost

- `getBuildingId()` → `string`；[绑定实现 L330](../../../../src/modules/building/Building.cpp#L330)。
- `getCellX()` → `int`；[绑定实现 L332](../../../../src/modules/building/Building.cpp#L332)。
- `getCellY()` → `int`；[绑定实现 L333](../../../../src/modules/building/Building.cpp#L333)。
- `getElevation()` → `float`；[绑定实现 L338](../../../../src/modules/building/Building.cpp#L338)。
- `getRotationDeg()` → `float`；[绑定实现 L340](../../../../src/modules/building/Building.cpp#L340)。
- `getWorldX()` → `float`；[绑定实现 L335](../../../../src/modules/building/Building.cpp#L335)。
- `getWorldY()` → `float`；[绑定实现 L336](../../../../src/modules/building/Building.cpp#L336)。
- `setElevation(elevation: float)` → `void`；[绑定实现 L339](../../../../src/modules/building/Building.cpp#L339)。
- `setWorld(worldX: float, worldY: float)` → `void`；[绑定实现 L337](../../../../src/modules/building/Building.cpp#L337)。

## PlacementSession

- `getBuildingId()` → `string`；[绑定实现 L375](../../../../src/modules/building/Building.cpp#L375)。
- `getGhost()` → `Ghost`；[绑定实现 L376](../../../../src/modules/building/Building.cpp#L376)。
- `getMode()` → `string`；[绑定实现 L378](../../../../src/modules/building/Building.cpp#L378)。
- `getRotationDeg()` → `float`；[绑定实现 L381](../../../../src/modules/building/Building.cpp#L381)。
- `stopPlacement()` → `void`；[绑定实现 L373](../../../../src/modules/building/Building.cpp#L373)。

## PlacementWorld

- `cellToWorld3DX(cellX: int, cellY: int, elevation: float)` → `float`；[绑定实现 L250](../../../../src/modules/building/Building.cpp#L250)。
- `cellToWorld3DY(cellX: int, cellY: int, elevation: float)` → `float`；[绑定实现 L251](../../../../src/modules/building/Building.cpp#L251)。
- `cellToWorld3DZ(cellX: int, cellY: int, elevation: float)` → `float`；[绑定实现 L252](../../../../src/modules/building/Building.cpp#L252)。
- `cellToWorldX(cellX: int)` → `float`；[绑定实现 L248](../../../../src/modules/building/Building.cpp#L248)。
- `cellToWorldY(cellY: int)` → `float`；[绑定实现 L249](../../../../src/modules/building/Building.cpp#L249)。
- `clearTerrainGidMap()` → `void`；[绑定实现 L264](../../../../src/modules/building/Building.cpp#L264)。
- `clearTileLayer()` → `void`；[绑定实现 L261](../../../../src/modules/building/Building.cpp#L261)。
- `getBuildingCellX(instanceId: int)` → `int`；[绑定实现 L280](../../../../src/modules/building/Building.cpp#L280)。
- `getBuildingCellY(instanceId: int)` → `int`；[绑定实现 L281](../../../../src/modules/building/Building.cpp#L281)。
- `getBuildingChannel(instanceId: int)` → `string`；[绑定实现 L295](../../../../src/modules/building/Building.cpp#L295)。
- `getBuildingCount()` → `int`；[绑定实现 L277](../../../../src/modules/building/Building.cpp#L277)。
- `getBuildingElevation(instanceId: int)` → `float`；[绑定实现 L285](../../../../src/modules/building/Building.cpp#L285)。
- `getBuildingId(instanceId: int)` → `string`；[绑定实现 L279](../../../../src/modules/building/Building.cpp#L279)。
- `getBuildingInstanceAt(index: int)` → `int`；[绑定实现 L300](../../../../src/modules/building/Building.cpp#L300)。
- `getBuildingProp(instanceId: int, key: string, fallback: string)` → `string`；[绑定实现 L297](../../../../src/modules/building/Building.cpp#L297)。
- `getBuildingRotation(instanceId: int)` → `float`；[绑定实现 L296](../../../../src/modules/building/Building.cpp#L296)。
- `getBuildingWorldX(instanceId: int)` → `float`；[绑定实现 L282](../../../../src/modules/building/Building.cpp#L282)。
- `getBuildingWorldY(instanceId: int)` → `float`；[绑定实现 L283](../../../../src/modules/building/Building.cpp#L283)。
- `getBuildingWorldZ(instanceId: int)` → `float`；[绑定实现 L284](../../../../src/modules/building/Building.cpp#L284)。
- `getCellSize()` → `float`；[绑定实现 L222](../../../../src/modules/building/Building.cpp#L222)。
- `getExtra(key: string, fallback: string)` → `string`；[绑定实现 L245](../../../../src/modules/building/Building.cpp#L245)。
- `getGridLayoutName()` → `string`；[绑定实现 L236](../../../../src/modules/building/Building.cpp#L236)。
- `getGridPlaneName()` → `string`；[绑定实现 L238](../../../../src/modules/building/Building.cpp#L238)。
- `getOriginX()` → `float`；[绑定实现 L224](../../../../src/modules/building/Building.cpp#L224)。
- `getOriginY()` → `float`；[绑定实现 L225](../../../../src/modules/building/Building.cpp#L225)。
- `getSnapMode()` → `string`；[绑定实现 L231](../../../../src/modules/building/Building.cpp#L231)。
- `getTileLayer()` → `TileLayer`；[绑定实现 L260](../../../../src/modules/building/Building.cpp#L260)。
- `getValidateRule()` → `string`；[绑定实现 L233](../../../../src/modules/building/Building.cpp#L233)。
- `hasGridFromLayer()` → `bool`；[绑定实现 L243](../../../../src/modules/building/Building.cpp#L243)。
- `inBounds(cellX: int, cellY: int)` → `bool`；[绑定实现 L258](../../../../src/modules/building/Building.cpp#L258)。
- `setBuildingProp(instanceId: int, key: string, value: string)` → `void`；[绑定实现 L298](../../../../src/modules/building/Building.cpp#L298)。
- `setExtra(key: string, value: string)` → `void`；[绑定实现 L244](../../../../src/modules/building/Building.cpp#L244)。
- `setGridFromLayer(layer: TileLayer)` → `void`；[绑定实现 L242](../../../../src/modules/building/Building.cpp#L242)。
- `setSnapMode(mode: string)` → `void`；[绑定实现 L232](../../../../src/modules/building/Building.cpp#L232)。
- `setValidateRule(rule: string)` → `void`；[绑定实现 L234](../../../../src/modules/building/Building.cpp#L234)。
- `worldToCell3DX(worldX: float, worldY: float, worldZ: float)` → `int`；[绑定实现 L253](../../../../src/modules/building/Building.cpp#L253)。
- `worldToCell3DY(worldX: float, worldY: float, worldZ: float)` → `int`；[绑定实现 L254](../../../../src/modules/building/Building.cpp#L254)。
- `worldToCellX(worldX: float)` → `int`；[绑定实现 L246](../../../../src/modules/building/Building.cpp#L246)。
- `worldToCellY(worldY: float)` → `int`；[绑定实现 L247](../../../../src/modules/building/Building.cpp#L247)。

## 接收者见绑定源码

- `getBuildingCategory(buildingId: string)` → `string`；[绑定实现 L442](../../../../src/modules/building/Building.cpp#L442)。
- `getBuildingChannel(buildingId: string)` → `string`；[绑定实现 L448](../../../../src/modules/building/Building.cpp#L448)。
- `getBuildingFootprintH(buildingId: string)` → `int`；[绑定实现 L444](../../../../src/modules/building/Building.cpp#L444)。
- `getBuildingFootprintW(buildingId: string)` → `int`；[绑定实现 L443](../../../../src/modules/building/Building.cpp#L443)。
- `getBuildingRenderMode(buildingId: string)` → `string`；[绑定实现 L449](../../../../src/modules/building/Building.cpp#L449)。
- `getBuildingRotationMode(buildingId: string)` → `string`；[绑定实现 L446](../../../../src/modules/building/Building.cpp#L446)。
- `getBuildingValidateRule(buildingId: string)` → `string`；[绑定实现 L447](../../../../src/modules/building/Building.cpp#L447)。
- `getBuildingVisual2d(buildingId: string, key: string, fallback: string)` → `string`；[绑定实现 L452](../../../../src/modules/building/Building.cpp#L452)。
- `getBuildingVisual3d(buildingId: string, key: string, fallback: string)` → `string`；[绑定实现 L453](../../../../src/modules/building/Building.cpp#L453)。
- `getChangeEventAction(index: int)` → `string`；[绑定实现 L469](../../../../src/modules/building/Building.cpp#L469)。
- `getChangeEventBuildingId(index: int)` → `string`；[绑定实现 L471](../../../../src/modules/building/Building.cpp#L471)。
- `getChangeEventCellX(index: int)` → `int`；[绑定实现 L473](../../../../src/modules/building/Building.cpp#L473)。
- `getChangeEventCellY(index: int)` → `int`；[绑定实现 L474](../../../../src/modules/building/Building.cpp#L474)。
- `getChangeEventCount()` → `int`；[绑定实现 L468](../../../../src/modules/building/Building.cpp#L468)。
- `getChangeEventInstanceId(index: int)` → `int`；[绑定实现 L472](../../../../src/modules/building/Building.cpp#L472)。
- `getChangeEventOtherCellX(index: int)` → `int`；[绑定实现 L475](../../../../src/modules/building/Building.cpp#L475)。
- `getChangeEventOtherCellY(index: int)` → `int`；[绑定实现 L476](../../../../src/modules/building/Building.cpp#L476)。
- `getChangeEventRotation(index: int)` → `float`；[绑定实现 L477](../../../../src/modules/building/Building.cpp#L477)。
- `getChangeEventWorldId(index: int)` → `string`；[绑定实现 L470](../../../../src/modules/building/Building.cpp#L470)。
- `getPlaneSurfaceHeight()` → `float`；[绑定实现 L466](../../../../src/modules/building/Building.cpp#L466)。
- `getSurfaceCount()` → `int`；[绑定实现 L463](../../../../src/modules/building/Building.cpp#L463)。
- `getSurfaceName(index: int)` → `string`；[绑定实现 L464](../../../../src/modules/building/Building.cpp#L464)。
- `hasSurface(name: string)` → `bool`；[绑定实现 L462](../../../../src/modules/building/Building.cpp#L462)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
