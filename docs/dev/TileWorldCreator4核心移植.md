# TileWorldCreator 4 核心功能移植到 EVEngine

## 范围

本移植以 `TileWorldCreator 4 v4.3.5.unitypackage` 中的运行时算法、图组合、网格构建和配置语义为
行为参考，采用 EVEngine 原生 `Grid2D`、`PointSet`、`PointGraph`、`MeshBuild`、Result 与模块句柄。
“核心功能完成”不包括 Unity Inspector/EditorWindow 外观、Unity Prefab/ScriptableObject 二进制兼容、
Unity Renderer/Collider 组件或示例美术资产；这些是宿主编辑器与资产格式，不是生成算法本身。

## 功能对应

- Dot Grid → `generate.dot_grid`
- Cellular Automata → `generate.cellular`
- Pathfinding → `grid.path`
- BSP Dungeon → `generate.registry` + `algorithm=dungeon.bsp`；完整 Roguelike 地牢使用
  `algorithm=level.roguelike`，不在 GridGraph 内维护第二套地牢算法
- Random Walk Dungeon → `generate.random_walk`
- Maze → `generate.maze`
- Height Texture → `Grid2D.detail` 输入与 `select.detail_range`
- Random Noise → `generate.random_noise`
- Shapes → `generate.shape`
- Checkerboard → `generate.checkerboard`
- Poisson Disc Sampling → `generate.poisson`
- Add / Boolean / Subtract → `grid.union`、`grid.intersect`、`grid.subtract`
- Smooth / Expand / Shrink / Invert → 同名 `grid.*` 节点
- Select / Select by neighbour / Select by rule → `select.random`、`select.neighbors`、`select.rule`
- Select islands / Find position on islands → `select.islands`、`select.island_centers`
- Push to paint positions → 图输出的拥有型 `Grid2D`；由调用方显式绑定为下游输入，不使用隐式全局画布
- Tile preset / neighbour rules → `grid.autotile` 把八邻域 mask 写入 detail；
  `resolveTilePreset` 按 4.3.5 原表精确解析 14 类标准网格瓦片、旋转、X 镜像和 9 位配置
- Object build layer → `ObjectBuildLayer` 把 PointGraph 输出转换为带 `asset`、父子关系和完整变换的
  `PointSet`，支持确定性加权选择、随机变换、朝向层、place-on-top 高度属性和子对象规则
- Persistent build configuration → `BuildLayerStack` 按插入顺序执行启用的 Tiles/Objects 层，使用
  `EVPCG_BUILD_LAYERS 1` 与嵌入式 `EVPCG_OBJECT_LAYER 1` 格式原子回读，失败不发布部分 artifacts
- Incremental build → `IncrementalBuildExecutor` 按固定格子簇缓存 BuildLayerStack artifacts；哈希包含层定义、
  格子一圈 halo、完整点字段和强类型属性，输出有序 upsert/removal delta，任一簇失败则旧缓存原样保留
- Tiles build layer / mesh combiner → `mesh.grid_tiles`、`mesh.merge`、`mesh.transform`
- Grid mesh collider generator → 增量示例从每簇 `MeshBuild` 创建 `World3D` 静态三角网格碰撞体；
  更新采用先建后换，removal 同步销毁，且 physics provider 缺席时可观察地退化为纯渲染
- Runtime manager / configuration → Procgen 模块拥有的代际句柄、图 revision、版本 1 定义格式和统一 Result

## 混合调用契约

标准路径为：

`GridGraph -> convert.grid_to_points -> point.subgraph(PointGraph) -> MeshGraph.point.input -> mesh.instance_points`

网格铺砖路径为：

`GridGraph -> grid.autotile -> MeshGraph.grid.input -> mesh.grid_tiles -> MeshBuild/GeneratedArtifact`

跨域只传递拥有型值或显式句柄。连接建立时检查端口类型；修改从节点向下游失效缓存；图实例仅允许
所属线程访问且不可重入。Grid/Mesh 定义只持久化拓扑与标量参数，场景输入在恢复后重新绑定。

## 确定性与失败语义

随机生成器使用图参数中的命名 seed；相同版本、尺寸、参数和 seed 产生相同网格。图执行成本与访问
格子数、输出顶点数或“源网格顶点数 × 点数量”成线性关系。无效操作、参数、类型、断开的必需输入、
环、尺寸不匹配和损坏资产均返回结构化 Result，不使用 `bool + lastError`。

## 验证入口

- `examples/tileworld-graph-dungeon`：可直接运行的资产无关 3D 示例，覆盖注册表 Roguelike 地牢、
  `select.semantic` 分层、autotile、随机选择、
  Grid→PointGraph、版本化 BuildLayerStack、PointSet→MeshGraph 实例化，以及边界墙、导航和装饰层；
  地面与对象场景缓存直接消费 8×8 格子簇的 upsert/removal delta，不再额外执行完整 build stack。
  同一 delta 还驱动每簇静态三角网格碰撞体的替换/回收。运行时会选取可行走端点、执行
  `grid.path`，并验证 MeshBuild 上传、空对象簇、热重载和换 seed 重建。
- `test/procgen_grid_graph.cpp`：生成器、选择器、寻路、autotile、TilePreset 精确配置、Grid→PointGraph、Grid/Point→Mesh、
  类型拒绝、环、缓存失效和两种定义格式回读。
- `test/procgen_object_build_layer.cpp`：加权资产、确定性随机变换、朝向、place-on-top、子对象和失败路径。
- `test/procgen_build_layer_stack.cpp`：层顺序/启用状态、混合 artifact、版本往返、未知字段和损坏配置原子拒绝。
- `test/procgen_incremental_build.cpp`：稳定缓存、簇内与边界脏区传播、删除 delta、全局坐标网格分片、
  参数拒绝以及失败时事务回滚。
- `test/procgen_result_contract.cpp`：Squirrel 创建拥有型 Grid2D 并直接绑定 GridGraph，随后执行
  GridGraph/MeshGraph 并取得拥有型 MeshBuild，覆盖跨图代理的生命周期与类型契约。
- `src/modules/procgen/GridGraphAlgorithms.cpp`：独立确定性算法实现。
