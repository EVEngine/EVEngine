# 模板代码生成审计（2026-09-09）

本轮先穷举、再逐项分析，然后测量候选。初始清单为 97 个位置；第二轮新增两个共用 functor 模板，第三轮消除一个泛型 lambda，下面更新为当前 98 个位置，行号对应当前代码。包含上一轮 ModuleManager / Subject 修改。

## 范围与计数

扫描 Git 跟踪的 **896 个 C++ 文件**（包括头文件、实现、平台文件和 `.inc`），屏蔽注释、普通字符串和 raw string 后，找到 **94 个显式 `template<...>` 位置 + 4 个泛型 lambda**，共 **98 行当前审计项**，分布于 19 个文件。没有发现额外的 abbreviated function template 候选。

| 范围 | 显式 template 位置 | 泛型 lambda |
|---|---:|---:|
| 自有生产代码 | 57 | 3 |
| DTL 兼容层 | 36 | 0 |
| 测试代码 | 1 | 1 |
| 合计 | 94 | 4 |

位置数不等于模板实体数，更不等于生成函数数：其中有 concept、friend 声明、显式特化，`map<T,R>` 的声明和双层定义模板头也分别计入。类模板内部的非模板成员同样会实例化，见下方成员分析。未使用的模板通常不生成代码；同一类型跨 TU 的生成、不同类型的生成、不同 closure 的生成必须分别判断。

清单来自 `python3 scripts/inventory_templates.py`，完整声明保存在 [template-inventory-2026-09-09.json](template-inventory-2026-09-09.json)。这是词法审计，未展开宏、未模拟平台预处理，不能替代编译器实例化记录。

本工作树的外部子模块仍未初始化、third-party 未下载。第三轮在主检出目录找到已存在的依赖，并只读使用真实头文件；另在临时目录构建了 Poco Foundation/JSON 用于测试。清单范围仍是**本工作树全部跟踪 C++ 模板定义**；外部库内部的全部模板尚未逐项审计，不能标记为已完成。生产调用方的审查见后表。

## 每个位置的分析

“候选/高优先”是基于代码和调用点的判断，不是整项目体积实测排名。未找到仓库调用不代表外部使用者不会调用。

### `src/engine/common/Capability.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 70 | `provide<I>` | 生产 IGpuInfo、IMainThreadPost；测试 IGreeter | 保留：仅 name + void* 转发，注册表已在 .cpp。 |
| 76 | `query<I>` | System、Thread、ThreadPool 等查询服务 | 保留：单次 Raw 调用和类型转换。 |
| 82 | `revoke<I>` | 当前直接调用在 capability 测试 | 保留：薄转发，未见生产放大。 |
| 88 | `addListener<I>` | IPlatformEventSink、IAssetReloader 多模块注册 | 保留：容器操作已在 Raw 层。 |
| 93 | `removeListener<I>` | 当前直接调用在 capability 测试 | 保留：薄转发。 |
| 98 | `listenerCount<I>` | capability 测试 | 保留：薄转发。 |
| 103 | `listenerAt<I>` | capability 测试 | 保留：薄转发。 |
| 113 | `forEach<I,F>` | 生产 Event.cpp、HotReload.cpp；每处 lambda 为不同 F | 低优先：循环按 F 重复，但短；搬到 Raw 会增加间接调用，先测实际代码段。 |
| 125 | `forEachUntil<I,F>` | 生产 SDL Event.cpp 消费事件；另有测试 | 低优先：短路循环小；不可改变遍历期间注册/撤销的行为。 |

### `src/engine/common/Module.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 78 | `ModuleManager::getInstance<T>` | 大量 getModInst / 直接 getInstance 调用 | 上一轮已改：查表移至 .cpp，T 仅保留转换。 |
| 84 | `ModuleManager::requireInstance<T>` | requireModInst / 直接 requireInstance 调用 | 上一轮已改：共享查表和 factory 逻辑。 |

### `src/engine/common/Object.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 11 | `DerivedFromObject<T>` | ref<T> 的 static_assert | 保留：concept 不生成运行时代码。 |
| 35 | `friend class ref<T>` | Object 的友元声明 | 无需优化：声明本身不生成代码，不另算一个实现。 |
| 53 | `ref<T>` | 生产可见 Resource、ImageData 两种 T | 低优先：构造/复制/赋值/析构重复但较短；引用计数及替换语义敏感，不为两个类型贸然增加基类。 |

### `src/engine/devtools/DevTool.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 710 | `排序 lambda(auto a, auto b)` | rows 的单一 pair<string,ProfileEntry> | 保留：两个 auto 参数不代表所有类型笛卡尔积；此处一种组合。 |

### `src/engine/devtools/McpServer.cpp`（已消除的泛型位置）

原第 546 行 `addList(..., auto getId)` 产生四个闭包参数实例。第三轮改为普通 lambda + `std::string (Procgen::*)(int) const`，使用同一循环；当前已不计入模板位置数。详见第三轮验证记录。

### `src/modules/database/Database.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 28 | `Field<T>` | Mapping<Actor> 测试；read/write 回调依赖 T | 保留数据类型；回调包装可能较大，但现有生产 ORM 无多类型实证。 |
| 37 | `Mapping<T>` | test/database.cpp 的 Actor | 候选：toRow/fromRow 中查表与循环可共享，类型读写仍需 adapter；目前仅一个已知 T。 |
| 105 | `Connection::save<T>` | 未找到仓库内直接调用 | 保留：insert + toRow 薄包装；未用模板成员不会因此自动全部生成。 |
| 111 | `Connection::exportECS<Range,Mapper>` | test/database.cpp 一个 range + lambda | 第三轮否决独立 RAII 实现 TU：当前一个调用组合时，计入共享实现后 obj/text 均增大；保留原实现。 |
| 157 | `Repository<T>` | test/database.cpp 的 Repository<Actor> | 候选：createTable、主键查找、SQL 构造可移出；insert/all/find/update/remove/select 仍涉及 T，不能整类擦除。 |

### `src/modules/gpgpu/EcsGpu.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 17 | `componentFloatCount<Comp>` | pack/unpack 内部调用 | 保留：sizeof 和 static_assert 为编译期工作。 |
| 27 | `countViewEntities<Base,Comp>` | 未发现直接调用 | 保留：ECS View 类型决定迭代；无当前 obj 收益证据。 |
| 40 | `packViewComponent<Base,Comp>` | gpgpu 测试：GpuNode 的 Position、Velocity | 候选：buffer 分配/upload 可共享，View 解包仍需模板；GPU 热路径，避免逐元素虚调用。 |
| 68 | `unpackViewComponent<Base,Comp>` | gpgpu 测试：Position | 低优先：同上，只有一个可见直接类型组合。 |
| 88 | `runViewComponentSystem<Base,Comp>` | 未发现直接调用 | 保留：三步编排薄包装，未用不生成。 |

### `src/modules/graphics/RenderSystem3D.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 787 | `bindCam(auto* fx)` | 仅 AmbientOcclusion* 一个调用 | 保留：改明确类型不会减少多实例。 |

### `src/modules/graphics/SceneLinks.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 76 | `entityAlive<T>` | Renderable2D、Renderable3D、Camera3D | 保留：T* 到 ecs::Entity* 可能需要基类偏移；不能直接把 void* 当 Entity*。 |

### `src/modules/graphics/vulkan/Graphics.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 69 | `embeddedSpirv<T,N>` | 12 个不同 shader 数组；重复调用复用相同 T,N | 已测并否决动态 span：Debug text 增加 18.1%，O2 text 不变；保留原实现。 |
| 74 | `currentSlot<Slots>` | 7 种 slot 容器；均在同一 .cpp | 保留：resize/返回值确实依赖元素类型；擦除类型难以保留生命周期与引用类型。 |
| 80 | `releaseFrame2dBuffers<FrameBuffers>` | frame2dBuffers 元素及 offscreenBuffers，均 Frame2DBuffers | 低优先：仅一个实参类型，改非模板改善表达但不消除多实例。 |

### `src/modules/housegen/HouseComponentLibrary.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 167 | `排序 lambda(auto* a, auto* b)` | out 元素单一组件指针类型 | 保留：一次类型组合，比较器极短。 |

### `src/modules/ik/ChainSolver.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 17 | `ChainOptions<D>` | Solver2D/3D：D=2、3，字段未使用 D | 可简化为非模板 options（兼容别名可保留）；主要减类型信息，不是算法代码膨胀。 |
| 32 | `placeOnLine<D>` | 由 reachTarget<2/3> 调用 | 保留：维度参与向量运算，匿名命名空间；当前每个 D 仅一个调用 TU。 |
| 46 | `buildChain<D>` | 由 solveChain<2/3> 调用 | 保留：骨骼类型随 D 变化；vector 算法可分离但类型组合有限。 |
| 73 | `reachTarget<D>` | Solver2D.cpp、Solver3D.cpp 各一种 D | 保留热路径：不能仅因函数长就擦除维度。 |
| 109 | `applyPole<D>` | 同上；if constexpr D==3 | 保留：2D 分支编译期消失，改运行时 D 可能反而增加代码。 |
| 148 | `applyChainConstraints<D>` | 同上；调用上游角度约束 | 保留：类型依赖上游；可独立考虑去掉临时 vector 的运行时成本。 |
| 169 | `solveChain<D>` | 仅 Solver2D.cpp 实例化 2、Solver3D.cpp 实例化 3 | 当前不加 extern template：每维度已集中一个 TU；拆声明/实现可减解析负担，但不能宣称 obj 大降。 |

### `src/modules/physics/World.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 359 | `eventAt<Event>` | begin/end contact 与 impact 数组 | 保留：边界检查 + 索引，实际类型决定步长；体积小。 |

### `src/modules/procgen/dtl_compat/DTL/Utility/DrawJagged.hpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 27 | `DrawJagged` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 CRTP 基类；成员按实际使用实例化，不能按重载数估算 obj。 |
| 46 | `draw` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 57 | `redraw` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 68 | `drawAdd` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 79 | `drawOther` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 91 | `drawOther` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 103 | `drawOperator` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 111 | `drawOperatorArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 必须保留兼容修复：上游 createOperatorArray 引用缺失成员；不可为减模板直接删除。 |
| 124 | `drawFunction` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 135 | `drawAddOperator` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 146 | `drawArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 159 | `operator()` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 173 | `create` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 186 | `createArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 199 | `createOperator` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 212 | `createOperatorArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |

### `src/modules/procgen/dtl_compat/DTL/Utility/DrawJaggedRandom.hpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 28 | `DrawJaggedRandom` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 CRTP 基类；成员按实际使用实例化，不能按重载数估算 obj。 |
| 47 | `redraw` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 58 | `draw` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 69 | `drawAdd` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 80 | `drawOther` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 92 | `drawOther` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 104 | `drawOperator` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 110 | `drawOperatorArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 必须保留兼容修复：上游 createOperatorArray 引用缺失成员；不可为减模板直接删除。 |
| 124 | `drawFunction` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 135 | `drawAddOperator` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 146 | `drawArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留 MatrixWrapper→drawNormal 薄适配；模板参数改变矩阵/算子类型，底层算法是测量重点。 |
| 157 | `drawSEED` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 候选仅在多种 seed 类型实证后统一；本仓库调用固定种子路径，外部继承调用尚不可解析。 |
| 168 | `drawAddSEED` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 候选仅在多种 seed 类型实证后统一；本仓库调用固定种子路径，外部继承调用尚不可解析。 |
| 179 | `drawRandomEngine` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留引擎转发语义；不是任意 lambda 包装，需上游调用图再定。 |
| 190 | `drawAddRandomEngine` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留引擎转发语义；不是任意 lambda 包装，需上游调用图再定。 |
| 203 | `operator()` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 217 | `create` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 230 | `createArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 243 | `createOperator` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |
| 256 | `createOperatorArray` | BuiltinAlgorithms.cpp 的四类 uint_fast8_t DTL shape；各重载的间接可达性待上游源码 | 保留薄转发/完美转发；删除未用重载一般不减已生成 obj。 |

### `src/modules/rx/Rx.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 90 | `ssq::detail::pushValue<Value>` | Rx.cpp 本 TU 绑定 | 保留：显式特化已是固定 Value；不是任意 T 展开。 |
| 104 | `ssq::detail::popValue<Value>` | 同上 | 保留：类型 switch 已集中，依赖绑定库的特化可见顺序。 |

### `src/modules/rx/Rx.h`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 135 | `Observer<T>` | Value 生产；int / map 输出 string 测试 | 本轮关联优化：重复的终止通知转发闭包可按输出 T 共用；保留各 Observer 独立 stopped_。 |
| 180 | `detail::ForwardError<T>` | 七个 C++ 算子、六个脚本算子的输出类型 | 本轮新增并验证：共用 closure 类型，减少 std::function 包装实例；独立值拷贝 Observer。 |
| 186 | `detail::ForwardCompleted<T>` | 七个 C++ 算子、六个脚本算子的输出类型 | 本轮新增并验证：共用 closure 类型，减少 std::function 包装实例；独立值拷贝 Observer。 |
| 198 | `Observable<T>` | Value 生产；int/string 测试 | subscribe 重载较薄；析构/虚接口保留，不做整类显式实例化。 |
| 226 | `Observable<T>::map<R> 声明` | 声明，定义见 248/249 | 不单独算实现；T×R 组合需关注。 |
| 241 | `AnonymousObservable<T>` | C++ 算子与 Rx.cpp 脚本算子 | 类已按 T 共用；膨胀主要是构造它的各类 closure，不是再加一层 std::function。 |
| 253 | `Observable<T>::filter` | C++ int 测试；Value 脚本另有 doFilter | 本轮已优化：Error/Completed 转发与其他算子完全一致；pred/onNext 依赖算子，保留。 |
| 267 | `Observable<T>::map<R> 外层模板头` | int→string 测试；脚本 doMap 为 Value→Value | 本轮已优化：终止转发只需输出 R，避免跟随输入 T 和算子闭包增加。 |
| 268 | `Observable<T>::map<R> 内层模板头` | 与上一行同一个定义 | 重复模板头登记，不能计为第二份函数。 |
| 280 | `Observable<T>::take` | C++ int；脚本另有 doTake | 本轮已优化：终止转发可共用；remaining 每次订阅独立，不改。 |
| 301 | `Observable<T>::skip` | C++ int；脚本另有 doSkip | 本轮已优化：终止转发可共用；计数状态保留。 |
| 320 | `Observable<T>::first` | C++ int；脚本另有 doFirst | 本轮已优化：终止转发可共用；done 语义不改成 take(1) 的假设性合并。 |
| 338 | `Observable<T>::takeUntil` | int source/stopper 测试；Value 绑定 | 本轮已优化：仅 source 的普通终止转发可共用；stopper 持有 src 并 dispose，必须保留。 |
| 366 | `Observable<T>::distinctUntilChanged` | C++ int；脚本使用 Value::equals | 本轮已优化：共用终止转发；不要显式实例化 Observable<Value> 整类，Value 无 operator==。 |
| 389 | `Subject<T>` | 生产 Value；测试 int | 上一轮已优化通知分发；订阅/快照/锁/Slot 仍依赖 T，跨类型擦除涉及回调与线程语义。 |
| 490 | `BehaviorSubject<T>` | Value 生产、int 测试 | 保留：latest/replay 必须依赖 T；错误/完成方法短，未证实跨 TU 重复。 |
| 559 | `ReplaySubject<T>` | Value 生产、int 测试 | 保留：buffer<T> 回放和容量操作依赖 T；改 deque 属运行时设计而非本轮 obj 目标。 |
| 611 | `ReactiveProperty<T>` | Value 生产、int 测试 | 保留：BehaviorSubject 包装很薄；整类显式实例化可能生成未使用成员。 |

### `src/modules/sound/Sound.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 122 | `tryMake<T>` | Wave、Vorbis、FLAC、Mpg123、ModPlug 五种 decoder | 第三轮已简化：删除与 catch(...) 同行为的 typed catch；保留按 T 构造及异常清理，不引入 factory 间接调用。 |

### `test/ECS.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 222 | `registerCppEntityClassForScript<T>` | Node、Movable 两种 T | 仅测试候选：registry DFS 可抽出，类型选择/push 保留；不影响生产引擎 obj。 |

### `test/housegen.cpp`

| 行 | 模板 / 泛型 lambda | 可见使用范围 | 结论与原因 |
|---:|---|---|---|
| 137 | `find_if lambda(const auto& instance)` | layout.instances 单一元素类型 | 测试保留：单一组合。 |

## 类模板内部成员的补充检查

| 类 | 检查过的成员 | 判断 |
|---|---|---|
| ref<T> | 默认/raw/copy 构造、析构、两种赋值、转换、->、*、get、checkDirty | 计数/替换存在类型无关流程，但只有 Resource/ImageData 实证；先避免改变所有权行为。 |
| Field<T> | name/read/write/sqlType/primaryKey 与隐式生命周期 | 数据布局和 read/write 的 T 相关；无独立的大型算法。 |
| Mapping<T> | toRow、fromRow | map/string/variant 操作会随调用 TU 生成；共享 row 操作可测，但转换必须保留 T。 |
| Repository<T> | 构造、createTable、insert、all、find、update、remove、select、primaryKey | createTable SQL 与 primaryKey 搜索可分离；all/select 的 vector<T> 不能直接擦除。 |
| Observer<T> | next/error/completed、isStopped/setStopped、隐式复制/销毁 | error/completed 的状态属于每份 Observer；只捕获 onError/onCompleted 或共享整个状态会改变语义。 |
| Observable<T> | 虚析构、subscribe 虚方法及两种重载、七个算子 | subscribe 小；七个算子的 terminal closure 存在同语义不同类型的重复。 |
| AnonymousObservable<T> | 构造、subscribe、隐式析构 | 已经是 std::function 类型擦除容器，但每种传入 closure 仍会实例化 manager/invoker。 |
| Subject<T> | subscribe、三种 on 通知、hasObservers/count、emit、removeSlot、markStopped、析构 | 上一轮合并通知包装；Slot/vector/observer 生命周期仍类型相关。 |
| BehaviorSubject<T> | 构造、subscribe、getValue/setValue、onNext/onError/onCompleted、hasObservers | 重放及最新值依赖 T；没有大规模多类型调用实证。 |
| ReplaySubject<T> | 构造、subscribe、onNext/onError/onCompleted、hasObservers | buffer 的构造/复制/销毁依赖 T；terminal 薄包装不优先。 |
| ReactiveProperty<T> | 构造、get/set、两种 subscribe、asSubject/asObservable | 全部为轻量包装；强制整类实例化可能生成原先未用成员。 |
| ChainOptions<D> | 默认初始化/隐式复制、字段 | D 未参与字段；可消除冗余类型，但预计主要影响 debug 类型信息。 |
| DrawJagged / DrawJaggedRandom | 每个成员模板均在上表单列 | 不删除兼容 API；需完整 DTL 继承关系才可判断具体实例。 |

## 外部模板调用方与未覆盖边界

| 模板族 | 项目侧证据 | 后续处理 |
|---|---|---|
| simplesquirrel：addFunc/addVar/addClass、push/pop、VM/Table/Function 操作 | 多数模块的 expose；Rx 有 Value 特化；大量绑定签名和 lambda 会触发库模板 | 优先取得真实 TU 符号与 -ftime-trace，按绑定签名/closure 分类。不要凭调用数宣称它是最大 obj 来源；库源码当前缺失。 |
| ECS.hpp：View、Manager、registry、类型注册 | common/ECS.h、各模块实体/View、gpgpu bridge、ECS 测试 | 区分按组件组合必需的代码与可下沉到 IComponentManager 的共同遍历；当前无法审计子模块内部。 |
| Vulkan-Hpp / VKBuilder | graphics/vulkan/Graphics.cpp 等 | 对 Graphics.cpp 的模板符号、异常/RAII、绑定生成做实测；不能直接批量 extern 实例化系统库。 |
| GLM | mat/vec 运算、相机、材质、变换等 | 维度/标量类型多数有意义，内联对热路径重要；只处理确认重复且冷的包装。 |
| zeroerr | Assert.h 的 ASSERT 宏和测试框架 | 宏调用可间接生成模板，不在 94 个显式位置里；检查 Debug 的表达式分解/格式化符号，不能简单关闭断言改变 Debug 契约。 |
| DTL | BuiltinAlgorithms.cpp 四种 shape，统一 uint_fast8_t 矩阵 | 当前适配已集中；重载多不代表都生成，等待上游源码确定继承调用。 |
| STL：vector/map/unordered_map/string/function/shared_ptr/variant 等 | 分布于公共头、Rx、数据库、图形资源容器 | 以完整类型与生成 TU 计重，不能把所有 vector 或 function 当一个实例；优先移动业务层共同算法。 |

不以 PCH 作为 obj 缩小方案；PCH 主要减少解析成本。链接去重/LTO 也不等价于减少链接前 obj 总量。显式实例化须证明同一 specialization 在多 TU 重复，并处理导出/插件、成员约束、内联和未用成员问题。

## 本轮选择

1. **优先验证 Rx 终止通知闭包共用**：Rx.h 七个算子 + Rx.cpp 六个脚本算子均各自声明相同语义的 Error/Completed lambda。用按输出类型参数化的命名 functor，预计减少 std::function 的管理/调用/复制销毁实例；保留每个 functor 对 Observer 的独立值拷贝，map 用输出类型 R。
2. **SPIR-V 长度 N 去模板化作为小候选测量**：不能把 shader 数据字节下降算作代码下降；必须保留相同数组和调用点，用代码段及符号区分数据/调试信息。
3. **McpServer addList / 数据库事务与 SQL / decoder catch**：有共享机会，但前者只有四种闭包，后两者当前调用集中。没有依赖和真实 obj 前不扩大重构。
4. **暂缓 IK extern template、STL 批量 extern、ref 类型擦除和 DTL 重载删除**：现有实例化证据不支持收益，或语义/性能代价较大。

## 验证记录

环境：Apple Clang 21.0.0，arm64-apple-darwin，C++20，`-g`，分别 `-O0` / `-O2`。obj 为整个 Mach-O 文件，text 为 `size -m` 的 `(__TEXT, __text)`，不含 DWARF / shader 数据。

**本轮 Rx 独立增量**（基线为保留上一轮 Subject 优化、尚无 ForwardError/ForwardCompleted 的头文件快照）：

| 编译 | 算子样例 obj：前 → 后 | obj 变化 | text：前 → 后 | text 变化 |
|---|---:|---:|---:|---:|
| -O0 -g | 1,473,008 → 1,241,512 B | -15.7% | 146,592 → 128,032 B | -12.7% |
| -O2 -g | 1,547,808 → 1,253,776 B | -19.0% | 89,628 → 71,388 B | -20.4% |

Subject-only 对照的 text 两边保持 94,712 B（O0）/35,216 B（O2）：收益来自算子终止转发，并非再次改动 Subject。obj 中数百字节差异来自调试位置/字符串，不作为收益。

两边都编译、链接并运行 `scripts/tests/template_rx_operators.cpp`：filter/take/skip/first/takeUntil/distinct 的值序列，map 的 int→string 类型转换，取消订阅、两个独立订阅者、error/completed、stopper 取消上游、异常传播均通过。特意记录 first 后上游 completed 仍会再次回调的现有行为，防止共享状态无意改变语义；本轮不修 Rx 语义问题。

**SPIR-V 否决实验**：实际 12 个 shader 数组、相同外部 wrapper、只替换 helper 为动态 span（未改生产代码）。

| 编译 | 原 obj → span obj | 原 text → span text | 结论 |
|---|---:|---:|---|
| -O0 -g | 267,272 → 285,792 B | 7,156 → 8,452 B | obj +6.9%，text +18.1%，否决 |
| -O2 -g | 238,152 → 244,208 B | 832 → 832 B | 无机器码收益，否决 |

重跑实验：`python3 scripts/probe_spirv_codegen.py`。
重跑与 git 基线的整体对比：`python3 scripts/check_template_codegen.py --baseline 6f486652c`。该命令包含上一轮修改，不能把累计降幅当作本轮增量。单独评估下一轮可用 `--baseline-dir` 提供带 `common/Module.h` 和 `rx/Rx.h` 的修改前头文件快照。

完整引擎构建仍受 third-party 下载 DNS 失败及子模块未初始化限制；Squirrel 绑定 TU 未能完整编译/运行。没有 Windows COFF、全引擎累计 obj、最终可执行文件或运行性能的降幅结论。

## 第三轮：冷路径候选的验证与取舍

本轮保留 `McpServer.cpp` 的列表构造和 `Sound.cpp` 的重复 catch 简化；数据库代码没有变更。源码模板数少一个只是结果，是否保留由生成体积与行为验证决定。

依赖输入：主检出 `third-party` 的 git HEAD 为 `566e407`（本地只读源码；不是在此工作树下载）。Poco Foundation/JSON 单独在 `/tmp/ev-codegen-poco` 构建为静态库，没有改动主检出源码，也没有安装到系统。

### 真实头文件下的代码生成量

编译器仍为 Apple Clang 21，C++20、`-g`。下表按**提取的实际函数/调用块**生成目标文件，包含真实 Poco / Procgen 声明及真实 decoder 声明；不是完整 McpServer.cpp/Sound.cpp 的总量。

| 样例 | 选项 | obj：前 → 后 | text：前 → 后 | text 变化 |
|---|---|---:|---:|---:|
| 四类 recipe 列表构造 | -O0 | 1,123,240 → 1,117,664 B | 27,568 → 26,076 B | -5.4% |
| 四类 recipe 列表构造 | -O2 | 1,110,560 → 1,094,344 B | 12,180 → 10,676 B | -12.3% |
| 四种真实 decoder 的 tryMake | -O0 | 88,736 → 87,184 B | 1,300 → 1,092 B | -16.0% |
| 四种真实 decoder 的 tryMake | -O2 | 88,096 → 86,848 B | 560 → 560 B | 不变 |

decoder 样例包括 Wave/Vorbis/FLAC/ModPlug。Mpg123 缺少生成的配置头，未纳入体积样例；生产修改同样作用于其 tryMake 实例，但不能由此给出完整五类/整个 Sound.cpp 的降幅。O2 下移除 typed catch 的收益在异常处理元数据/符号等，不能说机器码也下降。

列表 getter 改为成员函数指针引入统一间接调用；这是请求时执行的冷路径，保留列表顺序及实际调用次数，未声称改善运行速度。普通 lambda 的四个调用共享同一种参数类型，避免原先四个不同 getter closure 实例化四份循环。

### 行为测试

`probe_cold_template_codegen.py` 从 git 基线与当前源码提取真实修改块，分别编译链接运行 O0/O2，两边都通过：

- recipe：链接真实 Poco 静态库，使用真实 Procgen 类声明和确定性的测试 getter 实现；检查四个字段、空列表、数量、顺序、带引号/换行/中文的 ID 和 getter 异常。测试 getter 替代真实 recipe 注册表，因此不是完整 MCP 服务集成测试。
- decoder：使用真实 medialoader Decoder/Exception 实现，受控派生类触发库异常、std 异常、非 std 异常及分配失败；检查 null 回退、构造失败释放和成功对象的销毁。不是音频格式解码质量测试。

复现（依赖目录可替换）：

```sh
python3 scripts/probe_cold_template_codegen.py \
  --dependency-checkout /path/to/populated/EVEngine \
  --poco-build /path/to/poco-build \
  --baseline 6f486652c
```

Poco 构建只启用 `ENABLE_FOUNDATION` / `ENABLE_JSON`，其余 `ENABLE_*` 关闭，`BUILD_SHARED_LIBS=OFF`、`CMAKE_BUILD_TYPE=Release`，构建目标 `JSON`。不传 `--poco-build` 时仍编译真实头文件样例并运行 decoder 测试，但明确跳过 recipe 运行测试。

### 否决：数据库独立事务 RAII 实现

试验把 exportECS 的 BEGIN/COMMIT/ROLLBACK 放入非模板 ExportTransaction：构造开始事务，commit 成功后标记，未提交时析构捕获回滚异常。Range/Mapper 只保留遍历和 insert，共享实现放到单独 .cpp。所有统计将**调用方和新增 .cpp 两份 obj/text 相加**，没有只比较调用方。

| 不同 Mapper 闭包数 | 选项 | 合计 obj：前 → 后 | 合计 text：前 → 后 |
|---:|---|---:|---:|
| 1 | -O0 | 540,096 → 786,504 B | 23,280 → 23,908 B |
| 4 | -O0 | 550,744 → 795,520 B | 26,508 → 26,368 B |
| 8 | -O0 | 564,992 → 807,560 B | 30,812 → 29,648 B |
| 1 | -O2 | 416,328 → 667,536 B | 4,416 → 4,556 B |
| 4 | -O2 | 460,136 → 698,544 B | 6,960 → 6,296 B |
| 8 | -O2 | 518,688 → 740,104 B | 10,352 → 8,616 B |

新 TU 重复携带 Database.h 的调试类型信息；当前真实仓库只有一处 exportECS 使用，连机器码也未获益，故在体积验证阶段撤回，未进入事务行为验证。这个否决只针对此次独立 TU/RAII 方案，不代表所有事务下沉方式都无收益。可在实际类型组合增加后，测量把实现合入已有 Database.cpp 的增量。

Repository SQL 构造当前同样只有 Actor 类型调用，暂未修改；下一步应取得完整 Database.cpp 目标文件后比较共享实现的真实增量，避免再次被独立 TU 的固定成本误导。

### 仍未完成的验证边界

本轮完成了真实依赖下的候选片段编译与行为测试；尚未完成整个引擎、完整 MCP/Sound 翻译单元或 Windows 构建。主检出依赖可读不等于当前工作树已具备完整配置、生成头和链接产物，前两轮的全项目体积限制仍成立。

## 2026-10-25：剩余候选复查，重点转向脚本绑定

本次核对工作树：仍基于 `6f486652c`，之前的修改都保留；生产模板位置清单与上一轮的 98 项一致。**本次只新增实验脚本与审计结果，没有新增生产代码改动。**

### 新证据：相同签名的无捕获 lambda 绑定

读取了主检出的真实 simplesquirrel 实现：`Class::addFunc<F>` / `Table::addFunc<F>` 经 `detail::make_function(F)` 转为 `std::function<Signature>`。签名相同不意味着 closure 类型相同；每个 lambda 表达式都可能带来一套构造、复制销毁、管理和调用包装。该库已有函数指针的 `function_traits` 特化，因此无捕获、非泛型 lambda 可以先转为函数指针，再让相同签名共用包装代码。

实验仅把调用处 `[](Args...) { ... }` 改为 `+[](Args...) { ... }`，保留函数体、参数与返回类型、绑定名称、调用数量和真实头文件。没有修改依赖源码。样例从当前 Network.cpp 和 Card.cpp 自动提取；Apple Clang 21、arm64、C++20、`-g`。

| 实际绑定片段 | 编译 | obj 前 → 后 | obj 减少 | text 前 → 后 |
|---|---|---:|---:|---:|
| Network：8 个整数写入绑定 | -O0 | 336,976 → 236,840 B | 29.7% | 25,348 → 15,992 B |
| Network：8 个整数写入绑定 | -O2 | 301,528 → 231,768 B | 23.1% | 7,284 → 6,008 B |
| Network：8 个整数读取绑定 | -O0 | 333,040 → 234,024 B | 29.7% | 24,448 → 15,280 B |
| Network：8 个整数读取绑定 | -O2 | 298,320 → 229,520 B | 23.1% | 7,112 → 5,848 B |
| Card：34 个 LayoutConfig 绑定 | -O0 | 779,688 → 352,464 B | 54.8% | 72,180 → 32,480 B |
| Card：34 个 LayoutConfig 绑定 | -O2 | 629,720 → 331,920 B | 47.3% | 18,032 → 12,580 B |

这不是整个 Network.cpp / Card.cpp 或全引擎降幅。测试仅完成目标文件编译，未运行 Squirrel VM；函数指针路径可能增加间接调用，不能把体积下降当作运行速度提升。原始 lambda 可能利用 std::function 小对象优化，因此也没有“消除了每个 lambda 的堆分配”的结论。

全 src/*.cpp 的词法扫描发现 4,049 处 `.addFunc(...)` 调用，247 处包含无捕获 lambda 的文本候选，其中 Card 101、Network 43、DevTool 22、EditorHost 14。该计数包含平台/配置条件，也不是不同签名数量或全部可转换数量，不能线性外推 247 处的总收益。

复现命令：

```sh
python3 scripts/probe_binding_codegen.py \
  --dependency-checkout /path/to/populated/EVEngine
```

可用 `--group writer` / `reader` / `cfgCls` 单独测量。脚本使用真实 NetStream、CardTypes、simplesquirrel 和 Squirrel 头文件，只在临时目录生成候选代码。

### 下一步优先级

| 优先级 | 位置 / 候选 | 证据与具体做法 | 还缺什么 |
|---|---|---|---|
| 1 | Card.cpp 的 LayoutConfig、CardData；Network.cpp 的 reader/writer | 上表已证明相同签名无捕获 lambda 先转函数指针可显著减小包装；先小范围应用，保留所有空指针检查与整数转换 | VM 真实 getter/setter、参数错误、异常传播、整数边界测试；频繁调用性能对比 |
| 2 | Rx.cpp::bindCommon | map/filter、take/skip、first/distinct 都有只转调 doXxx 的无捕获 lambda；可直接给显式 std::function 传 `&doXxx`，避免额外 closure 类型 | 单独测完整绑定片段；不要顺便合并 Observer 状态或改变订阅语义 |
| 3 | simplesquirrel 的 addMemberFunc/addFunc/bindVar | push name、userdata/closure 注册、slot 设置、错误构造的大部分流程与签名无关；把共用流程移入库的现有 .cpp，仅保留签名相关 callback/params/转换 | 需要依赖层改动、库构建及 VM 栈平衡/错误路径测试；统计所有受影响 obj 加上共享实现的总增量 |
| 4 | Module.h 的 registry 类型暴露 | 查找算法已移到 .cpp，但公共头仍暴露 unordered_map<string,ModuleInfo>；实现字段在本仓库只被 Module.cpp 使用，可评估隐藏实现以减调试类型信息 | 先测真实多个调用 TU 的 debug/type 段；涉及 class 布局及 protected 成员兼容性，不能作为无风险小改 |
| 5 | Rx 的 distinctUntilChanged 状态 | 当前分别 make_shared<T/Value> 和 make_shared<bool>；合并为一个状态结构可把该算子的每次订阅两次分配变成一次，并减少捕获的 shared_ptr 数量 | 主要首先是运行时分配优化；新状态类型可能生成新的控制块模板，obj 收益未证实，需单测与测量 |

对于依赖层的 `static` 自由函数模板，改为 `inline` 可能有利于链接后的合并，但仍会在各 TU 中生成 obj 内容，不把它单独当作解决 obj 膨胀的方案。要降低 obj 总量，应分离真正类型无关的共同实现。

下一批建议先做第 1、2 项：它们不需要重写容器或所有权设计，也不需要先改第三方库。第 3 项可能覆盖更多绑定，但收益与兼容成本都必须在库级真实构建后判断。数据库、IK 和 DTL 的之前结论保持，不因本次绑定收益大而重新批量套用 extern template。

## 2026-10-25：移除 Rx 转发 lambda，直接保存函数指针（已应用）

本轮取代上节“先转函数指针、仍保留 std::function”的建议。新建
`src/engine/common/ScriptBinding.h/.cpp`，Squirrel userdata 直接复制保存完整普通函数指针或成员函数指针；按指针类型共享调用 trampoline，将与类型无关的注册过程移到 `.cpp`。不同函数的指针值保存在各自 closure 的 userdata 中，因此同签名共享 trampoline 不会混淆调用目标。成员指针按 `sizeof(Pointer)` 保存，以 memcpy 读取，不假设其大小等于 void*。

Rx 的 map/filter/take/skip/first/distinctUntilChanged 直接绑定现有 doXxx 函数，删除六个纯转发 lambda 及显式 std::function；Subscription 的 dispose/isDisposed 直接绑定成员指针。subscribe 的参数适配、捕获回调和构造器保留现有接口。新路径仍需要 Squirrel native trampoline 和函数指针间接调用，但不再创建 std::function 对象、其堆包装或释放回调；这不代表消除 VM userdata 分配，也不宣称运行速度提升。

### 完整翻译单元测量

macOS arm64、本机 c++、C++20、`-D_SQ64 -g`；本轮前的 Rx.cpp 与当前 Rx.cpp 使用同一份当前 Rx.h。**后值包含 Rx.o 与新增 ScriptBinding.o 的总和**，不是全引擎大小或最终链接产物。第三方依赖未修改。

| 配置 | obj 前 → 后（B） | obj 净减少 | text 前 → 后（B） | text 净减少 |
|---|---:|---:|---:|---:|
| -O0 -g | 2,941,552 → 2,863,304 | 78,248（2.66%） | 294,100 → 279,488 | 14,612（4.97%） |
| -O2 -g | 2,604,704 → 2,574,848 | 29,856（1.15%） | 122,712 → 119,284 | 3,428（2.79%） |

新增公共实现单独占 obj 104,560 / 107,872 B、text 4,024 / 1,520 B（O0 / O2），已全部扣除，未隐藏固定成本。新模板仅负责签名解析、参数转换与调用；不把模板声明数量减少作为成功标准。

### 验证与复现

使用真实 Squirrel 和 simplesquirrel 源码构建独立测试，在旧接口和新接口分别运行相同脚本。覆盖同签名不同自由函数/成员方法、const 方法、void 返回、实例状态、vector/tuple 返回、参数数量/类型错误、C++ 异常到脚本传播、捕获回调旧接口、正常注册及失败注册的栈恢复；测试通过。完整 Rx.cpp 在 O0/O2 均编译通过。未完成全引擎集成运行或 Windows/MSVC 验证。

```sh
cmake -S scripts/tests/direct_binding -B /tmp/ev-direct-binding \
  -DDEPENDENCY_CHECKOUT=/path/to/populated/EVEngine \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/ev-direct-binding --target direct_binding_test -j4
ctest --test-dir /tmp/ev-direct-binding --output-on-failure
# baseline-source 是本轮改动之前保存的 Rx.cpp；不能直接拿 HEAD 混入前几轮优化。
python3 scripts/probe_direct_binding_codegen.py \
  --dependency-checkout /path/to/populated/EVEngine \
  --baseline-source /path/to/before/Rx.cpp
```

清单脚本现在同时扫描已跟踪及未忽略的新 C++ 文件，以免漏掉尚未提交的新模板。JSON 已刷新：902 个文件、111 个词法站点（含本轮新绑定和测试模板）；上文 98 站点是此前 tracked-only 快照，不可直接当作新增实例数。下一步应继续按实际绑定语义区分纯转发与必要适配，不能自动把所有无捕获 lambda 改写，也不扩展到尚未验证的 noexcept/ref-qualified 成员函数。

## 2026-10-26：Network 绑定逐项筛选与第二批落地

继续复用上一轮 ScriptBinding；本轮没有新增生产模板或公共 helper。

| NetStream 绑定组 | 分析 | 本轮处理 |
|---|---|---|
| writer.toString、reader.str、reader.ok | 仅转发成员方法，无额外转换/校验 | 删除 3 个 lambda，直接绑定成员指针 |
| writer.writeBool、writer.writeString、reader.bool | 已传成员指针，但旧接口仍创建 std::function | 改用直接绑定路径 |
| 8 个整数 writer、8 个整数 reader | 显式控制脚本整数与不同位宽/符号 C++ 整数之间转换 | 保留，未机械删除适配 |
| writer.writeBytes、reader.init | ByteData 解包及空指针判断 | 保留 |
| writer.size、reader.remaining/pos、reader.bytes | 返回类型或参数转换，bytes 还将 vector 转 string | 保留 |
| reader.initString | 看似纯转发，但 setBytes 借用参数字符串内存；ssq 参数转换创建临时 std::string，存在悬空地址风险 | 保留旧代码，未将此绑定计入安全迁移；所有权修复应单独处理 |
| writeF32/writeF64、f32/f64 | 本机独立测试旧接口 writeF64 浮点实参报 `expected: INSTANCE got: FLOAT`；当前依赖仅在 SQUSEDOUBLE 下提供 double popValue 特化 | 本轮整组保留；该失败受构建宏影响，不能推广到所有平台 |

### 体积：完整 Network.cpp

基线为本轮之前（HEAD）的 Network.cpp，前后都用当前公共头；macOS arm64、C++20、`-D_SQ64 -g`，依赖 checkout 的真实 Poco/zeroerr/Squirrel/simplesquirrel 头。O0/O2 均编译通过，仅出现已有 Module_REG override 警告。

| 配置 | Network.o 前 → 后（B） | 本轮 obj 减少 | text 前 → 后（B） |
|---|---:|---:|---:|
| -O0 -g | 5,720,808 → 5,563,832 | 156,976 | 525,292 → 509,644（减少 15,648） |
| -O2 -g | 4,675,160 → 4,554,256 | 120,904 | 163,532 → 159,252（减少 4,280） |

ScriptBinding.o 上轮已加入，未变化，无需在本轮增量重复收费。若独立评估 Network 并重新计入 helper 的**全部**成本，O0/O2 的 obj 仍净减少 52,416 / 13,032 B，text 净减少 11,624 / 2,760 B。不是全引擎或最终链接体积，也未测运行速度。

### 实际验证

新增 `scripts/tests/direct_binding/network.cpp`，在真实 VM 中分别运行旧绑定和直接绑定：检查 writer 的确切编码字节、bool/string/空字符串读取、越界后的 sticky error、错误参数数量/类型。Reader 从 C++ 生命周期明确的固定缓冲区初始化，避免让 initString 的已有悬空问题污染本次比较。通用绑定测试和 Network 测试均通过（2/2）。本轮未完成全引擎运行或 MSVC 验证。

复用上一节配置命令，构建两个目标后运行 CTest：

```sh
cmake --build /tmp/ev-direct-binding --target direct_binding_test network_binding_test -j4
ctest --test-dir /tmp/ev-direct-binding --output-on-failure
git show HEAD:src/modules/network/Network.cpp > /tmp/ev-network-before-direct.cpp
python3 scripts/probe_direct_binding_codegen.py --module network \
  --dependency-checkout /path/to/populated/EVEngine \
  --baseline-source /tmp/ev-network-before-direct.cpp
```

上述 HEAD 提取仅适用于本轮改动尚未提交时；提交后应使用本轮前的 revision。完整模板清单已重新生成，新增测试文件也纳入扫描。
