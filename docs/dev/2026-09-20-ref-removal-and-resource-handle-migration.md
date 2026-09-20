# 移除 `eve::ref<T>` 与 `Object::ref_count`：资源句柄迁移方案

> 状态：**方案（尚未实现）**。本文件先固定所有权契约与分阶段计划，实现按 P1→P5 逐步落地，每阶段可独立构建与测试通过。

## 1. 目标与范围

把资源族（`Object` 继承体系）的共享所有权从**侵入式引用计数**（`ref<T>` + `Object::ref_count`）迁移到既有的
`RuntimeObjectRegistry<T, Tag>` / `Owned<T>` / `Borrowed<T>`（`src/engine/common/SquirrelOwnership.h`），
然后删除 `ref<T>` 与 `Object::ref_count`（含已死的 `Object::update` 链）。

范围之外（本方案不动）：ECS 组件存储、`RuntimeInstance<State>`、脚本自有对象的既有机制。

## 2. 现状清单（实测，非推测）

### 2.1 `Object` 继承体系

| 类型 | 位置 | 当前谁持有 |
|---|---|---|
| `Object` | `common/Object.h:18` | 侵入式计数 + `update` 链 |
| `Data` | `modules/filesystem/Data.h:8` | 由 `Filesystem` 模块工厂 `new` 出，交给脚本 |
| `FileData : Data` | `modules/filesystem/FileData.h:12` | `ref<FileData>`（绑定/动画） |
| `Resource : Object` | `common/Resource.h:40` | `ResourceManager::resources`（`map<string, ref<Resource>>`） |
| `ImageData/SoundData/ModelData/FontData : Resource` | 各自模块 | 缓存 + `ref<T>` |
| `Source` | `modules/audio/Source.h:30` | 已经是 `std::unique_ptr<Source>` |
| `Decoder` | `modules/audio/Decoder.h:22` | 模块内部 |
| `Model` | `modules/model3d/Model.h:13` | 模块内部 |

### 2.2 `ref<T>` 的生产用点（26 处，5 个 payload 类型）

按语义分三类：

1. **纯借用（调用期间活着即可）**——`AnimationClipBindings.cpp:33/94`、`AnimationCurveBindings.cpp:20`、
   `AnimationMotionBindings.cpp:52`、`Model3D.cpp:187`、`ModelLoader.cpp:119`、`UI.cpp:1104`、
   `GraphicsTexture.cpp:1135`、`webgpu/Graphics.cpp:2041`、`Resource.cpp:359`（加载期间 `keepAlive`）。
2. **生命周期延长（必须在 owner 卸载后仍存活）**——`AudioCapabilities.cpp:372` 的 `ActiveSource::data`
   （原注释：「Source 借用它，ref 保证跨缓存卸载存活」）、`Resource.h:71` 的资源依赖图
   （`getDependencies()/addDependency()`）、`HouseLayout.cpp:99/254` 的模型缓存成员、`Image.h:79-80` 返回类型。
3. **新建对象的所有权转移**——`Image::newCubeFaces` / `newVolumeLayers` 返回
   `std::vector<ref<ImageData>>`，元素是**新建**对象（不是共享缓存对象）。

另有 **25 处测试**用点（`test/animation_skinned.cpp` 8、`test/resource.cpp` 5、`test/RenderImageAudit.cpp` 4、
`test/particles_attach_*.cpp` 6 等）。

### 2.3 已经不需要引用计数的部分

`std::unique_ptr<Source>`（`AudioCapabilities.cpp:294/373/527/547`）、`std::unique_ptr<ImageData>`
（`UvPaintSession.h:50-52`）、`std::unique_ptr<Resource> replacement`（`Resource.cpp:317/371`）——
这些直接用 `Owned<T>` 即可，说明 `Object` 的计数并非处处必要。

### 2.4 死机制

`Object::setUpdate` / `getUpdate()` / `isDirty()` 与 `ref<T>::checkDirty()` 组成的热替换链**没有任何调用者**
（除定义处外全仓 grep 为空），`isDirty()` 恒为 false。本方案一并删除；若将来要做资源热替换，应按
「先 reload 协议后重建」重新设计，而不是靠指针自动转发。

### 2.5 `RuntimeObjectRegistry` 现状能力（迁移基础）

`emplace(Owned<T>) → Result<Ref>`、`resolve(Ref) → Borrowed<T>`、`erase(Ref)`、`isStale(Ref)`、`clear()`、
`ownerEpoch()`；底层 `detail::RuntimeSlotStore` 是**类型擦除**的（`Slot{generation, retired, object}` +
`freeSlots_` + `destroy_` 钩子），因此 pin 记账应加在这一层。
`Borrowed<T>` 的文档明确写着「Non-owning … intentionally has no reset-to-own or heap-management operation」，
**不具备寿命延长能力** —— 这正是本方案必须先补 `pin` 的原因。

## 3. 目标所有权模型

- **唯一 owner**：每个资源类型一个 `RuntimeObjectRegistry<Resource, Tag>`（异构缓存用 `Resource` 基类 +
  虚析构，具体类型由模块的类型标签/`typeid` 判定，与今天 `static_cast<ImageData*>(resource)` 等价）。
- **命名映射留在 `ResourceManager`**：`std::map<std::string, Ref>`（path → 句柄），`unload/unloadPath/clear/
  handlesPath/peek` 语义不变。
- **借用**：`resolve(ref) → Borrowed<T>`（ptr + epoch），不延长寿命；对象不存在/已被卸载时返回空。
- **寿命延长**：新增 `pin(ref) → Result<Pin<T, Tag>>`（move-only RAII）。pin 计数记在 slot 上：
  - `erase(ref)` 命中已 pin 的 slot：把 slot 标记 `retired`（此后 `resolve` 一律失败），
    **延迟销毁**到最后一个 pin 释放（保持今天「缓存卸载后声音继续播」的行为）；
  - `clear()` 同理对所有 slot 生效；
  - pin 释放时若 slot 已 retired 且 pin 计数归零 → 调用 `destroy_` 销毁载荷。
- **新建对象**：直接 `Owned<T>`（`unique_ptr`），不再进入引用计数体系。

## 4. 生命周期契约（仓库规范要求逐项声明）

| 项 | 约定 |
|---|---|
| 权威所有者 | registry；`ref_count` 与 `update` 链删除后不存在第二所有者 |
| stale 检测 | `RuntimeHandleRef{index, generation, ownerEpoch}`；generation 防槽位复用，epoch 防模块实例重载 |
| 销毁顺序 A（先删 holder 后删 owner） | pin 先释放，再 `erase` → 载荷在 `erase` 时销毁，恰好一次 |
| 销毁顺序 B（先删 owner 后删 holder） | `erase` 标记 retired 并延迟销毁；最后一个 pin 释放时销毁，恰好一次 |
| 模块卸载/热重载 | `ownerEpoch` 变化即所有旧 `Ref` stale；registry 重建，旧 epoch 的 pin 不得再解析 |
| 回调/脚本重入 | 仍不得在持有锁时调用未知回调（沿用现状：`ResourceManager` 的 mutex 是唯一闸门） |
| 线程亲和 | registry 本身无锁；`ResourceManager` 的 mutex 保证 registry 只被单线程使用（含异步 `runLoadJob` 路径） |

## 5. 分阶段计划

| 阶段 | 内容 | 完成判据 |
|---|---|---|
| **P1** | `detail::RuntimeSlotStore` 增加每槽 pin 计数与延迟销毁；`RuntimeObjectRegistry<T,Tag>::pin(Ref)` 与 `RuntimePin<T,Tag>`；`erase/clear` 交互 | 契约测试：pin 后解析、erase 后 pin 仍可解析、最后一个 pin 释放恰好销毁一次、槽位复用/epoch 不匹配、moved-from pin、对已 retired slot 重复 erase |
| **P2** | `ResourceManager` 用 registry 持有资源：`resources: map<string, Ref>`；`get/peek` 返回借用；异步加载期间对候选 pin | 现有 `resource.*` 测试通过；`unload/clear` 行为不变 |
| **P3** | 迁移 26 个生产用点：借用→`Borrowed`，寿命延长→`RuntimePin`，新建对象→`Owned`；依赖图改为「依赖方持有被依赖方的 pin」 | 逐模块构建 + 定向测试；`audio.*`（跨卸载播放）与 `animation.*` 回归 |
| **P4** | 删除 `ref<T>`、`Object::ref_count`、`update`/`setUpdate`/`getUpdate`/`isDirty`；`Object` 收缩为空基类或一并移除 | 全量构建通过；无 `ref<` 残留 |
| **P5** | 迁移 25 个测试用点；补契约测试（两种销毁顺序、失败注入：工厂抛异常、projector 抛异常、异步加载失败） | 定向 + 全量测试通过 |

## 6. 必须保留的行为（回归清单）

1. 音频：资源缓存被 `unload` 后，正在播放的 `Source` 仍能读到 `SoundData`（今天的 `ref` 语义）。
2. 资源依赖图：依赖方存活期间其依赖不得被销毁（`getDependencies()/addDependency()` 的语义）。
3. `ResourceManager` 公开行为：`get` 命中缓存返回同一对象、`peek` 不加载、`unloadPath`/`clear` 释放、`handlesPath` 判定不变。
4. 脚本侧：`newImageDataFromFile` 等返回的裸指针在解析当次调用内有效；脚本自有对象仍走
   `makeOwnedSquirrelInstance`（registry + release 钩子）。
5. 异步加载：`pending_`/`cv_`/`runLoadJob` 的 epoch 语义与失败路径不变。

## 7. 未决决策

- **D1（默认已选）**：pin 存在时 `unload` = 立即从命名映射移除 + 延迟销毁。备选：拒绝卸载（返回失败）或
  强制销毁（持有者变 stale）。选默认是因为它**精确保留今天的行为**。
- **D2**：`Object` 在 P4 后是否保留为空基类（供 `RTTI`/`static_cast` 分派），还是彻底删除并改用别的类型标识。
- **D3**：是否需要把依赖图提升为一等概念（显式 `Ref` 列表 + pin），还是让每个资源自己持有依赖 pin。

## 8. 风险

- 一次性删除 `ref_count` 会使所有持有者同时切换，任何漏改都是悬垂指针或二次释放 → 必须按 P1→P5 分批，
  每批构建 + 定向测试通过后再进入下一批。
- `ResourceManager` 的异步路径与 pin 的锁语义需要保持一致（registry 不自己加锁）。
- 本方案的收益主要是**机制统一与可判定的 stale 检测**，不是二进制体积；体积/编译时间的量化仍需独立 A/B 实测。
