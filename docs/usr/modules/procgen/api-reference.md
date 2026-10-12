# procgen：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## ProcgenCloudField

- `coverageAt(x: float, z: float, time: float)` → `float`；[绑定实现 L1494](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1494)。
- `setCoverage(coverage: float)` → `void`；[绑定实现 L1487](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1487)。
- `setOctaves(octaves: int)` → `void`；[绑定实现 L1491](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1491)。
- `setSeamless(seamless: bool)` → `void`；[绑定实现 L1493](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1493)。
- `setSoftness(softness: float)` → `void`；[绑定实现 L1488](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1488)。
- `setWarp(warp: float)` → `void`；[绑定实现 L1492](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1492)。
- `setWind(speed: float, angleRad: float)` → `void`；[绑定实现 L1490](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1490)。
- `setWorldScale(worldScale: float)` → `void`；[绑定实现 L1486](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1486)。

## ProcgenCloudShadow

- `coverageAt(x: float, z: float, time: float)` → `float`；[绑定实现 L1501](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1501)。
- `setCloudAltitude(altitude: float)` → `void`；[绑定实现 L1499](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1499)。
- `setStrength(strength: float)` → `void`；[绑定实现 L1500](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1500)。
- `setSunDirection(x: float, y: float, z: float)` → `void`；[绑定实现 L1498](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1498)。
- `shadowFactorAt(x: float, z: float, time: float)` → `float`；[绑定实现 L1502](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1502)。

## ProcgenHeightmap

- `sampleBilinearSeamless(x: float, y: float)` → `float`；[绑定实现 L1914](../../../../src/modules/procgen/heightmap/TerrainStampScript.cpp#L1914)。

## ProcgenMeshBuild

- `getIndex(i: int)` → `int`；[绑定实现 L821](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L821)。
- `getIndexCount()` → `int`；[绑定实现 L799](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L799)。
- `getPositionX(i: int)` → `float`；[绑定实现 L801](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L801)。
- `getPositionY(i: int)` → `float`；[绑定实现 L802](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L802)。
- `getPositionZ(i: int)` → `float`；[绑定实现 L803](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L803)。
- `getUvU(i: int)` → `float`；[绑定实现 L807](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L807)。
- `getUvV(i: int)` → `float`；[绑定实现 L808](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L808)。
- `getVertexCount()` → `int`；[绑定实现 L798](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L798)。

## ProcgenPbrMaterial

- `getAlbedoHeight()` → `dynamic`；[绑定实现 L1513](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1513)。
- `getAlbedoWidth()` → `dynamic`；[绑定实现 L1512](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1512)。
- `getAoWidth()` → `dynamic`；[绑定实现 L1518](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1518)。
- `getHeightWidth()` → `dynamic`；[绑定实现 L1517](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1517)。
- `getMetallicWidth()` → `dynamic`；[绑定实现 L1516](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1516)。
- `getNormalWidth()` → `dynamic`；[绑定实现 L1514](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1514)。
- `getRoughnessWidth()` → `dynamic`；[绑定实现 L1515](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1515)。
- `hasAllMaps()` → `dynamic`；[绑定实现 L1519](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1519)。

## ProcgenTerrainMeshChunk

- `getIndex(i: int)` → `dynamic`；[绑定实现 L1481](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1481)。
- `getIndexCount()` → `int`；[绑定实现 L1459](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1459)。
- `getPositionX(i: int)` → `dynamic`；[绑定实现 L1469](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1469)。
- `getPositionY(i: int)` → `dynamic`；[绑定实现 L1471](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1471)。
- `getPositionZ(i: int)` → `dynamic`；[绑定实现 L1473](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1473)。
- `getVertexCount()` → `int`；[绑定实现 L1458](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1458)。

## ProcgenTerrainSampler

- `getAmplitude()` → `float`；[绑定实现 L1418](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1418)。
- `getClampMax()` → `float`；[绑定实现 L1422](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1422)。
- `getClampMin()` → `float`；[绑定实现 L1421](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1421)。
- `getCoastSoftness()` → `float`；[绑定实现 L1411](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1411)。
- `getContinent()` → `float`；[绑定实现 L1407](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1407)。
- `getExponent()` → `float`；[绑定实现 L1405](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1405)。
- `getFrequency()` → `float`；[绑定实现 L1391](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1391)。
- `getGain()` → `float`；[绑定实现 L1399](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1399)。
- `getIsland()` → `float`；[绑定实现 L1409](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1409)。
- `getLacunarity()` → `float`；[绑定实现 L1397](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1397)。
- `getOctaves()` → `int`；[绑定实现 L1395](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1395)。
- `getRidge()` → `float`；[绑定实现 L1401](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1401)。
- `getWarp()` → `float`；[绑定实现 L1403](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1403)。
- `getWavelength()` → `float`；[绑定实现 L1393](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1393)。
- `getWorldHeight()` → `int`；[绑定实现 L1414](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1414)。
- `getWorldWidth()` → `int`；[绑定实现 L1413](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1413)。
- `isClamped()` → `bool`；[绑定实现 L1420](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1420)。
- `sampleTile(tileX: int, tileY: int)` → `float`；[绑定实现 L1385](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1385)。
- `setAmplitude(amplitude: float)` → `void`；[绑定实现 L1417](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1417)。
- `setBase(base: float)` → `void`；[绑定实现 L1415](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1415)。
- `setClamp(enabled: bool, minHeight: float, maxHeight: float)` → `void`；[绑定实现 L1419](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1419)。
- `setCoastSoftness(softness: float)` → `void`；[绑定实现 L1410](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1410)。
- `setContinent(continent: float)` → `void`；[绑定实现 L1406](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1406)。
- `setExponent(exponent: float)` → `void`；[绑定实现 L1404](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1404)。
- `setFrequency(frequency: float)` → `void`；[绑定实现 L1390](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1390)。
- `setGain(gain: float)` → `void`；[绑定实现 L1398](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1398)。
- `setIsland(island: float)` → `void`；[绑定实现 L1408](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1408)。
- `setLacunarity(lacunarity: float)` → `void`；[绑定实现 L1396](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1396)。
- `setOctaves(octaves: int)` → `void`；[绑定实现 L1394](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1394)。
- `setRidge(ridge: float)` → `void`；[绑定实现 L1400](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1400)。
- `setWarp(warp: float)` → `void`；[绑定实现 L1402](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1402)。
- `setWavelength(wavelength: float)` → `void`；[绑定实现 L1392](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1392)。
- `setWorldSize(width: int, height: int)` → `void`；[绑定实现 L1412](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1412)。

## 接收者见绑定源码

- `getPbrRecipeCount()` → `int`；[绑定实现 L1222](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1222)。
- `getPbrRecipeId(index: int)` → `string`；[绑定实现 L1223](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1223)。
- `hasPbrRecipe(recipeId: string)` → `bool`；[绑定实现 L1224](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1224)。
- `newCloudField()` → `dynamic`；[绑定实现 L386](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L386)。
- `newCloudShadow()` → `dynamic`；[绑定实现 L399](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L399)。
- `newTerrainSampler()` → `dynamic`；[绑定实现 L351](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L351)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
