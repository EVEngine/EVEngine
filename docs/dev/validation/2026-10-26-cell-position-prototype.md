# CellPosition：多尺度坐标数值原型

日期：2026-10-26。状态：局部实现已验证，尚未通过仓库整体门禁；不是完整多尺度世界。

## 实现范围

新增 [CellPosition.h](../../../src/modules/spatial/multiscale/CellPosition.h) 与
[CellPosition.cpp](../../../src/modules/spatial/multiscale/CellPosition.cpp)。旧源文件、默认配置、脚本绑定、
相机/物理更新和存档格式未修改。后续已把新增实现移入默认关闭的 spatial_multiscale 卫星模块；
模块清单只有显式扩展，旧 spatial 不再纳入新实现。

位置由每轴 int64 分区地址和 [-0.5,0.5) 双精度局部偏移组成。`projectCellRelative` 显式接收
分区尺度指数、输出单位指数与绝对误差预算，返回 Result<CellProjection>。
先做安全整数差，再转换相对差，避免把巨大绝对地址转成 double 后丢失局部位移。
完整 INT64_MIN/MAX 跨度通过无符号有序差处理，不产生有符号溢出。

尺度指数范围 [-900,900]，不是无限精度；输入必须来自同一轴对齐分区参考系。
首个原型没有 FrameHandle、坐标规范化、旋转/父子参考系转换或动态场景重定位。
后续精确归一化和静态轴对齐参考系树见[坐标补充记录](2026-10-26-multiscale-frames.md)。
这个约束在 API 注释中显式说明，不能把它当成完整坐标注册表。

误差输出是 IEEE binary64、渐进下溢且无 fast-math/FTZ 下的保守算术界，不覆盖输入量化或物理误差。
若输出范围或误差无法满足请求，返回 Unsupported；非法输入返回 InvalidArgument。
预算允许时下溢可返回零并带非零误差界；预算不允许时拒绝。三轴全部成功后才发布拥有型结果。
该函数每位置 O(1)，批量调用 O(N)，并不构成引力两阶段 O(N) 求解器。

## 聚焦测试

[独立单元测试](../../../test/unit/spatial_multiscale/cell_position_unit.cpp) 包含五个 case、30 条断言：

1. 在接近 int64 两端的地址上保留 2^-40 的局部位移。
2. 跨相邻分区的局部差、单位换算及反向差。
3. 完整有符号地址跨度，严格精度拒绝与宽松预算接受。
4. 非规范偏移、NaN、超范围尺度及非正预算拒绝。
5. 极大/极小单位、溢出、带误差的下溢和相同位置精确零。

本机 MSVC 19.51.36248，C++20 Debug，/W4 /WX /utf-8 /EHsc /fp:strict。
使用仓库既有 /wd4251 /wd4275 策略（cmake/compiler_options.cmake），没有修改仓库编译选项。
在忽略目录 `build/cell-position-probe/` 配置数值探针，实际编译新增源、原仓库
Status.cpp/Diagnostic.cpp/Result.cpp 与上述单元测试；使用本机主 checkout 的 zeroerr 源构建测试库。

- 静态链接探针：编译、链接成功，5/5 case、30/30 断言通过。
- 独立 DLL + 消费者探针：编译、导出/导入和链接成功，同样 5/5、30/30 通过。
- 探针没有窗口、GPU、场景或物理后端依赖。
- zeroerr 自身 serialization.cpp 有既有 unused-parameter 警告；新增目标以 /WX 编译通过。

当前工作树的探针重跑入口：

```powershell
cmd /c cmake\with-msvc.cmd cmake --build build/cell-position-probe/native --parallel 4
build/cell-position-probe/native/cell_probe.exe
cmd /c cmake\with-msvc.cmd cmake --build build/cell-position-probe/shared --parallel 4
build/cell-position-probe/shared/cell_probe.exe
```

探针构建目录不提交，不是正式构建目标；干净 checkout 按根 Makefile 配置/构建后，使用
`make test/win32-debug DOMAIN=core FILTER=spatial.cellPosition` 运行纳入标准发现的测试。
该测试要求配置时显式启用 EVENGINE_MODULE_SPATIAL_MULTISCALE。
完整引擎 SHARED/OBJECT 构建、跨平台 CI、原有场景回归和百万规模性能尚未执行。

## 源码门禁与阻塞

已使用 clang-format 18.1.8 格式化三个新文件。工具仅安装在忽略目录 build/check-tools。
测试命名、测试断言级别、模块依赖/层级检查通过。变更行架构检查通过；另外直接把新增头文件和
实现的全部行交给现有 lint_api_shapes/lint_runtime_guards 检查，也无违规，覆盖未跟踪文件。
全仓 --all 扫描未完成，停止后改用上述新文件完整扫描；不声称全仓历史代码无违规。

`make check` 最初受沙箱内 Git Bash signal-pipe 权限限制；在沙箱外运行并补齐本地 formatter 后，
通过格式、模块依赖和脚本绑定覆盖阶段，在 test source manifest 阶段失败：

```text
error: spatial: 2 unit files contain only 245 lines; consolidate below 5000 lines
```

来源为 scripts/test_module_coverage.py 的小文件合并规则，与本次用户提供的 AGENTS.md
“新测试放入自己的 test 文件”要求冲突。保留独立测试，没有修改 checker、合并旧测试、
添加例外或把失败伪装成通过。后续通过默认关闭的 spatial_multiscale 卫星模块与模块所属测试目录解决此冲突，
check_test_manifest 已通过；没有改 checker 或添加豁免。完整 make check 仍单独跟踪。
nodiscard 脚本未找到其支持的 c++/g++/clang++ 驱动而跳过；实际 MSVC 聚焦编译不等同于该诊断用例通过。

## 架构适用性与后续

本轮应用唯一值 owner、结构化 Result/[[nodiscard]]、Doxygen 成本/线程/生命周期及 FOUNDATION 导出规则。
无新增 ECS System、Link、provider、持久格式或自动执行路径，因而不引入相关注册或隐式生命周期责任。
没有向 common 添加领域接口，没有向旧空间 API 添加必需参数，没有批准任何兼容例外。

本文保留首个坐标原型的验证范围。后续 FMM、事务模拟和 Release 数值性能进展见
[场求解实现记录](2026-10-26-multiscale-field-baseline.md)。递归生成、无限远视图与百万实时验收仍未完成。
