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
