> zeroerr 0.4.0 更新：下文 compiled diagnostics 的测量记录针对旧版。
> 0.4.0 已提供轻量 String/StringBuffer 及独立编译的文本输出实现；
> 引擎已移除旧版诊断补丁、CompiledDiagnostics.cpp 和
> EVENGINE_ZEROERR_COMPILED_DIAGNOSTICS 选项，直接使用上游实现。
> 下文历史性能数字不代表 0.4.0 的测量结果。

# Windows 工作树共享 OBJ

`EVENGINE_COMPILER_CACHE=CCACHE_HARDLINK` 是显式启用的 Windows/Ninja/MSVC 模式，
要求 CMake >= 3.25、ccache >= 4.14.1，以及缓存和构建输出位于同一个支持硬链接的卷。
默认 `AUTO` 仍选择 sccache。不要让多个工作树共用同一个可写构建目录。

在每个工作树运行原有构建命令，将两个路径替换为本机路径：

```powershell
make build/win32-debug JOBS=8 CMAKE_EXTRA_ARGS="-DEVENGINE_COMPILER_CACHE=CCACHE_HARDLINK -DEVENGINE_CCACHE_EXECUTABLE=C:/tools/ccache.exe -DEVENGINE_CCACHE_DIR=C:/build-cache/EVEngine"
```

每个工作树保留自己的 Ninja 依赖数据库、链接输出和 PDB；缓存命中的相同 OBJ 与
缓存中的对象使用同一个 NTFS 文件记录。源码变化时 ccache 替换输出文件，不原地修改
旧硬链接。不要对缓存返回的 OBJ 运行原地补丁、strip 或其他修改工具。
先让一棵基线工作树完成构建、预热缓存，再让多个 agent 并发构建各自的工作树。
两个工作树同时遇到同一个冷缓存 key 时可能都启动编译，各保留一份物理 OBJ；
实测这种情况构建正确，但不会立即达到一份空间。后续命中同一缓存项再重建后
File ID 才合并。硬链接共享保证针对缓存命中，不是并发冷编译的全局对象去重。
硬链接不能跨卷，也不能减少链接后 EXE、DLL、PDB 的体积；文件属性显示的逻辑大小
仍会重复计数，可用 `fsutil file queryfileid` 和 `fsutil hardlink list` 检查实际共享。
缓存淘汰只删除缓存中的文件名；仍被工作树硬链接引用的内容要等对应构建输出清理后
才释放。缓存容量过小会降低后续工作树的共享命中率。
完整引擎的 OBJ 集合可能超过 ccache 默认 5 GiB 容量；按实际工作集使用
`ccache --dir=C:/build-cache/EVEngine --max-size=<容量>` 设置上限，避免第一棵树编译
结束时已淘汰前半部分、第二棵树又重新生成。容量上限不会预分配磁盘空间。

此模式使用 `/Z7`，关闭压缩和 direct mode，基于完整预处理输入校验头文件。
显式 `/FC` 防止 ccache 的相对编译参数把 CPP 的 `__FILE__` 变成构建目录相对路径。
`/Z7` 保留完整的基本原生调试能力，但没有降低调试数据等级。launcher 使用 CMake
实测的本地化 `/showIncludes` 前缀：中文 MSVC 的 `注意: 包含文件:` 必须让 ccache
识别，才能将依赖转换为当前构建目录下的相对路径。修复后的协议有单独 namespace，
不会命中先前错误记录的依赖输出。第三方依赖仍使用现有独立/预编译依赖配置。

## 调试必须配置源码映射

共享 `/Z7` 对象中的绝对源码路径来自首次编译它的工作树。每次配置和构建生成
`build/win32-debug/ccache/sourceFileMap.json`，将共享缓存记录的各个源码根映射到
当前工作树。目标单独构建也刷新映射。旧工作树的记录保留，以便映射旧缓存对象。
删除缓存时可以一起删除这些小型记录。

将该文件的 JSON 对象放进调试配置的 `sourceFileMap` 属性：

- VS Code C++ 的 `cppvsdbg` 配置支持此属性。
- Visual Studio 的 `launch.vs.json` 支持此属性；需要按源码设置断点时，使用
  `{ "editorPath": "当前工作树根", "useForBreakpoints": true }` 作为每个映射的值。

映射键值顺序应保留，较深的根目录在父目录之前。修改源码后应重建再调试，避免
调试二进制与编辑器源码不一致。映射不改变 OBJ 内容，也不改变 `__FILE__` 字符串；
含工作树绝对路径的 `__FILE__` 宏展开会产生不同的预处理输入，因此这些 TU 会正确
地分别缓存，无法跨工作树共享。实际 EVMath 实验中 4/5 个 OBJ 共享，但最大约 8.8 MiB
的 `Math.cpp.obj` 因 `Result.h` 断言的绝对路径而未共享。这限制了实际节省量：不能用
命中对象数推断节省字节数，也不能把本模式描述为所有 OBJ 只存一份。

需要消除这类差异时，可显式追加 `-DEVENGINE_CCACHE_TRIM_FILE_PATHS=ON`。
它使用 MSVC 的 **未公开文档参数** `/d1trimfile`，默认关闭。配置时编译并运行头文件
`__FILE__` 行为探针；当前编译器不能正确裁剪就报错，不自动退化。
只有该根目录参数的拼写被排除出缓存键，完整预处理内容仍参加校验，且使用独立
namespace。开启后运行时诊断和 PDB 的源码文件名变为源码根相对路径，映射文件
同时覆盖这些相对路径。源码相对资源路径要求以源码根作为运行工作目录，现有
CTest 配置已使用该目录。在 MSVC 19.51.36248、ccache 4.14.1 的实际 EVMath 实验中，
开启后 5/5 个 OBJ（合计约 10 MiB）共享 NTFS 文件记录。编译器升级后必须重跑验证。

参考：[ccache 手册](https://ccache.dev/manual/latest.html)、
[VS Code 调试配置](https://code.visualstudio.com/docs/cpp/launch-json-reference)、
[Visual Studio 原生调试配置](https://learn.microsoft.com/en-us/cpp/build/launch-vs-schema-reference-cpp?view=msvc-170)。

## 减少单个 OBJ

硬链接之外，正式构建还隐藏了 `ModuleManager` 的容器布局：公共头只保留私有
State 的唯一所有权，注册事务和脚本绑定实现访问同一份状态。构造和析构在实现文件
中定义，单例不能复制或移动；私有布局头不进入 SDK。

SimpleSquirrel 的成员函数 `addFunc` 重载直接在 VM userdata 中存储完整的成员函数
指针，省去 `std::mem_fn` 和 `std::function` 包装。const/非 const、void、引用、vector
和 tuple 返回值沿用原转换行为，C++ 异常仍转为 Squirrel 错误。捕获 lambda 与显式
`std::function` 仍使用原路径。生成的头文件来自实际选定的源码或预编译安装，不修改
共享依赖；SDK 安装同一套模板定义，避免插件与引擎混用不同模板实现。

`Value` 与 `Diagnostic` 的构造、析构、复制、移动等非模板实现集中在各自 CPP，
保留值语义、容器布局和原有 noexcept 保证。Diagnostic 工厂及详情追加也不再在
每个消费者中生成容器操作。Value 的类型化访问仍保留模板，直接访问 Storage 的
消费者仍可能实例化 variant；此方案减少重复生成，不保证消除所有长符号。
成员函数调用模板只接收成员指针类型，通过 traits 推导返回值、类与参数，避免把
完整签名作为多个模板参数重复传入。固定调试配置下应统计消费者与新增实现 OBJ
的总量，不能只统计缩小的单个文件。

普通函数指针和可转换为函数指针的无捕获 lambda 也使用 VM userdata 保存指针，
绑定调用模板只依赖函数签名，不再为每个 lambda 类型生成 std::function 管理器。
捕获 lambda 和其他 callable 继续使用上游 std::function 路径；参数检查、异常转换、
vector/tuple 返回值和 Squirrel 栈参数位置保持相同语义。生成的 class.hpp、table.hpp
与 direct_member.hpp 必须一起用于引擎和 SDK 插件，不能混用安装目录中的旧模板。

Value::array/object 的初始化列表重载在 Value.cpp 中创建容器；调用方只准备元素，
不再创建临时 vector/map。已有容器参数重载仍接受所有权转移。初始化列表元素按
原 STL 构造语义复制，对象重复键保留第一项，键遍历顺序保持确定性；这不改变 Value
的存储布局、JSON 格式或 Result 检查策略。GameplayControlJson 使用这一集中入口。

Value 的 vector/map 和 ECS 的两种 type_index map 使用显式模板实例化，把非模板
成员实现集中在 foundation；成员模板仍由调用方按需实例化。ECS 的生成头只追加
extern template 声明，上游独立 header-only 用法不变。SDK 安装同一生成头，消费者
必须沿用 SDK 的工具链、运行库和 STL 调试配置。实例化单元不拥有任何 Table、实体
或组件状态，世界隔离、延迟发布、继承和 generation 检查仍由原 ECS 实现负责。

Status::describe 的字符串拼接在 Status.cpp 中实现；Result 的失败文本在
Result.cpp 中同步构造和销毁，断言仍在 Result.h 中执行，保留原格式、源文件位置、
未观察检查和失败后终止策略。输入只在调用期间借用，不保留跨线程或跨帧引用。

Windows string 试验发现，普通 extern template 不减少样本调用方 OBJ；增加
DLL 导入可减少调用方实现，但与当前预编译 zeroerr 静态库中的同名 STL 定义发生
LNK2005 冲突。因此未接入 basic_string 集中导出，继续沿用依赖的原有 STL 配置。

此修改不改变 `/Od`、`/RTC1` 或 `/Z7`。生成头文件的目录必须在 MSVC 普通 `/I`
搜索路径的最前面，不能再追加重复 `/external:I`；ccache 改写相对路径后，重复外部
路径可能令旧安装头优先。验证应检查实际 include 输出和 OBJ，而不能只看配置列表。

## 重复验证

准备两个可丢弃的真实 Git 工作树。验证只写它们各自的忽略目录 `build/`，不要使用
正在工作的 agent 构建目录。在 MSVC 开发环境运行，例如：

```powershell
cmake/with-msvc.cmd pwsh -NoProfile -File scripts/verify_ccache_windows.ps1 -WorktreeA C:/work/a -WorktreeB C:/work/b -Ccache C:/tools/ccache.exe -EvidenceDir C:/work/results
```

脚本验证相同 OBJ 的 NTFS File ID、CPP 和公共头文件变化、两工作树同时进行
冷缓存/热缓存/不同源码构建（各 5 轮），并从 PDB 解析路径，通过 Windows 原生调试
API 在缓存命中的 B 可执行文件中实际命中 `main` 断点，使用生成映射打开 B 源码。
日志、断言结果和硬链接记录保存在 EvidenceDir。它是两 TU 的构建工具回归实验，
不代替全引擎 CI，也不包含 Visual Studio GUI 自动化。
追加 `-TrimFilePaths` 会开启私有参数模式，并额外验证 CPP 和公共头文件内的
`__FILE__` 展开都确实为源码根相对路径。真实 EVMath 还验证了两树全部 5 个 OBJ、实际 CPP/Vec3.h
修改后的机器码及 5 轮并发构建；不代表整个引擎的磁盘节省比例。
`concurrent-storage.json` 记录各轮物理文件身份；热缓存同时断言两棵树的两个 OBJ
共享 File ID，冷缓存只验证构建正确，允许重复物理输出。

### Diagnostic、Status 与 EditingValue 去重验证

Diagnostic 的无详情构造及 error/warning 使用独立短重载，在实现文件中创建空详情容器。
Status 的构造、析构、复制、移动和工厂函数，以及 EditingValue 的构造、析构、复制、
移动和比较，集中到各自 CPP；保持数据布局、深复制和原有异常属性。
EditingValue 的移动异常属性由 Storage 推导，兼容 MSVC map 的可能抛出行为。

同一 Windows Debug 配置下，全部 OBJ 从 9,800,103,933 字节降至 9,719,248,040 字节，
净减少 80,855,893 字节（0.825%），包含两个新增测试 OBJ；这是逻辑文件体积，
不是硬链接缓存的物理磁盘占用。2855 个配置内 OBJ 全部编译完成。
Foundation 与 EditingValue 的独立 DLL/静态组合测试通过，MSVC/GCC 布局和异常属性
前后相同，make check 通过。完整引擎链接仍受预编译 Box2D/box3d 库缺失阻挡。


### Result 断言处理集中实现

Result.h 中的条件仍由调用方求值一次，重处理通过 Foundation 的 Result.cpp 执行。
调用方传递表达式拼写、源码文件和行号；成功检查不构造诊断对象，失败才创建
zeroerr::AssertionData、分解布尔结果并打印上下文。expect 的失败描述也在 CPP 内生成。
zeroerr 的格式器使用花括号占位符，集中实现使用该格式打印上下文和 Status 描述。
断言启用策略仍由调用方的 ZEROERR_NO_ASSERT 决定，因此关闭断言的 Foundation DLL
被开启断言的消费者使用时，Result 的观察检查仍然有效。辅助函数只同步借用参数，
异常中的文件名借用调用方的静态字符串，其余输入不跨调用保留；不持锁或调用外部回调。
Result 数据布局、观察责任与异常属性保持原样。
本轮保留 Assert.h 的间接包含兼容性，不更改全局 EV_ASSERT 的表达式分解机制。

最终同配置全量 OBJ 从 9,719,248,040 字节降至 9,345,895,063 字节，净减少
373,352,977 字节（3.841%），包含一个新增回归测试 OBJ。2856 个配置内目标全部存在并
编译通过；分项 COFF 统计与整体文件体积相符。AssertionData 构造相关符号标记出现于
1177 个 OBJ，此前扫描约为 2081 个；标记出现不等于源码直接调用或运行时执行。
DLL 与静态组合各通过 25 个用例、155 个断言；关闭断言的 DLL 配合开启断言的调用方
通过 11 个用例、59 个断言。反向配置验证 expect 失败进入指定终止处理器而非抛出断言。
GCC 语法、改动行格式与 make check 均通过；未改变调试信息级别。完整引擎 DLL 链接
仍受缺失的预编译 Box2D/box3d 库阻挡，上述运行证据来自独立组合测试。

### EditingResult 与相机工厂集中实现

editing::failed<T> 的断言及诊断构造移到 EditingResult.cpp，模板只保留类型相关的
Result 包装；调用方传入断言策略和原始头文件位置。相机创建及断言移到
CameraFactory.cpp，ECS 表通过默认参数在调用方取得，避免 MSVC Debug 将类级导出
的内联包装导入 DLL 后读取错误的线程局部表槽位。现有无参调用继续有效；C++ 工厂
函数签名加入 Table&，消费者必须随引擎重新编译。布局、组件和销毁责任保持原样。
相机表参数只同步借用，在所属线程创建；不调用外部回调。
独立 DLL/静态探针不链接完整图形后端，使用测试专用宏抑制未使用图形类的强制导出；
DLL 消费方仍使用生产 dllimport 声明，运行真实工厂及 ECS 实现，不提供图形方法假实现。
正式 Windows Debug 同配置重建 617 个受影响目标后，全部 OBJ 从 9,345,895,063 字节
降至 9,265,686,079 字节，净减少 80,208,984 字节（0.858%），已包含两个新增实现与
两个测试 OBJ。2860 个配置内目标全部存在；COFF 分项总和与全部 OBJ 文件体积一致。
相机工厂仅在一个 OBJ 中有定义；zeroerr AssertionData 构造实际定义出现于 983 个
OBJ，其中引擎源码 47、测试 934、外部库 2。数字指编译产物定义，不指运行次数。
EditingResult DLL/静态组合各通过 4 个用例、33 个断言，相机组合各通过 1 个用例、
10 个断言；关闭断言的实现 DLL 与开启断言的消费者组合通过 2 个用例、10 个断言。
GCC 语法检查通过。完整引擎链接与运行仍未由这些独立 CPU 组合测试覆盖。
make check CI_BASE=4d4e88f04 通过，包含 232 个仓库脚本测试；未增加基线或豁免。
适用的 Result 观察责任、DLL 导出归属、同步借用与 ECS 表所有权规则均保留，无规则例外。

后续 zeroerr 集中实现试验仅保存在忽略目录：统一编译库与消费者后，16 个用例、
82 条断言前后相同且全部通过，覆盖表达式分解、位置、异常、计数及 fixture 注册。
相同五个消费者 TU 的 OBJ 从 7,808,206 字节降至 7,161,015 字节，减少 647,191 字节
（8.289%）；该小样本未计集中实现库的新增成本，不能当作全工程净收益。
同时将测试注册的函数指针转 std::function 放在 CPP 中，避免重复实例化包装。
GCC 语法通过。混用旧内联消费者库会产生 LNK2005，所以正式接入必须统一头文件和
库重建，并保留上游头文件模式；本轮未将该试验接入正式配置。

### zeroerr 编译诊断模式

EVENGINE_ZEROERR_COMPILED_DIAGNOSTICS 默认 ON，通过受版本控制的补丁给上游头文件
增加条件式声明；零散 ASSERT/CHECK 的条件求值、表达式分解和上下文计数仍在调用方。
AssertionData 的构造、结果存储、日志及抛出，ExprResult/TestCase 构造在 zeroerr 库
内集中实现。测试注册的函数指针转 std::function 也在库内完成。
引擎 TU 与 zeroerr 库统一使用 ZEROERR_COMPILED_DIAGNOSTICS；选项 OFF 保留原内联
实现，不编译集中实现源文件。未改变字段布局、异常策略、装饰器所有权或测试名称。
源码位置从调用方传入；辅助函数同步执行，不在构造时调用测试回调。
补丁在配置阶段幂等应用，发生源码漂移时报错；不得混用旧内联消费者库与新实现库。
本轮最终同配置全量 OBJ 从 9,265,686,079 字节降至 9,106,566,098 字节，净减少
159,119,981 字节（1.717%），已计入 1,017,918 字节的集中实现 OBJ 和 1,375,442 字节
的新测试 OBJ。2862 个配置内目标全部存在并编译通过；COFF 分类总和与整体体积相符。
AssertionData 构造/日志/结果存储和 ExprResult 构造的实际定义均仅出现在一个 OBJ；
此前分别覆盖约 983 个 OBJ。TestCase 构造相关定义从 938 个 OBJ 降到两个（包含隐式复制）。
Windows 与 GCC 的集中/内联两模式、共享/静态两路线共八组，各通过 16 个用例、
82 条断言。正式根项目 OFF 裁剪配置也编译通过 zeroerr，未配置集中实现 CPP 目标。
LF 与 CRLF 补丁输入都能幂等应用，真实缩进漂移仍被拒绝；为跨 Windows/WSL 工作树
仅在该 LF-blob 补丁调用上使用 Git 输入换行规范化，不放宽空白匹配。
make check CI_BASE=4d4e88f04 通过（232 个脚本测试），新增补丁回归与 Ruff 检查通过。
字段布局、断言异常、上下文计数、静态装饰器寿命及 CPP/DLL 所有权规则保留，无例外。
完整引擎 DLL 链接与图形运行仍未由这些 CPU 组合测试覆盖。

### zeroerr 生命周期集中实现

集中模式下，TestCase、ExprResult 和 AssertionData 的析构、复制/移动构造与赋值
由 CompiledDiagnostics.cpp 的 defaulted 实现负责；内联模式保留原来的隐式操作。
全部移动操作和析构保持不抛出，复制仍可能因字段分配或 callable 复制而抛出。
数据字段、异常基类与虚函数顺序保持原样；复制不转移装饰器、子用例和文件名指针
的所有权。操作在调用线程同步执行，保持原字段的外部同步要求，不增加内部锁。
调用方保存的 callable 及其捕获仍遵守原生命周期约定。
八组 Windows/GCC、集中/内联、共享/静态组合各通过 19 个用例、106 条断言；
MSVC 和 GCC 的尺寸、对齐、复制/移动/析构异常属性前后相同。测试覆盖深复制字符串、
浅复制借用指针、移动后复用、callable 捕获释放、异常复制和基类虚析构。
同配置正式 OBJ 构建完成后，2862 个配置目标无缺失；2869 个实际 OBJ 的总逻辑体积
从 9,106,566,098 降至 9,086,483,721 字节，净减少 20,082,377 字节（0.221%）。
其中已包含集中实现和扩展测试的成本，COFF 分类总和与整体体积相符。
TestCase 析构的实际定义从 939 个 OBJ 降至一个，ExprResult 和 AssertionData 析构
各从 985 个 OBJ 降至一个；不是以字符串提及数代替定义数。
make check CI_BASE=4d4e88f04 再次通过，232 个脚本测试全部通过。
本轮保留借用指针寿命、同步调用、DLL 所有权和异常属性规则，无架构例外。
完整引擎 DLL 链接及图形运行仍未由本轮 OBJ 编译与 CPU 组合测试证明。

### zeroerr 打印流与格式解析集中实现

集中模式把 Printer 的拥有/借用流构造、析构、重置和文本获取放在 zeroerr 库内；
类型相关 PrinterExt 和比较仍由调用方实例化，字段布局和原复制语义保持不变。
format 按原顺序打印所有参数，再调用库内解析函数；不改成仅在失败时打印，
也不改变自定义打印抛异常后停止后续参数处理的行为。解析保留原占位符语法及
参数数量前置条件，不保留传入指针。辅助函数同步执行，不增加锁或跨线程回调。
Windows/GCC、集中/内联、共享/静态八组各通过 22 个用例、122 条断言，覆盖流重置、
错误状态清空、借用流存活、输出拼接、打印顺序、打印异常和原占位符语法。
MSVC/GCC 集中与内联模式的 Printer 尺寸、对齐和异常属性一致。
正式 OBJ 构建的 2862 个配置目标全部存在并编译通过；2869 个实际 OBJ 从
9,086,483,721 降至 9,047,145,395 字节，净减少 39,338,326 字节（0.433%），
已计入新增测试和集中实现成本，COFF 分类总和一致。Printer 构造/析构实际定义
从 987 个 OBJ 降至一个，重置及文本获取实际定义也各只在集中实现 OBJ 内。
类型相关 format 模板仍由调用方实例化，但通用解析循环已集中；不以模板定义
数量未降作为无收益结论，也不把符号提及数作为重复机器码的证据。
本轮保留同步借用寿命、流所有权、异常语义和 DLL 组合规则，无架构例外；
完整引擎 DLL 链接及图形运行仍不由本轮 OBJ 编译和 CPU 组合测试证明。
make check CI_BASE=4d4e88f04 通过，232 个脚本测试全部通过；补丁 LF/CRLF 幂等应用
及真实源码漂移拒绝回归通过，git diff --check 通过。

### Result 头文件与诊断后端解耦

Result.h 和 EditingResult.h 不再包含 Assert.h；调用方的断言策略仍由构建定义
ZEROERR_NO_ASSERT 决定，字段布局、观察责任和失败检查语义保持原样。
真正使用 EV_ASSERT/EV_PARAM_CHECK 的引擎 CPP 显式包含 Assert.h，失败处理仍集中
在 Result.cpp。测试框架的既有 zeroerr/unittest.h 包装头显式包含断言头，测试
不再借业务头文件间接获得 CHECK；不把断言后端放回 Result 的公共包含面。
独立测试 TU 在加载测试框架前检查 Result 两个头文件不暴露断言宏，再验证实际
成功、无变化和失败状态。Windows/GCC、集中/内联、共享/静态八组各通过 23 个
用例、126 条断言；测试域清单验证通过。保留 Result 的 nodiscard、观察义务、
同步调用及 DLL 所有权规则，无架构例外。
去掉 zeroerr 的间接包含后，四个仅供断言使用的局部条件以 maybe_unused 标注，
避免关闭断言的严格消费方出现 C4189；不通过禁用警告绕过，也不改变条件行为。
独立消费方在没有 zeroerr 头文件搜索路径的情况下，开启和关闭断言两种策略均通过
MSVC /W4 /WX 编译、实际库链接和运行；析构异常属性符合各自的断言策略。
最终正式 OBJ 构建的 2863 个配置目标均存在并编译通过，2870 个实际 OBJ 从
9,047,145,395 降至 9,021,746,320 字节，净减少 25,399,075 字节（0.281%），
已计入新增独立头文件测试的 1,141,980 字节；COFF 分类总和与整体体积一致。
完整引擎 DLL 链接及图形运行仍不由本轮 OBJ 编译和 CPU 组合测试证明。

最终源码 make check CI_BASE=4d4e88f04 通过（232 个脚本测试）；最终八组组合测试
再次全部通过，各 23 个用例、126 条断言，git diff --check 通过。

### string-to-string unordered_map 后续候选实验

隔离 MSVC Debug 实验保留 /Z7，普通、静态集中、DLL 集中三条路线均验证复制、
修改、删除、reserve、rehash、移动、赋值与源容器独立性。四个消费 TU 的普通
OBJ 合计 3,928,801 字节；普通 extern template 静态路线连同新增实例化产物为
5,319,487 字节，消费方没有实际缩小，不能据此给静态路线添加额外实例化。
DLL 导入路线连同新增实例化产物为 3,578,675 字节，净减少 350,126 字节。
这只是隔离实验，不代表已接入正式配置，也不外推成全工程节省量；接入需复用
Foundation 的导出规则，并以正式 SHARED OBJ 和跨 DLL 合约测试验证净收益。

### Windows SHARED 字符串属性容器实例化

StringMapInstances.cpp 在 Foundation DLL 中导出 std::unordered_map<string,string>
实例，真实字符串属性容器字段的头文件及直接操作点中的跨组 Windows DLL 消费方
通过同一声明导入；只有返回类型声明的 Json.h 不强制实例化完整导入类。
Foundation 内部 TU 保留原标准库定义，避免同 DLL 内的导入/导出冲突；静态和
非 Windows 路线不生成额外实例化。普通 extern template 的无收益路径不接入。
容器类型、字段布局、聚合初始化和每个容器的状态/分配器/迭代器所有权保留；
只集中代码，不增加全局容器或内部锁。参与 DLL 使用相同 CRT 和迭代器调试配置。
MSVC Debug 原始和导入容器均为 80 字节、8 字节对齐，移动构造仍可能抛异常，
其余复制/移动/析构异常属性也一致。真实生产源文件的八组 Windows/GCC、
集中/内联诊断、共享/静态组合各通过 26 个用例、146 条断言，覆盖堆字符串复制、
移动后复用、节点转移、merge、迭代、rehash 和四线程独立容器操作。
保留 DLL 所有权、同步寿命及标准容器异常规则，无架构例外；测试域清单通过。


最终字符串属性映射正式重建包含 2865 个配置 OBJ、2872 个实际 OBJ，无缺失。
总量从 9,021,746,320 降至 8,915,516,919 字节，计入实例化与测试后净减少
106,229,401 字节（1.177%）。后续移除 Light.h、RenderSystem.h 和
RenderSystem3D.h 的残留 zeroerr 头文件包含，实际断言 CPP 显式包含
common/Assert.h；最终 OBJ 编译通过，总量进一步降至 8,913,940,949 字节，
该步净减少 1,575,970 字节。源码检查结果独立于 OBJ 编译结果记录。

### vector<string> 实例化的混合链接限制

八个 MSVC Debug 消费 TU 的隔离实验中，普通路线为 5,309,362 字节，
DLL 导入路线连同新增实例化 OBJ 为 4,698,948 字节；普通静态 extern
路线增长到 6,221,911 字节。三条路线的复制、移动后复用、赋值、扩容、
插入、删除、交换与迭代检查均通过，容器尺寸和对齐保持 32/8。
但接入真实生产源文件的跨 DLL 组合时，zeroerr 静态库中的 vector<string>
实例与 Foundation 导入库发生 LNK2005/LNK1169；该组合证明仅测试同构
消费者不足以验证混合库。已撤回该轮生产导入声明、实例化源文件和测试，
不使用强制忽略重复定义，不把隔离收益计入正式工程节省量。


### 测试注册的装饰器临时容器实验

TEST_CASE 的集中注册工厂仍以 vector<Decorator*> 按值接收参数，所以空的
装饰器列表也会在调用方实例化 vector 构造/析构。隔离实验在根拥有的测试包装
头中把集中模式工厂切换为 initializer_list 参数，后端同步复制指针列表到
TestCase 持有的 vector；不保留 initializer_list 的存储，也不调用测试回调。
普通非集中模式保留上游宏。共享/静态组合各通过 27 个用例、155 条断言，
另有一个预期跳过的用例；验证装饰器指针顺序和 metadata 复制后的独立性。
六个原有共享消费 OBJ 合计减少 97,586 字节，后端合入已有诊断 CPP 后该 OBJ
增长 37,593 字节；隔离净收益 59,993 字节。六个消费 OBJ 中实际 vector
构造/析构定义各从六份降为一份，剩余一份来自主动操作装饰器 vector 的测试。
这仍是隔离证据，尚未接入生产注册路径，不外推成完整工程收益。


装饰器注册优化已接入根拥有的包装头和现有诊断 CPP，不新增标准库 DLL 实例。
Windows/GCC 的集中与内联、共享与静态八组组合测试通过：集中模式各 28 个
用例、156 条断言及一个预期跳过，内联模式各 27 个用例、147 条断言。
最终 2866 个配置 OBJ 无缺失，2873 个实际 OBJ 从 8,913,940,705 降至
8,896,084,870 字节，计入新增测试后净减少 17,855,835 字节（0.200%）。
COFF 分类总和匹配整体。装饰器 vector 构造/析构实际定义各剩三个 OBJ，
其中一个是主动操作 vector 的测试，另外两个是框架后端；注册桥仅有一个定义。
后续字符串视图实验让后端同步复制名称和文件名，六个消费 OBJ 再减少
14,891 字节，已有后端 OBJ 增长 7,333 字节；尚待正式构建测量，不外推收益。


名称与文件名的注册桥改为 string_view 输入并同步复制成持有字符串后，
Windows/GCC 八组组合验证输入修改、临时字符串销毁和嵌入 NUL 的完整保存；
集中模式各 29 个用例、165 条断言及一个预期跳过，内联模式各 27 个用例、
147 条断言。最终正式 OBJ 构建通过，无配置目标缺失，2873 个实际 OBJ 从
8,896,084,870 降至 8,891,862,254 字节，计入新增断言后的净减少为
4,222,616 字节（0.0475%）。完整源码检查与 OBJ 编译分别取证。

Diagnostic vector 候选的真实构造/析构定义分别存在于 282/339 个 OBJ，
并非只有 23 个显式写出该类型的文件；Status 的花括号参数也会实例化容器。
隔离共享组合的复制、移动、赋值、扩容和元素独立性测试通过，但六个消费 OBJ
只减少 31,367 字节，实例化后端增长 448,493 字节。现接入 Foundation 的
Diagnostic.cpp，由正式完整重建判断净收益；静态/非 Windows 不增加实例。
容器与元素仍各自拥有状态，同一 CRT/迭代器配置，标准异常和同步规则保留。


Diagnostic vector 的正式完整重建通过，2867 个配置 OBJ 无缺失，2874 个实际
OBJ 从 8,891,862,254 降至 8,877,592,539 字节，包含新增测试后的净减少为
14,269,715 字节（0.1605%）。Windows/GCC 八组组合通过：集中诊断各 32 个
用例、179 条断言及一个预期跳过，内联诊断各 30 个用例、161 条断言。

editing 标识适配器的生命周期集中实验保留 UuidIdState 的聚合属性，复制、
移动、赋值、兼容文本/规范 UUID 投影和开放自定义标签通过共享/静态运行验证。
真实跨组构造定义较多的五种标签是 RuleIdTag（238 个 OBJ）、TargetIdTag
（216）、StableIdTag（141）、ObjectIdTag（138）和 CapabilityIdTag（93）。
实例化仅由 editing 模块的 Platform 组拥有，普通静态/非 Windows 不生成
额外实例；其他标签保留泛型路径，领域标签不可隐式互换，存储和持久投影不变。
已接入这五种常用标签，正式净收益仍由完整重建验证，不外推隔离实验数据。


五种 editing ID 的正式完整重建通过：2869 个配置 OBJ 无缺失，实际 OBJ
从 8,877,592,539 降至 8,873,033,792 字节，含实例化实现和新增测试，
净减少 4,558,747 字节（4.35 MiB）。

测试窗口夹具的 openGfxWindow/openHeadlessGfx 实际定义分别重复于 44/20
个 OBJ。按五个消费域分别编译同一个实现反而增加 961,134 字节，原因是
五份 OBJ 各自携带图形头文件的调试信息。最终采用测试专用静态库，只编译
一份 GraphicsWindowFixtures.cpp，消费域仍由 test_domains.py 的共享源
映射决定；移动平台保留原本单一测试产物的源文件路径。
完整重建的 2870 个配置 OBJ 无缺失，实际总量从 8,873,033,792 降至
8,868,755,714 字节，计入唯一新增实现后净减少 4,278,078 字节（4.08 MiB）。
COFF 符号表确认两个函数均仅有一个定义 OBJ。默认参数、同步调用、模块
单例归属和现有 REQUIRE 行为保持不变，不增加进程状态；Windows 的双消费
文件链接、环境覆盖、失败注入和窗口清理隔离验证通过，图形 provider 为
可控替身，此证据不能替代实际 GPU 或完整引擎链接运行。


编辑器 Value 的 Windows DLL vector/map 非模板操作现由 Platform 的
EditingValue.cpp 显式实例化，消费方 extern dllimport；静态/非 Windows
保持原路径。保留 Value/Object/Array 的类型、布局、递归独立所有权、
确定性 JSON 和成员模板路径，不引入全局容器、锁或跨域状态。
完整重建通过，2870 个配置 OBJ 无缺失，总量从 8,868,755,714 降至
8,821,538,575 字节，净减少 47,217,139 字节（45.03 MiB，0.5324%）。
map 构造/析构跨组定义由 133/69 个 OBJ 降至 0；Platform 内仍保留
三个定义 OBJ，不将链接组内的正常模板生成误报为跨组共享失败。
Windows/GCC 的共享/静态及诊断 ON/OFF 八组实际链接和 CPU 用例通过：
集中诊断各 36 用例、214 断言、一个预期跳过；内联诊断各 34 用例、
196 断言，无跳过。新增覆盖包含编辑器 Value 深拷贝、移动、源复用、
JSON 往返、错误 JSON 和预算限制；这些证据不表示完整引擎 GPU 运行通过。


CapabilityId vector 复用 Platform 的 EditingIdInstances.cpp 实例化，
Windows DLL 消费者导入非模板容器操作。64 个长文本标识的独立复制、
移动后源复用、扩容、删除和 UUID 投影通过 Windows/GCC 八组验证。
完整重建的 2870 个配置 OBJ 无缺失，总量从 8,821,538,575 降至
8,816,974,693 字节，净减少 4,563,882 字节（4.35 MiB，0.0517%）。
SelectionItem vector 的双消费文件隔离 DLL 验证同样通过，现接入相同
Platform 实例化文件，保留聚合、类型、布局和独立字符串/标识所有权。
其正式净收益仍待完整重建，隔离实验不代表引擎整体运行验证。


SelectionItem vector 的正式完整重建通过，2870 个配置 OBJ 无缺失，
总量从 8,816,974,693 降至 8,811,848,894 字节，净减少 5,125,799
字节（4.89 MiB，0.0581%）。Windows/GCC 八组链接和 CPU 验证通过：
集中诊断各 38 用例、229 断言、一个预期跳过；内联诊断各 36 用例、
211 断言。布局、聚合属性和领域标识语义保留。
GridObject vector 隔离 DLL 实验使用实际 Grid2D 实现，验证对象拷贝、
元数据、坐标/资源标识/朝向/标记和容器生命周期通过，现接入 Domains
组的 Grid2D.cpp。正式净收益待完整重建，不外推隔离实验。


GridObject vector 正式完整重建判定为失败的体积优化：2871 个配置 OBJ
无缺失，总量从 8,811,848,894 增至 8,813,569,222 字节，增长
1,720,328 字节；扣除新增测试 OBJ 的 1,403,825 字节，生产/既有测试
仍增长 316,503 字节。隔离运行通过并不能证明体积收益，已撤回本轮
实例化和新增测试，保留先前 SelectionItem 等已验证有效的优化。
实验日志与测量数据保存在忽略的 build/obj-size-trial，恢复后完整重建
另行验证，不将这轮增长计为最终收益。


DiagnosticDetails（vector<pair<string,string>>）的 DLL 实例化被撤回。
Windows/GCC 八组 CPU 验证和零错误静态库混用通过，但进一步检查预编译
第三方库发现 Assimp、PocoFoundation 和 PocoNet 中合计 112 条同类型
容器实际外部定义；真实 Poco::URI 调用复现 LNK2005/LNK1169。
因此不将 CPU 夹具通过外推为完整引擎链接通过，也不使用 /FORCE:MULTIPLE。
已取消本轮尚未完成的正式编译/门禁，保留日志和 COFF 证据，撤回实例化
及新增测试；部分编译 OBJ 不作为净收益测量。恢复后的完整重建另行验证。


撤回 DiagnosticDetails 标准容器导出后，完整重建的 2870 个配置 OBJ
无缺失，总量精确恢复为 8,811,848,894 字节；真实 Poco URI 链接/运行
恢复通过。该实验没有计入体积收益。
ruleDiagnostic 的原实现实际重复于 85 个 OBJ，其中 81 个跨组；
现迁入已有 EditingResult.cpp，由 Platform 导出该已有函数。参数和
返回类型、字段/RuleId 投影、所有权和调用方式保持不变，无标准容器
DLL 导出。Windows/GCC 八组 CPU 组合通过，且直接链接实际 EditingResult
实现的共享/静态程序与预编译 Poco URI 链接和运行通过。正式净收益
仍待完整重建，不外推隔离实验数据。


ruleDiagnostic 的正式完整重建通过：2870 个配置 OBJ 无缺失，总量从
8,811,848,894 降至 8,798,749,589 字节，含新增测试后的净减少为
13,099,305 字节（12.49 MiB，0.1487%）。真实定义从 85 个 OBJ 降至
一个 Platform OBJ，跨组重复定义归零。Windows/GCC 八组 CPU 组合通过，
实际实现的 Windows 共享/静态消费者与预编译 Poco URI 链接和运行通过；
这些证据不代替完整引擎 GPU 运行。所有权、线程、成本和回调契约已说明，
参数/返回类型和持久诊断字段不变，无架构规则例外。


EntityHandle vector 的隔离 DLL 验证通过，现由已有 EcsContainerInstances.cpp
集中 Windows DLL 非模板操作；根维护的 generated ECS 覆盖层声明导入，
不修改 upstream ECS.hpp 或静态/非 Windows vector 路径。复用既有 ECS
容器测试，覆盖 Table 隔离、延迟继承组件、句柄字段/生成号、复制移动、
扩容删除、实体销毁及 ID 复用后的 stale 检测。Table 和实体的权威状态
仍各由原 Table 所有，vector 只保存原借用值；不改变类型/布局/协议。
Windows 四组组合已通过，GCC 和正式整体净收益另行验证。


EntityHandle vector 正式完整重建通过，2870 个配置 OBJ 无缺失，总量从
8,798,749,589 降至 8,794,021,949 字节，含既有测试扩展后的净减少为
4,727,640 字节（4.51 MiB，0.0537%）。构造/析构定义各集中到一个
Foundation OBJ，字段和生成号/stale 协议保持原样。Windows/GCC 八组
实际组合通过：集中诊断各 42 用例、263 断言、一个预期跳过，内联诊断
各 40 用例、245 断言；无架构规则例外。完整引擎 GPU 运行未由这些
CPU 证据覆盖。
EditingValue::getIf 的下一实验在 Windows 共享/静态隔离程序中通过，
覆盖七种值类型、const/可变即时借用、类型不匹配和指针一致性；正式
OBJ 中该方法仍有 577 条定义，分布于 165 个 OBJ。尚未接入正式源码。


现接入 EditingValue 七种 getIf 的 const/可变 Windows DLL 显式实例，
由现有 EditingValue.cpp 拥有；泛型定义仍保留，静态/非 Windows 路径
不增加实例。Storage、类型/布局和借用有效期不变，返回指针仍直接指向
调用者拥有的 Value，无临时代理、共享状态、分配或回调。Windows 四组
组合通过七种类型、类型不匹配、可变写入和 const/可变指针一致性测试；
GCC、完整 OBJ 净收益和最新源码门禁另行取证。


EditingValue::getIf 正式完整重建通过，2870 个配置 OBJ 无缺失，总量从
8,794,021,949 降至 8,782,664,388 字节，含测试扩展后净减少
11,357,561 字节（10.83 MiB，0.1292%）。577 条真实定义由 165 个 OBJ
集中到一个 Platform OBJ 的 14 个 const/可变实例，跨组重复归零。
Windows/GCC 八组组合通过：集中诊断各 43 用例、274 断言、一个预期
跳过；内联诊断各 41 用例、256 断言。类型、Storage 和借用语义不变。

PCH 调试类型共享仅作隔离实验，未接入正式配置。MSVC /Z7、相同七个
消费源文件及现有引擎静态实现，普通 OBJ 合计 10,353,615 字节，PCH
路线包含创建 OBJ 后为 6,568,687 字节；debug$T 合计由 4,432,260 降至
355,764 字节，两种产物运行一致。但新增 PCH 为 169,476,096 字节，
OBJ+PCH 合计达 176,044,783 字节，因此小规模使用会增加磁盘占用。
依据编译定义/选项/include 路径完全一致分组，当前有 54 种编译环境，
最大一组 933 个 C++ TU；是否能摊薄 PCH 成本必须在实际规模下测量，
还未证明跨工作树 ccache、修改重建、并发与调试路径兼容，不宣称可直接
投入正式构建。参考微软 /Yd 和 /Z7 文档：
https://learn.microsoft.com/en-us/cpp/build/reference/yd-place-debug-information-in-object-file?view=msvc-170
https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-170


PCH 实际源文件试验继续保持隔离。首次手工响应文件将 Flags 放在 Includes
前，改变了 external include 搜索顺序并选中 upstream zeroerr 头，已按
正式 Defines→Includes→Flags 顺序修正；未修改正式源文件。扩展测试时，
现有 Result 无断言后端隔离检查拒绝了全局注入 zeroerr 的 PCH，因而收窄
为 common/Result、Value、Identity 和 ECS，不加入跳过/白名单。
300 个实际源文件全部编译通过；普通 OBJ 为 1,008,323,623 字节，共享
PCH 消费 OBJ 816,223,957，创建 OBJ 1,602,222，PCH 169,410,560。
计入创建 OBJ 后，OBJ 减少 190,497,444 字节；计入 PCH 后总磁盘净减少
21,086,884 字节（20.11 MiB）。这是隔离编译级证据，仍不代表正式
链接/运行、跨工作树缓存、并发、调试路径和修改重建已经通过。
最大同环境组 933 个 OBJ 对应 930 个独立源文件，已启动全组隔离编译，
复用已完成的 300 个消费对象；不按样本均值宣称全组收益。
当前正式代码的 round29 make check CI_BASE=4d4e88f04 已退出 0，源码
质量门禁通过；PCH 仍未接入正式构建配置。


PCH 同编译环境全组隔离测量完成，930 个独立实际源文件均编译通过。
原 OBJ 合计 3,050,797,301 字节，消费 OBJ 2,465,847,513，创建 OBJ
1,602,222，PCH 169,410,560。计入创建 OBJ 后，OBJ 净减少
583,347,566 字节（556.32 MiB）；再计入 PCH 文件，总磁盘净减少
413,937,006 字节（394.76 MiB）。不将三个重复源对象额外计入收益。
这是全组实测而非样本外推，仍是隔离构建，未修改正式配置。
公共 PCH 的 Windows 共享/静态与诊断 ON/OFF 四组真实 CPU 链接运行
通过，分别 43 用例/274 断言/一个预期跳过，41 用例/256 断言。
PCH 创建对象会引用 canonical Value 的内联工厂，因而隔离 Foundation
补齐正式 Foundation 已有的 Value.cpp/Json.cpp 实现；没有加入伪实现
或新的正式模块依赖。现有 Result 无断言后端隔离检查保留并通过。
跨工作树 ccache hardlink、cpp/公共头变更重建、并发和调试路径仍待验证，
不能仅凭此 CPU/体积证据接入正式 PCH 配置。


两个独立 Windows 源码/构建目录的 PCH-cache 隔离试验均链接和运行通过，
但正式 launcher 策略下首次 6 次编译仅 2 次可缓存、4 次因 PCH 不可缓存，
缓存命中为 0；两个目录的消费 OBJ/PCH NTFS File ID 不同。日志明确要求
使用 time_macros sloppiness。未修改正式 launcher 或缓存策略。
仅在不含时间宏的最小隔离语料中临时设置 time_macros 后，PCH 消费编译
可以缓存；再次编译同目录两个消费源均得到 preprocessed_cache_hit。
但两个目录之间仍是不同 File ID，不能宣称跨目录 hardlink 复用成立。
这两个目录不是正式 Git 工作树；真实跨工作树、源码/公共头修改、并发和
调试来源验证尚未完成。PCH 体积收益不能代替缓存正确性，正式配置仍不启用。
相关日志为 round30-pch-cache-*，缓存完全位于忽略的实验目录中。


PCH 跨目录继续排查：两个目录生成的 PCH SHA256 不同，仅临时启用
时间宏选项并不能让它们共享。对内容 SHA256 相同的头做同 PCH 复用
试验，A 消费成功，B 重读 pragma-once detail.hpp 时复现 C2011 类型
重定义；PCH 保留了 A 的头文件身份。不能忽略 PCH 校验或直接复用二进制
跨过此问题，正式配置仍不启用 PCH。全部试验保存在忽略目录。
下一源码候选为 common Value::getIf：611 条真实定义分布于 198 个 OBJ，
已启动 const/可变 noexcept 实例的隔离共享/静态组合验证；借用和 JSON
语义测试保留，尚未改正式源码。


common Value::getIf 现由既有 Value.cpp 集中七种 const/可变 Windows DLL
实例，保留 noexcept、Storage、类型/布局和直接借用；静态/非 Windows
路径不增加实例，泛型定义仍保留。复用 Value 生命周期测试，覆盖所有
类型、指针一致性、可变写入、类型不匹配、JSON 往返以及 noexcept。
Windows 四组实际 Foundation/JSON/Platform/ECS/zeroerr 组合通过；
GCC、正式整体净收益和最新源码门禁另行验证。PCH 未接入正式配置。

common Value::getIf 本轮验证已完成：Windows/GCC 的 compiled diagnostics
ON/OFF、共享/静态共八组通过；ON 为 47 cases/312 assertions/1 expected skip，
OFF 为 45 cases/294 assertions/0 skip。正式整体 OBJ 从 8,782,664,388
降到 8,773,073,085 字节，净省 9,591,303 字节（9.15 MiB）；配置 2870
个对象无缺失。真实定义从 198 OBJ/611 条降到 Foundation 内 39 OBJ/127 条，
跨组定义为零，Foundation 内仍保留原内联路径。make check CI_BASE=4d4e88f04
退出 0，232 个脚本测试通过，source-quality gate passed。

内联断言后续审计：现有 OBJ 中 Printer::tab/字符串转换/quote 各出现
992/991/990 个对象，demangle 出现 611 个；这些是相同非模板函数定义。
format<string> 出现 988 个对象，布尔 ExpressionDecomposer 实例出现在
909 个对象。计数不是节省字节，尚需正式重建计量。已在忽略目录建立
Printer 四个非模板方法的集中实现副本，不改变正式头文件、补丁或配置。

Printer 非模板方法集中化已接入现有 root-owned zeroerr 补丁与
CompiledDiagnostics.cpp，保留 compiled diagnostics OFF 的原内联实现。
Windows/GCC 的 ON/OFF、共享/静态八组通过：ON 48 cases/318 assertions/
1 expected skip，OFF 46 cases/300 assertions/0 skip。补充缩进、compact、
引号、字符串转换及 GNU demangle/未知类型名语义验证；签名和布局不变。
最终 LF 补丁的干净应用、幂等重放、真实漂移拒绝均通过；保留严格上下文，
不放宽 whitespace 匹配。公开借用/同步行为在声明中注明，无新增可变状态、
生命周期或跨域接口；无架构例外。
正式 Windows Debug 全部 OBJ 重建退出 0：8,773,073,085 -> 8,758,779,212
字节，净减少 14,293,873 字节（13.63 MiB），包含 owner 与新增断言测试。
2870 配置对象无缺失、实际 2877 对象无新增；不是 NTFS/cache 物理占用。
Printer tab/quote/字符串转换/demangle 的真实定义分别从 992/990/991/611
个对象收敛到各 1 个对象/1 条定义。当前 make check 正在运行，未声明通过。
这些 CPU 契约和 OBJ 编译证据不等于完整引擎 DLL 链接或 GPU 运行证明。

下一轮集中 compiled diagnostics 的 format<std::string>(const char*,
std::string&&)；该实例在先前签名扫描中出现在 988 个 OBJ。声明 extern
实例，原 CompiledDiagnostics.cpp 提供定义；关闭 compiled diagnostics 时
保留原泛型路径，其他类型不变。Windows/GCC 共享/静态隔离组合通过；
67 个匹配实验对象计入 owner 后净省 461,719 字节，排除 8 个旧实验残留。
正式 Windows/GCC ON/OFF、共享/静态八组通过：ON 48 cases/321 assertions/
1 expected skip，OFF 46 cases/303 assertions/0 skip。补测 string rvalue、
4096 字符长串和内嵌 NUL。最终补丁重放/幂等/漂移拒绝通过，无新状态或
布局变化，无架构例外。正式全量 OBJ 构建及本轮源代码门禁仍在运行。
上轮 round33 make check 最终退出 2，原因是启动 PATH 未加入既有
check-bin/python3，误用 Python314 与 Python runtime 的 Pillow 二进制；
不算源码门禁通过。round34 重新加入既有包装器后运行完整门禁。

format<string> 正式 Windows Debug 整体重建退出 0：8,758,779,212 ->
8,739,607,414 字节，净省 19,171,798 字节（18.28 MiB），包括集中实现及
新增语义检查；实际对象 2877、配置对象 2870，无缺失/新增。对应真实模板
定义从 988 个 OBJ/988 条收敛到 1 个 OBJ/1 条。与此前 Printer 四方法一共
净省 33,465,671 字节（31.92 MiB），仍为逻辑 OBJ 计量。当前 round34
make check 尚在运行，八组 CPU 契约和全量 OBJ 编译不替代整引擎链接/GPU
证据；保留既有完整引擎依赖限制，无例外批准或豁免。

布尔表达式候选的整类 extern template 实验被拒绝：67 个匹配对象反而
增长 36,810 字节，未接入。改为 compiled diagnostics 模式下只特化
Expression<bool>::operator ExprResult，保留原构造、比较及其他类型模板。
该转换之前重复在 909 个 OBJ；Windows/GCC 共享/静态隔离组合通过，
67 个匹配对象计入 owner 后净省 126,719 字节。正式补丁已接入，签名、
布局、调用线程、诊断内容和已有 decomp 复用保持不变，无架构例外。
补充 true 输出及现有 decomp 复用检查。测试最初 static_cast 写法在 MSVC
因另一 bool 转换路径歧义而无法编译，修正为断言原路径的隐式转换；
正式 Windows 四组已通过：ON 48 cases/326 assertions/1 expected skip，
OFF 46 cases/308 assertions/0 skip。GCC 最终测试与整体 OBJ 重建仍在运行。
首次全量构建因上述测试编译错误明确退出 2，修正后才继续依赖重建，
尚未给出正式整体节省量。round34 源码门禁已按核实的 PID/子树取消，
被 round35 完整门禁替代，不将旧门禁标作通过。最终补丁重放测试通过。

布尔转换特化的正式 Windows Debug 重建（修正后 retry）退出 0：
8,739,607,414 -> 8,730,299,768 字节，净省 9,307,646 字节（8.88 MiB），
包含集中实现和新增诊断语义检查。实际 OBJ 2877、配置对象 2870，
无缺失/新增。Windows/GCC 最终八组均通过：ON 48 cases/326 assertions/
1 expected skip，OFF 46 cases/308 assertions/0 skip。构造与析构仍保留
原模板路径，不把其重复数量误报为已集中。当前源码门禁继续运行，
未声明通过；完整引擎 DLL/GPU 限制仍未解除。

round35 源码门禁最终退出 0：232 个脚本测试通过，make check
CI_BASE=4d4e88f04 显示 source-quality gate passed；当前 formal 逻辑
OBJ 基线仍为 8,730,299,768 字节。更新扫描：bool 分解入口 909 OBJ，
const char*/int/unsigned long long/std::string/float 打印分别为
784/738/619/526/457 OBJ。布尔入口集中函数模板实例的隔离实验通过，
67 个匹配对象计入 owner 后净省 56,242 字节，实际入口定义为一次。
十种 builtin print 实例隔离组合 Windows/GCC 共享/静态通过，净省
382,127 字节；与 bool 入口组合后新增 overlay 头没有使 Ninja 重编
旧消费者，首次组合数字弃用。--clean-first 完整组合重建与运行均
通过，67 个匹配对象净省 450,661 字节；OFF 路由验证仍在运行。
候选尚未接入正式源码；不将实验规模净省当成整体项目收益。

组合候选的 compiled diagnostics OFF Windows/GCC 共享/静态四组也通过，
ON/OFF 合计八组：ON 48 cases/326 assertions/1 expected skip，OFF
46 cases/308 assertions/0 skip。round35 门禁完成之后才将十种 builtin
print 及 rvalue bool 分解入口的 extern function-template instances 接入
正式补丁与原 CompiledDiagnostics.cpp；函数签名、布局及流策略不变，
OFF 路径保留原泛型实现。无新增 mutable fact、callbacks 或模块边界，
无架构例外。正式源码补丁重放/幂等/漂移拒绝通过；Windows/GCC 正式
八组 CPU 契约通过。当前 round36 整体 OBJ 重建与最新 make check 仍在
运行，尚未声明正式项目总量收益；源码保持固定直到量测完成。
