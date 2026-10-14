# procgen：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## ProcgenCloudField

- `coverageAt(x: float, z: float, time: float)` → `float`；[绑定实现 L1494](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1494)。
- `setCoverage(coverage: float)` → `void`；[绑定实现 L1487](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1487)。
- `setDetail(detail: float)` → `void`；[绑定实现 L1489](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1489)。
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

## ProcgenContext

- `getName()` → `string`；[绑定实现 L335](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L335)。
- `getOutput(outputName: string)` → `PointSet`；[绑定实现 L347](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L347)。
- `getOutputCount()` → `int`；[绑定实现 L345](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L345)。
- `getOutputName(index: int)` → `string`；[绑定实现 L346](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L346)。
- `getTraceCount()` → `int`；[绑定实现 L360](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L360)。
- `getTraceInputCount(index: int)` → `int`；[绑定实现 L362](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L362)。
- `getTraceMilliseconds(index: int)` → `float`；[绑定实现 L364](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L364)。
- `getTraceName(index: int)` → `string`；[绑定实现 L361](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L361)。
- `getTraceOutputCount(index: int)` → `int`；[绑定实现 L363](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L363)。
- `hasFailed()` → `bool`；[绑定实现 L339](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L339)。
- `hasOutput(outputName: string)` → `bool`；[绑定实现 L344](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L344)。

## ProcgenGrid2D

- `addObjectAt(name: string, type: string, x: float, y: float)` → `void`；[绑定实现 L186](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L186)。
- `clearObjects()` → `void`；[绑定实现 L185](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L185)。
- `getObjectCount()` → `int`；[绑定实现 L189](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L189)。
- `getObjectGid(i: int)` → `int`；[绑定实现 L196](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L196)。
- `getObjectHeight(i: int)` → `float`；[绑定实现 L195](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L195)。
- `getObjectWidth(i: int)` → `float`；[绑定实现 L194](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L194)。
- `getObjectX(i: int)` → `float`；[绑定实现 L192](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L192)。
- `getObjectY(i: int)` → `float`；[绑定实现 L193](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L193)。
- `resize(width: int, height: int)` → `dynamic`；[绑定实现 L160](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L160)。
- `setCell(x: int, y: int, semantic: int)` → `dynamic`；[绑定实现 L166](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L166)。
- `setDetail(x: int, y: int, detail: int)` → `dynamic`；[绑定实现 L171](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L171)。
- `setMeta(key: string, data: string)` → `dynamic`；[绑定实现 L180](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L180)。

## ProcgenHeightmap

- `resize(width: int, height: int)` → `void`；[绑定实现 L1628](../../../../src/modules/procgen/heightmap/TerrainStampScript.cpp#L1628)。
- `sampleBilinearSeamless(x: float, y: float)` → `float`；[绑定实现 L1914](../../../../src/modules/procgen/heightmap/TerrainStampScript.cpp#L1914)。

## ProcgenMeshBuild

- `empty()` → `bool`；[绑定实现 L800](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L800)。
- `getGroupCount()` → `int`；[绑定实现 L786](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L786)。
- `getGroupName(groupIndex: int)` → `string`；[绑定实现 L787](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L787)。
- `getIndex(i: int)` → `int`；[绑定实现 L821](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L821)。
- `getIndexCount()` → `int`；[绑定实现 L799](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L799)。
- `getNormalX(i: int)` → `float`；[绑定实现 L804](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L804)。
- `getNormalY(i: int)` → `float`；[绑定实现 L805](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L805)。
- `getNormalZ(i: int)` → `float`；[绑定实现 L806](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L806)。
- `getPositionX(i: int)` → `float`；[绑定实现 L801](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L801)。
- `getPositionY(i: int)` → `float`；[绑定实现 L802](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L802)。
- `getPositionZ(i: int)` → `float`；[绑定实现 L803](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L803)。
- `getTriangleGroup(triangleIndex: int)` → `int`；[绑定实现 L788](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L788)。
- `getUvU(i: int)` → `float`；[绑定实现 L807](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L807)。
- `getUvV(i: int)` → `float`；[绑定实现 L808](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L808)。
- `getVertexCount()` → `int`；[绑定实现 L798](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L798)。
- `setActiveGroup(name: string)` → `int`；[绑定实现 L785](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L785)。
- `setMeta(key: string, value: string)` → `void`；[绑定实现 L822](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L822)。

## ProcgenOutput

- `getPalette()` → `string`；[绑定实现 L128](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L128)。
- `getPath()` → `string`；[绑定实现 L130](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L130)。
- `setPalette(paletteName: string)` → `void`；[绑定实现 L127](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L127)。
- `setPath(path: string)` → `void`；[绑定实现 L129](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L129)。

## ProcgenOwnedContext

- `getName()` → `dynamic`；[绑定实现 L701](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L701)。
- `hasFailed()` → `dynamic`；[绑定实现 L709](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L709)。

## ProcgenOwnedGrid2D

- `getObjectCount()` → `dynamic`；[绑定实现 L618](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L618)。
- `getObjectGid(index: int)` → `dynamic`；[绑定实现 L646](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L646)。
- `getObjectHeight(index: int)` → `dynamic`；[绑定实现 L642](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L642)。
- `getObjectWidth(index: int)` → `dynamic`；[绑定实现 L638](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L638)。
- `getObjectX(index: int)` → `dynamic`；[绑定实现 L630](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L630)。
- `getObjectY(index: int)` → `dynamic`；[绑定实现 L634](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L634)。
- `resize(width: int, height: int)` → `dynamic`；[绑定实现 L539](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L539)。
- `setCell(x: int, y: int, semantic: int)` → `dynamic`；[绑定实现 L571](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L571)。
- `setDetail(x: int, y: int, detail: int)` → `dynamic`；[绑定实现 L599](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L599)。
- `setMeta(key: string, data: string)` → `dynamic`；[绑定实现 L666](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L666)。

## ProcgenOwnedParams

- `getString(key: string, fallback: string)` → `dynamic`；[绑定实现 L511](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L511)。

## ProcgenParams

- `getString(key: string, defaultValue: string)` → `string`；[绑定实现 L119](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L119)。

## ProcgenPbrMaterial

- `getAlbedoHeight()` → `dynamic`；[绑定实现 L1513](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1513)。
- `getAlbedoWidth()` → `dynamic`；[绑定实现 L1512](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1512)。
- `getAoWidth()` → `dynamic`；[绑定实现 L1518](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1518)。
- `getHeightWidth()` → `dynamic`；[绑定实现 L1517](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1517)。
- `getMetallicWidth()` → `dynamic`；[绑定实现 L1516](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1516)。
- `getNormalWidth()` → `dynamic`；[绑定实现 L1514](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1514)。
- `getRoughnessWidth()` → `dynamic`；[绑定实现 L1515](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1515)。
- `hasAllMaps()` → `dynamic`；[绑定实现 L1519](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1519)。

## ProcgenPointSet

- `empty()` → `bool`；[绑定实现 L204](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L204)。
- `getDensity(index: int)` → `float`；[绑定实现 L239](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L239)。
- `getFloatAttribute(index: int, name: string, fallback: float)` → `float`；[绑定实现 L253](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L253)。
- `getNormalX(index: int)` → `float`；[绑定实现 L212](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L212)。
- `getNormalY(index: int)` → `float`；[绑定实现 L213](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L213)。
- `getNormalZ(index: int)` → `float`；[绑定实现 L214](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L214)。
- `getPointSeed(index: int)` → `int`；[绑定实现 L241](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L241)。
- `getScaleZ(index: int)` → `float`；[绑定实现 L223](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L223)。
- `getStringAttribute(index: int, name: string, fallback: string)` → `string`；[绑定实现 L267](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L267)。
- `hasFloatAttribute(index: int, name: string)` → `bool`；[绑定实现 L254](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L254)。
- `hasStringAttribute(index: int, name: string)` → `bool`；[绑定实现 L268](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L268)。
- `setDensity(index: int, density: float)` → `void`；[绑定实现 L238](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L238)。
- `setFloatAttribute(index: int, name: string, value: float)` → `void`；[绑定实现 L252](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L252)。
- `setPointSeed(index: int, seed: int)` → `void`；[绑定实现 L240](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L240)。
- `setStringAttribute(index: int, name: string, value: string)` → `void`；[绑定实现 L266](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L266)。

## ProcgenTerrainMeshChunk

- `getIndex(i: int)` → `dynamic`；[绑定实现 L1481](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1481)。
- `getIndexCount()` → `int`；[绑定实现 L1459](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1459)。
- `getNormalX(i: int)` → `dynamic`；[绑定实现 L1475](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1475)。
- `getNormalY(i: int)` → `dynamic`；[绑定实现 L1477](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1477)。
- `getNormalZ(i: int)` → `dynamic`；[绑定实现 L1479](../../../../src/modules/procgen/ProcgenBindingsTable.cpp#L1479)。
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

- `generateNormalImage()` → `dynamic`；[绑定实现 L441](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L441)。
- `generateTo()` → `dynamic`；[绑定实现 L1168](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1168)。
- `getMeshRecipeCount()` → `int`；[绑定实现 L1308](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1308)。
- `getMeshRecipeId(index: int)` → `string`；[绑定实现 L1309](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1309)。
- `getName()` → `string`；[绑定实现 L69](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L69)。
- `getPaletteGid(palette: string, semantic: string)` → `int`；[绑定实现 L1128](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1128)。
- `getPbrRecipeCount()` → `int`；[绑定实现 L1222](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1222)。
- `getPbrRecipeId(index: int)` → `string`；[绑定实现 L1223](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1223)。
- `getSystemOutputCount(name: string)` → `int`；[绑定实现 L1120](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1120)。
- `getSystemOutputName(name: string, index: int)` → `string`；[绑定实现 L1121](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1121)。
- `getSystemSeed(name: string)` → `int`；[绑定实现 L1118](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1118)。
- `getTextureRecipeCount()` → `int`；[绑定实现 L1200](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1200)。
- `getTextureRecipeId(index: int)` → `string`；[绑定实现 L1201](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1201)。
- `gridToJson()` → `dynamic`；[绑定实现 L1188](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1188)。
- `hasMeshRecipe(recipeId: string)` → `bool`；[绑定实现 L1310](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1310)。
- `hasPbrRecipe(recipeId: string)` → `bool`；[绑定实现 L1224](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1224)。
- `hasSystem(name: string)` → `bool`；[绑定实现 L1116](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1116)。
- `hasTextureRecipe(recipeId: string)` → `bool`；[绑定实现 L1202](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1202)。
- `newCloudField()` → `dynamic`；[绑定实现 L386](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L386)。
- `newCloudShadow()` → `dynamic`；[绑定实现 L399](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L399)。
- `newOutput()` → `dynamic`；[绑定实现 L79](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L79)。
- `newTerrainSampler()` → `dynamic`；[绑定实现 L351](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L351)。
- `removeSystem()` → `dynamic`；[绑定实现 L1108](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1108)。
- `setPaletteGid(palette: string, semantic: string, gid: int)` → `void`；[绑定实现 L1127](../../../../src/modules/procgen/ProcgenBindingsClass.cpp#L1127)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
