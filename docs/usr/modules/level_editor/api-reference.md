# level_editor：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Brush

- `clearStamp()` → `void`；[绑定实现 L69](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L69)。
- `eraseAt(buffer: TileBuffer, tx: int, ty: int)` → `int`；[绑定实现 L71](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L71)。
- `floodFill(buffer: TileBuffer, tx: int, ty: int)` → `int`；[绑定实现 L72](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L72)。
- `getChangeCount()` → `int`；[绑定实现 L82](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L82)。
- `getChangeNewGid(index: int)` → `int`；[绑定实现 L86](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L86)。
- `getChangeOldGid(index: int)` → `int`；[绑定实现 L85](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L85)。
- `getChangeX(index: int)` → `int`；[绑定实现 L83](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L83)。
- `getChangeY(index: int)` → `int`；[绑定实现 L84](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L84)。
- `getEraseTile()` → `int`；[绑定实现 L63](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L63)。
- `getPreviewCount()` → `int`；[绑定实现 L78](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L78)。
- `getPreviewGid(index: int)` → `int`；[绑定实现 L81](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L81)。
- `getPreviewX(index: int)` → `int`；[绑定实现 L79](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L79)。
- `getPreviewY(index: int)` → `int`；[绑定实现 L80](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L80)。
- `getShape()` → `string`；[绑定实现 L59](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L59)。
- `getSize()` → `int`；[绑定实现 L57](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L57)。
- `getStampHeight()` → `int`；[绑定实现 L66](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L66)。
- `getStampTile(lx: int, ly: int)` → `int`；[绑定实现 L68](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L68)。
- `getStampWidth()` → `int`；[绑定实现 L65](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L65)。
- `getTile()` → `int`；[绑定实现 L61](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L61)。
- `getTool()` → `string`；[绑定实现 L55](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L55)。
- `paintAt(buffer: TileBuffer, tx: int, ty: int)` → `int`；[绑定实现 L70](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L70)。
- `paintLine(buffer: TileBuffer, x0: int, y0: int, x1: int, y1: int)` → `int`；[绑定实现 L73](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L73)。
- `paintRect(buffer: TileBuffer, x0: int, y0: int, x1: int, y1: int, filled: bool)` → `int`；[绑定实现 L74](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L74)。
- `previewAt(buffer: TileBuffer, tx: int, ty: int)` → `int`；[绑定实现 L75](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L75)。
- `previewLine(buffer: TileBuffer, x0: int, y0: int, x1: int, y1: int)` → `int`；[绑定实现 L76](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L76)。
- `previewRect(buffer: TileBuffer, x0: int, y0: int, x1: int, y1: int, filled: bool)` → `int`；[绑定实现 L77](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L77)。
- `setEraseTile(gid: int)` → `void`；[绑定实现 L62](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L62)。
- `setShape(shape: string)` → `void`；[绑定实现 L58](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L58)。
- `setSize(size: int)` → `void`；[绑定实现 L56](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L56)。
- `setStampSize(width: int, height: int)` → `void`；[绑定实现 L64](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L64)。
- `setStampTile(lx: int, ly: int, gid: int)` → `void`；[绑定实现 L67](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L67)。
- `setTile(gid: int)` → `void`；[绑定实现 L60](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L60)。
- `setTool(tool: string)` → `void`；[绑定实现 L54](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L54)。

## EditorHistory

- `applyLastToBuffer(buffer: TileBuffer)` → `bool`；[绑定实现 L102](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L102)。
- `beginGroup(name: string)` → `void`；[绑定实现 L92](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L92)。
- `canRedo()` → `bool`；[绑定实现 L97](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L97)。
- `canUndo()` → `bool`；[绑定实现 L96](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L96)。
- `clear()` → `void`；[绑定实现 L90](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L90)。
- `endGroup()` → `void`；[绑定实现 L94](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L94)。
- `getLastActionKind()` → `string`；[绑定实现 L104](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L104)。
- `getLastActionName()` → `string`；[绑定实现 L103](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L103)。
- `getLastPayload()` → `string`；[绑定实现 L105](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L105)。
- `getLastTileCount()` → `int`；[绑定实现 L106](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L106)。
- `getLastTileNewGid(index: int)` → `int`；[绑定实现 L110](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L110)。
- `getLastTileOldGid(index: int)` → `int`；[绑定实现 L109](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L109)。
- `getLastTileX(index: int)` → `int`；[绑定实现 L107](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L107)。
- `getLastTileY(index: int)` → `int`；[绑定实现 L108](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L108)。
- `getRedoCount()` → `int`；[绑定实现 L99](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L99)。
- `getUndoCount()` → `int`；[绑定实现 L98](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L98)。
- `isGrouping()` → `bool`；[绑定实现 L95](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L95)。
- `push(name: string, payload: string)` → `void`；[绑定实现 L91](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L91)。
- `recordTile(x: int, y: int, oldGid: int, newGid: int)` → `void`；[绑定实现 L93](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L93)。
- `redo()` → `bool`；[绑定实现 L101](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L101)。
- `undo()` → `bool`；[绑定实现 L100](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L100)。

## TileBuffer

- `clear()` → `void`；[绑定实现 L46](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L46)。
- `fill(gid: int)` → `void`；[绑定实现 L47](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L47)。
- `getGid(x: int, y: int)` → `int`；[绑定实现 L49](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L49)。
- `getHeight()` → `int`；[绑定实现 L44](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L44)。
- `getWidth()` → `int`；[绑定实现 L43](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L43)。
- `inBounds(x: int, y: int)` → `bool`；[绑定实现 L50](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L50)。
- `resize(width: int, height: int)` → `void`；[绑定实现 L45](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L45)。
- `setGid(x: int, y: int, gid: int)` → `void`；[绑定实现 L48](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L48)。

## TileBufferTarget

- `clearDirtyRegion()` → `void`；[绑定实现 L124](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L124)。
- `getHeight()` → `int`；[绑定实现 L121](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L121)。
- `getRevision()` → `dynamic`；[绑定实现 L117](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L117)。
- `getTargetId()` → `dynamic`；[绑定实现 L114](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L114)。
- `getWidth()` → `int`；[绑定实现 L120](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L120)。
- `readInt(x: int, y: int)` → `int`；[绑定实现 L122](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L122)。
- `writeInt(x: int, y: int, value: int)` → `FieldWriteStatus`；[绑定实现 L123](../../../../src/modules/map/level/editor/LevelEditorModule.cpp#L123)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
