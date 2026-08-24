# 骨骼动画脚本播放：AnimSkin 蒙皮结果写回渲染网格 API 设计

日期：2026-08-23

## 背景

有 agent 报告：引擎脚本层 CPU 蒙皮（`AnimSkin`）能算出骨骼位置，但没有接口把
蒙皮结果写回渲染网格，因此骨骼动画在脚本里没法真正播放。

## 问题确认（现状调研）

结论：报告的核心结论**成立**——脚本侧目前端到端确实播放不了骨骼动画；但“没有
任何接口”的说法不准确，仓库里已存在通用回写原语，缺的是 `AnimSkin` 侧的便捷
接口、法线蒙皮和几条配套脚本通路。

逐项证据：

| 说法 | 实际情况 |
| --- | --- |
| `AnimSkin` 能算出骨骼顶点位置 | 成立。`AnimSkin::skinPositions` / `updateSkinnedPositions` 做 CPU 线性混合蒙皮（`src/modules/animation/AnimSkin.h/.cpp`）。 |
| 没有接口写回渲染网格 | 不准确。`Graphics::updateMeshVertices` 已实现并绑定脚本（`src/modules/graphics/Graphics.cpp:728`，Vulkan 实现 `GraphicsMesh.cpp:282`）。 |
| 脚本里仍无法播放骨骼动画 | 成立，原因如下面 4 点。 |

真正缺的四点：

1. **`AnimSkin` 没有打包数组 getter。** 脚本绑定只有逐分量
   `getSkinnedPositionX/Y/Z(vertexIndex)`（`Animation.cpp:443-447`），没有像
   `AnimLattice::getDeformedPositions()` 那样返回 `xyz` 打包数组的方法
   （`AnimLattice.h:168`）。脚本要回写就得每帧 `3N` 次脚本调用组数组
   （CesiumMan 约 2k+ 顶点，即 6k+ 次/帧），不现实。
2. **完全没有法线蒙皮。** `AnimSkin` 只算位置，回写后法线仍是 bind pose，
   肢体旋转时光照错误。现有 C++ 渲染测试也是把静态 bind 法线原样上传
   （`test/animation_skinned.cpp` 的 `renderSkinnedAnimation`），C++ 侧同样不完整。
3. **脚本拿不到网格句柄。** `model3d.createRenderable(gfx, model, meshIndex)`
   内部用 `newMeshFromAssimp` 建网格并 `setMesh`（`ModelRenderer.cpp:133-142`），
   但 `Renderable3D` 没有绑定 `getMesh()`（只有 `setMesh` / `getPartMesh`）；
   `ModelData` 也没有暴露位置/法线/索引数组 getter，用户无法自行建蒙皮网格。
4. **文档/示例缺失。** `docs/usr/modules/animation.md` 的 CPU 蒙皮示例只写到
   “脚本可读 `getSkinnedPositionX/Y/Z(i)`”并转给粒子；`graphics.md` 快查甚至
   未收录 `updateMeshVertices`；没有任一个 `.nut` 示例播放蒙皮网格。

附带坑：`Mesh::getVertexCount()` 返回 morph CPU 基数
`basePos_.size()/3`（`Mesh.cpp:103`），对无 morph 的网格（`newMeshFromArrays` /
普通 `newMeshFromAssimp`）恒为 0，脚本用户不能拿它当回写长度。

参考的既有模式：`AnimLattice` 已经走通“脚本算好变形 → `gfx.updateMeshVertices`
回传 GPU”的完整链路（`AnimLattice.h:34-36`、`examples/lattice-deform/main.nut:175`），
`AnimSkin` 直接沿用即可。

## 目标

- 让脚本一行调用完成“播放器推进 → 姿态世界矩阵 → CPU 蒙皮 → 写回网格”。
- 法线一并蒙皮，保证旋转/缩放后光照正确。
- 与 `AnimLattice` 既有 API 风格保持一致，同时保留逐分量 getter 兼容。

## 非目标

- 不做 GPU 骨骼动画（bone matrix UBO / 蒙皮 shader），本轮保持 CPU 蒙皮。
- 不改 `AnimSkeleton` / `AnimClip` / `AnimPlayer` / `AnimPose` 行为。
- 不引入新第三方依赖（法线变换用自带的 3x3 逆矩阵辅助函数）。

## 放置位置

全部改动在 `animation` 模块（与 `AnimSkin` 同层）：

- `src/modules/animation/AnimSkin.h/.cpp`：新增方法；头文件只前向声明
  `eve::graphics::Graphics` / `eve::graphics::Mesh`，`.cpp` 内
  `#include "graphics/Graphics.h"`（`animation` 已依赖 `graphics`，
  `AnimTrail.cpp` / `SpineAnim.cpp` 已有同样 include，depgraph 无头文件泄漏）。
- `src/modules/animation/Animation.cpp`：新增脚本绑定。
- `src/modules/graphics/RenderSystem3D.h` 与 `Graphics.cpp`：给 `Renderable3D`
  补 `getMesh()` 绑定（配套，让脚本拿到 `createRenderable` 建的网格）。
- 可选小修：`Mesh` 增加 GPU 顶点数记录，修 `getVertexCount()` 对无 morph 网格
  返回 0 的问题。

## API（脚本）

### 1. 一行播放（推荐）

```squirrel
skin.applyToMesh(gfx, mesh, pose);   // 位置 + 法线一次完成，返回 bool
```

等价于：`updateSkinnedPositions(pose)` → （内部）bind 法线蒙皮 →
`gfx.updateMeshVertices(mesh, pos, nrm, [], vertexCount, [], 0)`。
返回 `false` 表示参数非法或后端不支持（WebGPU `updateMeshVertices` 返回 false）。

### 2. 底层原语（与 `AnimLattice` 对齐，供自定义流水线）

```squirrel
skin.updateSkinnedPositions(pose);
skin.updateSkinnedNormals(pose);          // 用 fromModel 时拷贝的内部 bind 法线
local pos = skin.getSkinnedPositions();   // [x0,y0,z0,x1,...]，先蒙皮否则空数组
local nrm = skin.getSkinnedNormals();
gfx.updateMeshVertices(mesh, pos, nrm, [], skin.getVertexCount(), [], 0);
```

### 3. 配套

```squirrel
local ent  = model3d.createRenderable(gfx, model, 0);
local mesh = ent.getMesh();               // 新增绑定
```

## C++ 侧关键方法

`AnimSkin.h` 新增：

```cpp
/** @brief Packed skinned positions (xyz, vertexCount*3). Empty if not skinned yet. */
std::vector<float> getSkinnedPositions() const;

/**
 * @brief Skin bind normals (captured at fromModel) with the given pose.
 * pose must already have computeWorld(skeleton) applied.
 */
bool updateSkinnedNormals(const AnimPose *pose);

/** @brief Packed skinned normals (xyz, vertexCount*3). Empty if not skinned yet. */
std::vector<float> getSkinnedNormals() const;

/**
 * @brief Skin positions (+ normals) and write back to mesh in one call.
 * pose must already have computeWorld(skeleton). Returns false when the
 * backend cannot update in place (WebGPU) or arguments are invalid.
 */
bool applyToMesh(graphics::Graphics *gfx, graphics::Mesh *mesh,
                 const AnimPose *pose);
```

实现要点：

- `fromModel` 额外把 `aiMesh->mNormals`（若有）拷贝为 `bindNrm_`，与 `bindPos_`
  并列；没有法线时 `updateSkinnedNormals` 返回 false。
- 法线蒙皮复用 `skinPositions` 的 skin matrix
  `M_j = boneWorld_j * inverseBind_j`，对 `M_j` 的 3x3 线性部分取逆转置并
  normalize（`n' = normalize(IT(M33) * n)`）。`AnimMath::Mat4` 没有求逆，
  在 `AnimSkin.cpp` 内加一个小型 3x3 adjugate 辅助函数，不引入依赖。
- `applyToMesh` 内部：`skinPositions(pose, pos)` → 可选 `updateSkinnedNormals(pose)`
  → `gfx->updateMeshVertices(mesh, pos.data(), nrmPtr, nullptr, vertexCount_,
  nullptr, 0)`；`updateMeshVertices` 会顺带重算包围球（`GraphicsMesh.cpp:303`）。
- 绑定包装（`Animation.cpp`）：`getSkinnedPositions` / `getSkinnedNormals`
  直接绑返回 `std::vector<float>`（与 `AnimLattice` 相同）；`applyToMesh` 用
  `std::function<bool(AnimSkin*, Graphics*, Mesh*, AnimPose*)>` 包装。

配套小修（`Mesh`）：

```cpp
int gpuVertexCount = 0;   // 上传时填写
int getVertexCount() const { return !basePos_.empty() ? int(basePos_.size() / 3)
                                                      : gpuVertexCount; }
```

在 `newMeshFromAssimp` / `newMeshFromArrays` 上传路径里填 `gpuVertexCount`，
同时让 `applyToMesh` 可以校验 `mesh->getVertexCount() == vertexCount_`。

## 示例（脚本）

```squirrel
local model  = model3d.newModelDataFromFile("CesiumMan.gltf");
local ent    = model3d.createRenderable(gfx, model, 0);   // meshIndex 需 hasBones
local mesh   = ent.getMesh();
local sk     = anim.newSkeletonFromModel(model);
local clip   = anim.newClipFromModel(model, sk, 0);
local skin   = anim.newSkinFromModel(model, 0, sk);
local player = anim.newPlayer(sk);
player.play(clip);

eve_update = function(dt) {
    player.update(dt);
    local pose = player.getPose();
    pose.computeWorld(sk);
    skin.applyToMesh(gfx, mesh, pose);   // 每帧一行
};
```

注意：蒙皮网格必须与 `AnimSkin` 同源（同一 `ModelData` / 同一 meshIndex），且
上传时不要烘焙节点世界变换（`newMeshFromAssimp` 的 worldTransform 重载），否则
回写的模型空间位置与烘焙后的网格对不上。

## 测试

- C++ 单测（`test/animation_skinned.cpp` 追加）：
  - `animation.skinned.getSkinnedPositionsMatchesSkinPositionsTo`：
    `updateSkinnedPositions` 后 `getSkinnedPositions` 与 `skinPositionsTo` 输出一致。
  - `animation.skinned.normalsFollowPose`：bind pose 下法线不变（skin matrix
    近似单位阵），mid-clip 姿态下至少一个法线发生旋转且全部保持单位长度。
  - `animation.skinned.render.applyToMesh`：网格只创建一次，每帧
    `applyToMesh` 原地写回（不再每帧 `newMeshFromArrays` 重建），断言
    多帧 luma 有变化、末帧前景像素数达标。
- WebGPU 目标：断言 `applyToMesh` 返回 false（后端暂不支持原地更新）。
- 脚本冒烟：新增 `examples/skinned-anim/`（可选）或按 `lattice-deform` 的
  结构补一个最小示例。

## 兼容性与格式

- 纯新增 API，不改现有方法签名与行为；逐分量 getter 保留。
- 文档同步：
  - `docs/usr/modules/animation.md`：CPU 蒙皮示例改为 `applyToMesh` 一行，
    快查表补 `getSkinnedPositions` / `updateSkinnedNormals` / `getSkinnedNormals`
    / `applyToMesh`。
  - `docs/usr/modules/graphics.md`：快查表补 `updateMeshVertices` / `getMesh`。
- 提交前 `git clang-format` 只格式化改动行；按模块约定单 PR 提交接口 + 后端 +
  消费者（本例消费者为脚本示例与文档）。
