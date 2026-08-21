# 3D 晶格缩放变形（Lattice Scale Deformer）设计

日期：2026-08-21  
状态：已实现（PR: codex/lattice-scale-deformer）

## 背景

骨骼动画只覆盖“按骨骼权重移动顶点”，要做卡通式的 squash & stretch、
局部鼓起、区域性缩放等程序化形变时没有现成手段。现有 `AnimSkin`
只做线性混合蒙皮；`Tween` / `ControlAnim` 只能驱动标量属性，无法直接
把缩放“场”施加到网格顶点上。

参考 Blender 的 Lattice 修改器：用一个三维控制点网格（晶格）包住模型，
移动/缩放控制点，顶点按其在晶格中的归一化坐标做三线性插值得到形变。
本设计聚焦“缩放 + 偏移”两类控制点变换，覆盖程序化缩放变形动画的
主要需求（整体 squash/stretch、轴向拉伸、局部鼓起），且实现轻量、
纯 CPU、脚本可驱动。

## 目标

1. 新增 `AnimLattice`（脚本类型 `AnimLattice`），提供：
   - 晶格框架：分割数（≥2）、尺寸、原点、是否把晶格外顶点钳制到边界；
   - 控制点：每个控制点可设 `scale (sx,sy,sz)` 与 `offset (dx,dy,dz)`，
     默认恒等（offset=0、scale=1）；
   - 绑定：从 `ModelData` 网格（Assimp 顶点）或脚本数组绑定顶点位置；
   - 变形：对绑定顶点（或每帧传入的顶点数组）按三线性权重插值输出
     新位置与法线；
   - 缓存：`updateDeformed*` 之后可逐顶点读取，或整体取回数组。
2. 脚本可直接制作程序化 3D 缩放变形动画：
   - `anim.newLattice(divX, divY, divZ)` 创建晶格；
   - `lat.setPointScale(...)` / `lat.setPointOffset(...)` 驱动控制点；
   - `lat.updateDeformedPositions()` 变形绑定顶点；
   - `gfx.updateMeshVertices(mesh, posArray, nrmArray, ...)` 每帧回传 GPU。
3. 与现有动画系统组合：
   - `Tween` / `ControlAnim` 提供标量（如全局 `setScale` 或单点缩放）→
     脚本每帧 `lat.setPointScale(...)` 即可；
   - 先 `AnimSkin` 蒙皮、再晶格变形的流水线在 C++ 侧可直接串联
     （`skinPositionsTo` → `deformPositions`）。

## 非目标

- 不做 GPU 端晶格着色器（本轮保持 CPU 变形 + `updateMeshVertices` 上传）。
- 不做控制点旋转（三线性插值仅限缩放 + 平移；旋转留作后续扩展）。
- 不做晶格对象自身的旋转/非轴对齐包围盒（用户可通过旋转网格实体近似）。
- 不做网格拓扑编辑（索引不变，只动顶点位置/法线）。
- 不改动现有 `AnimSkin` / `Tween` / `ControlAnim` 行为。

## 放置位置

放在 `animation` 模块（与 CPU 蒙皮 `AnimSkin` 同层）：

- `src/modules/animation/AnimLattice.h`
- `src/modules/animation/AnimLattice.cpp`

`animation` 已依赖 `model3d`（`DEPS data filesystem graphics model3d`），
从 `ModelData` 读 Assimp 顶点不需要新增模块依赖。脚本绑定登记在
`Animation::expose(ssq::Table&)`，工厂方法 `Animation::newLattice`。

另在 `graphics` 模块给脚本补两个已有 C++ API 的薄包装：

- `gfx.newMeshFromArrays(posArray, nrmArray, uvArray, vertexCount, indicesArray, indexCount)`
- `gfx.updateMeshVertices(mesh, posArray, nrmArray, uvArray, vertexCount, indicesArray, indexCount)`

它们分别转发到 `Graphics::newMeshFromArrays` / `updateMeshVertices`，
参数用 Squirrel 数组（空数组 = 缺省）。这样脚本才能把晶格变形结果
每帧写回 GPU，也是当前唯一缺的“脚本侧 CPU 网格更新”能力。

## 变形数学

晶格盒子以原点 `O` 为中心，尺寸 `(sx, sy, sz)`；分割数 `(nx, ny, nz)`，
控制点网格坐标 `(i,j,k)`，`i∈[0,nx-1]` 等。顶点 `p` 的晶格坐标：

```
u = (px - (Ox - sx/2)) / sx
v = (py - (Oy - sy/2)) / sy
w = (pz - (Oz - sz/2)) / sz
```

默认把 `u,v,w` 钳制到 `[0,1]`（晶格外顶点按边界单元插值，视觉连续）；
`setClamp(false)` 可改为线性外推。

每个控制点携带：

- `offset (dx,dy,dz)`：平移；
- `scale (sx,sy,sz)`：围绕晶格原点 `O` 的轴对齐缩放。

顶点所在单元的下标 `(i0,j0,k0) = floor(u*(nx-1))` 等，分数坐标
`fu, fv, fw ∈ [0,1]`，8 个角点的三线性权重：

```
w(i0+i, j0+j, k0+k) = (i?fu:1-fu) * (j?fv:1-fv) * (k?fw:1-fw)
```

插值得到逐顶点仿射映射：

```
δ(p) = Σ w * offset_ijk
S(p) = Σ w * scale_ijk（对角矩阵）
p'   = O + δ(p) + S(p) · (p - O)
```

全默认时恒等。缩放角点控制点会把该区域相对晶格原点拉伸/压缩，
跨单元边界连续；`setScale(sx,sy,sz)` 全部控制点同值即整体
squash & stretch。

法线用同一插值得到的对角缩放矩阵（不含平移）变换后归一化：

```
n' = normalize(S(p) · n)
```

## API（脚本）

```
local lat = anim.newLattice(4, 4, 4);      // 分割数 ≥ 2
lat.setSize(3.0, 3.0, 3.0);                // 晶格包围盒尺寸（默认 1）
lat.setOrigin(0.0, 0.0, 0.0);              // 盒子中心
lat.setClamp(true);                        // 越界顶点钳制（默认 true）

lat.bindModel(model, meshIndex);           // 绑定模型网格顶点
// 或 lat.bindPositionsFromArray(posArray);

lat.setScale(1.2, 0.8, 1.2);               // 全部控制点统一缩放
lat.setPointScale(3, 3, 3, 2.0, 1.0, 1.0); // 单点缩放
lat.setPointOffset(3, 3, 3, 0.3, 0.0, 0.0);// 单点偏移（鼓起）
lat.reset();                               // 全部恢复恒等

lat.updateDeformedPositions();             // 变形绑定顶点（缓存）
// 或 lat.updateDeformedPositionsFromArray(skinnedPosArray);  // 蒙皮后流水线
// 法线需要变形后的位置做晶格单元查询，再配合原始法线：
lat.updateDeformedNormalsFromArray(lat.getDeformedPositions(), nrmArray);

local pos = lat.getDeformedPositions();    // 取回变形数组（xyz 打包）
local nrm = lat.getDeformedNormals();
gfx.updateMeshVertices(mesh, pos, nrm, [], mesh.getVertexCount(), [], 0);
```

### C++ 侧关键方法

```cpp
static AnimLattice *fromModel(const model3d::ModelData *model, int meshIndex,
                              int divX, int divY, int divZ);
void bindPositions(const float *posXYZ, int count);
void setPointScale(int ix, int iy, int iz, float sx, float sy, float sz);
void setPointOffset(int ix, int iy, int iz, float dx, float dy, float dz);
void deformPositions(const float *inPosXYZ, float *outPosXYZ, int count) const;
bool deformNormals(const float *posXYZ, const float *inNrmXYZ, float *outNrmXYZ,
                   int count) const;
bool updateDeformedPositions(const std::vector<float> &inPosXYZ);
bool updateDeformedPositions();
std::vector<float> getDeformedPositions() const;
```

`setDivisions` / `setSize` / `setOrigin` / `bind*` 会置脏晶格坐标缓存
并要求重新绑定/重新变形；控制点数组在 `setDivisions` 时重置为恒等。

## 测试

新增 `test/animation_lattice.cpp`（纯数学，无图形依赖），覆盖：

1. 默认恒等：任意顶点变形前后一致。
2. 整体缩放：`setScale(2,1,1)` 后 x 放大 2 倍、y/z 不变；
   法线相应归一化。
3. 单点缩放 + 三线性权重：单元内顶点按权重插值，角点精确命中。
4. 偏移鼓起：单点 offset 使靠近该角点的顶点位移最大。
5. 越界钳制 vs 外推。
6. `bindModel` 绑定网格顶点数与位置一致。
7. 参数校验：分割数 < 2、非法控制点坐标、顶点索引越界抛异常。
8. `getDeformedPositions()` 与逐顶点 getter 一致。

注册进 `test/CMakeLists.txt` 的 `all_test_cpp`。

## 示例

新增 `examples/lattice-deform/`：脚本生成一个细分立方体网格，
用 `anim.newLattice(4,4,4)` 绑定，播放“压扁-拉伸”循环：

- 整体 `setScale` 由 `Tween` 的标量驱动（yoyo + repeat）；
- 一个角落控制点做局部鼓起/凹陷；
- 每帧 `updateDeformedPositions/updateDeformedNormalsFromArray` +
  `gfx.updateMeshVertices` 回传 GPU。

## 兼容性与格式

- 不改任何既有公共 API；新增类与工厂方法。
- 新文件按 `.clang-format` 手工格式化（新文件跳过 CI 格式检查但有警告）。
- `scripts/module_depgraph.py --check` 不应新增跨层 include
  （`AnimLattice.cpp` 只 include `animation/*`、`model3d/ModelData.h`、
  `common/Exception.h`、Assimp 头，均在 animation 现有依赖内）。
