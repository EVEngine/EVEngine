# scene：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## SceneNodeRef

- `getPositionX()` → `float`；[绑定实现 L1247](../../../../src/modules/scene/Scene.cpp#L1247)。
- `getPositionY()` → `float`；[绑定实现 L1248](../../../../src/modules/scene/Scene.cpp#L1248)。
- `getPositionZ()` → `float`；[绑定实现 L1249](../../../../src/modules/scene/Scene.cpp#L1249)。
- `getRotationPitch()` → `float`；[绑定实现 L1253](../../../../src/modules/scene/Scene.cpp#L1253)。
- `getRotationRoll()` → `float`；[绑定实现 L1254](../../../../src/modules/scene/Scene.cpp#L1254)。
- `getRotationYaw()` → `float`；[绑定实现 L1252](../../../../src/modules/scene/Scene.cpp#L1252)。
- `getScaleX()` → `float`；[绑定实现 L1257](../../../../src/modules/scene/Scene.cpp#L1257)。
- `getScaleY()` → `float`；[绑定实现 L1258](../../../../src/modules/scene/Scene.cpp#L1258)。
- `getScaleZ()` → `float`；[绑定实现 L1259](../../../../src/modules/scene/Scene.cpp#L1259)。

## 接收者见绑定源码

- `addChildAt(hostName: string, parentId: string, childId: string)` → `bool`；[绑定实现 L1325](../../../../src/modules/scene/Scene.cpp#L1325)。
- `addNodeTagAt(hostName: string, nodeId: string, tag: string)` → `bool`；[绑定实现 L1330](../../../../src/modules/scene/Scene.cpp#L1330)。
- `collectFrustumIdsAt(hostName: string, cam: Camera3D, viewW: float, viewH: float)` → `string`；[绑定实现 L1350](../../../../src/modules/scene/Scene.cpp#L1350)。
- `collectIdsByTagAt(hostName: string, tag: string)` → `string`；[绑定实现 L1334](../../../../src/modules/scene/Scene.cpp#L1334)。
- `forEachEntity(hostName: string, nodeId: string, cb: dynamic)` → `dynamic`；[绑定实现 L1375](../../../../src/modules/scene/Scene.cpp#L1375)。
- `getNodeBoundsAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1345](../../../../src/modules/scene/Scene.cpp#L1345)。
- `getNodeLayerAt(hostName: string, nodeId: string)` → `int`；[绑定实现 L1336](../../../../src/modules/scene/Scene.cpp#L1336)。
- `getNodePositionAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1314](../../../../src/modules/scene/Scene.cpp#L1314)。
- `getNodeQuaternionAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1328](../../../../src/modules/scene/Scene.cpp#L1328)。
- `getNodeRefByPathAt(hostName: string, path: string)` → `SceneNodeRef`；[绑定实现 L1381](../../../../src/modules/scene/Scene.cpp#L1381)。
- `getNodeRotationAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1315](../../../../src/modules/scene/Scene.cpp#L1315)。
- `getNodeScaleAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1316](../../../../src/modules/scene/Scene.cpp#L1316)。
- `getNodeTagsAt(hostName: string, nodeId: string)` → `string`；[绑定实现 L1333](../../../../src/modules/scene/Scene.cpp#L1333)。
- `getNodeVisibleAt(hostName: string, nodeId: string)` → `bool`；[绑定实现 L1317](../../../../src/modules/scene/Scene.cpp#L1317)。
- `getNodeWorldPositionAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1318](../../../../src/modules/scene/Scene.cpp#L1318)。
- `getNodeWorldRotationAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1319](../../../../src/modules/scene/Scene.cpp#L1319)。
- `getNodeWorldScaleAt(hostName: string, nodeId: string)` → `Array<dynamic>`；[绑定实现 L1320](../../../../src/modules/scene/Scene.cpp#L1320)。
- `hasNodeBoundsAt(hostName: string, nodeId: string)` → `bool`；[绑定实现 L1344](../../../../src/modules/scene/Scene.cpp#L1344)。
- `hasNodeTagAt(hostName: string, nodeId: string, tag: string)` → `bool`；[绑定实现 L1332](../../../../src/modules/scene/Scene.cpp#L1332)。
- `linkAudio3DAt(hostName: string, nodeId: string, s: Source)` → `bool`；[绑定实现 L1308](../../../../src/modules/scene/Scene.cpp#L1308)。
- `linkCamera3DAt(hostName: string, nodeId: string, c: Camera3D)` → `bool`；[绑定实现 L1307](../../../../src/modules/scene/Scene.cpp#L1307)。
- `linkCountAt(hostName: string, nodeId: string)` → `int`；[绑定实现 L1311](../../../../src/modules/scene/Scene.cpp#L1311)。
- `linkPhysics2DAt(hostName: string, nodeId: string, b: Body, mode: string)` → `bool`；[绑定实现 L1305](../../../../src/modules/scene/Scene.cpp#L1305)。
- `linkPhysics3DAt(hostName: string, nodeId: string, b: Body3D, mode: string)` → `bool`；[绑定实现 L1306](../../../../src/modules/scene/Scene.cpp#L1306)。
- `linkRenderable2DAt(hostName: string, nodeId: string, r: Renderable2D)` → `bool`；[绑定实现 L1303](../../../../src/modules/scene/Scene.cpp#L1303)。
- `linkRenderable3DAt(hostName: string, nodeId: string, r: Renderable3D)` → `bool`；[绑定实现 L1304](../../../../src/modules/scene/Scene.cpp#L1304)。
- `localToWorldAt(hostName: string, nodeId: string, x: float, y: float, z: float)` → `Array<dynamic>`；[绑定实现 L1321](../../../../src/modules/scene/Scene.cpp#L1321)。
- `nodeIdFromSpatialIdAt(hostName: string, index: int)` → `string`；[绑定实现 L1357](../../../../src/modules/scene/Scene.cpp#L1357)。
- `pickRayAt(hostName: string, ox: float, oy: float, oz: float, dx: float, dy: float, dz: float)` → `string`；[绑定实现 L1348](../../../../src/modules/scene/Scene.cpp#L1348)。
- `pickScreenAt(hostName: string, cam: Camera3D, screenX: float, screenY: float, viewW: float, viewH: float)` → `string`；[绑定实现 L1349](../../../../src/modules/scene/Scene.cpp#L1349)。
- `removeChildAt(hostName: string, parentId: string, childId: string)` → `bool`；[绑定实现 L1326](../../../../src/modules/scene/Scene.cpp#L1326)。
- `removeNodeAt(hostName: string, nodeId: string)` → `bool`；[绑定实现 L1324](../../../../src/modules/scene/Scene.cpp#L1324)。
- `removeNodeTagAt(hostName: string, nodeId: string, tag: string)` → `bool`；[绑定实现 L1331](../../../../src/modules/scene/Scene.cpp#L1331)。
- `rootEntity(hostName: string, nodeId: string, instance: dynamic)` → `dynamic`；[绑定实现 L1362](../../../../src/modules/scene/Scene.cpp#L1362)。
- `setNodeBoundsAt(hostName: string, nodeId: string, minX: float, minY: float, minZ: float, maxX: float, maxY: float, maxZ: float)` → `bool`；[绑定实现 L1343](../../../../src/modules/scene/Scene.cpp#L1343)。
- `setNodeEventHandlerAt(hostName: string, cb: dynamic)` → `dynamic`；[绑定实现 L1337](../../../../src/modules/scene/Scene.cpp#L1337)。
- `setNodeLayerAt(hostName: string, nodeId: string, layer: int)` → `bool`；[绑定实现 L1335](../../../../src/modules/scene/Scene.cpp#L1335)。
- `setNodeLookAtAt(hostName: string, nodeId: string, tx: float, ty: float, tz: float)` → `bool`；[绑定实现 L1329](../../../../src/modules/scene/Scene.cpp#L1329)。
- `setNodeParentAt(hostName: string, childId: string, parentId: string)` → `bool`；[绑定实现 L1323](../../../../src/modules/scene/Scene.cpp#L1323)。
- `setNodeQuaternionAt(hostName: string, nodeId: string, qx: float, qy: float, qz: float, qw: float)` → `bool`；[绑定实现 L1327](../../../../src/modules/scene/Scene.cpp#L1327)。
- `syncSpatialIndexAt(hostName: string, ot: Octree)` → `bool`；[绑定实现 L1356](../../../../src/modules/scene/Scene.cpp#L1356)。
- `unlinkNodeAt(hostName: string, nodeId: string)` → `bool`；[绑定实现 L1309](../../../../src/modules/scene/Scene.cpp#L1309)。
- `unlinkNodeKindAt(hostName: string, nodeId: string, kind: string)` → `bool`；[绑定实现 L1310](../../../../src/modules/scene/Scene.cpp#L1310)。
- `unrootEntityAt(hostName: string, nodeId: string, index: int)` → `bool`；[绑定实现 L1367](../../../../src/modules/scene/Scene.cpp#L1367)。
- `updateScripts(dt: float)` → `void`；[绑定实现 L1293](../../../../src/modules/scene/Scene.cpp#L1293)。
- `worldToLocalAt(hostName: string, nodeId: string, x: float, y: float, z: float)` → `Array<dynamic>`；[绑定实现 L1322](../../../../src/modules/scene/Scene.cpp#L1322)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
