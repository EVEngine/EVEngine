# 多尺度场：实现进展与未通过性能基线

日期：2026-10-26。基线 HEAD：`5f2cd482ad1f88a7e11116000823a2b139c54a61`。
本记录针对未提交工作树，不是整个设计的完成报告。

## 用户确认的最终目标

百万独立源与目标，1080p，固定步长约 1/60 秒，**整帧 16.67 ms / 60 FPS**。
当前硬件：Intel Core Ultra 9 285K，24 核/24 逻辑处理器；可见 GPU 为 Intel Graphics
和 AMD Radeon AI PRO R9700；系统报告可用物理内存总容量 50,974,298,112 字节。
数值基准单线程 CPU 执行，不使用 GPU，没有渲染分辨率；它不能验证整个帧目标。

## 已实现边界

- `spatial_multiscale` 与 `physics_field` 在所属 manifest 中声明，选项默认 OFF。
  默认 full 配置选择 196/198 个模块；显式开启两个选项后选择 198/198。
  这是配置选择证据，不等于完整引擎 L1/L2/L3/L4 裁剪证据。
- [GravityField](../../../src/modules/physics/field/GravityField.h) 是实际笛卡尔多极/局部展开：
  归一化、树、P2M/M2M、双树交互、M2L/L2L 和原输入目标批量求值。
  单子路径压缩保留实际空间中心；深度与分区计数仍计入压缩前访问成本。
  没有按距离丢弃源。固定阶数/深度并限制交互与直接对数时，已接纳工作有线性资源上界；
  任意分布可能被拒绝，不能据此保证所有输入都能线性完成。
- 正质量、自由空间点源，输入行同时作为目标，按输入索引排除自作用。
  共点不同源拒绝；有限体积核、软化、独立目标集尚未接入。
- 误差只提供解析截断界，**不包含完整浮点舍入界**；不宣称已满足最终总误差验收。
  全局归一化仍有动态范围限制，不等于多参考系跨无限尺度求解。
- [GravitySimulation](../../../src/modules/physics/field/GravitySimulation.h) 显式拥有点体状态，
  固定 tick/dt 的 velocity-Verlet prepare/commit；初始加速度由真实构场产生。
  候选持有弱身份凭证，过期/跨 owner 提交被拒绝；失败不发布位置、速度或 tick。
  没有注册到旧物理循环，也没有实现刚体绑定、迁移或持久化。

当前没有新增 ECS System、provider、脚本绑定或持久格式。唯一可变 owner、Result、
显式时间、Doxygen 所有权/线程/成本、FOUNDATION 导出、模块层级及默认关闭规则适用。
无兼容例外。实际旧场景回归、整机引擎链接与后端适配仍待验证。

## 数值与生命周期验证

MSVC 19.51.36248 / C++20，Debug `/W4 /WX /fp:strict`，沿用仓库 `/wd4251 /wd4275`。
独立探针编译实际产品源和 common Result/Status/Diagnostic，使用初始化的 zeroerr。
可提交重跑入口现位于 benchmarks/physics_field/CMakeLists.txt；新建 Debug DLL 目录
build/field-contract 编译并运行 CTest 成功（1 个聚合入口包含上述 18 个用例）。

- 静态探针：18 个用例、4690 条断言通过，包括压缩树近距离分离。
- DLL + 消费者：压缩树修改后重新编译，18 个用例、4690 条断言通过。
- 测试覆盖两体势/力、多簇远场在 2/4/6 阶对直接和的比较、自作用、非法/共点输入、
  距离 1e100/质量 1e200 的远源、对称抵消、平移/重排、
  三种工作预算、输入快照独立、stale tick、移动后失效、外来/过期提交和失败回滚。
- 两体圆轨道 2000 步、dt=0.01：能量漂移 <1e-8，动量/角动量漂移 <1e-12。
  这不是所有分布和所有平台的精度证明。

## 首次 Release 数值测量（压缩树修改前）

可重跑入口：[benchmarks/physics_field](../../../benchmarks/physics_field/README.md)。
Release `/O2 /fp:strict`；seed=20261026，均匀分布，G=1，总质量=1，阶数4，
解析绝对加速度截断目标1e-3，近场上限1024N，交互访问上限512N。

- N=1000，三次阶段 A/B 合计分别为 10.5568、10.7402、10.8098 ms。
  首次 A=2.3002 ms、B=8.2566 ms；266 节点，3502 分区访问，61616 双树访问，
  708 远场交互，996650 直接对。接近全直接计算，不构成线性扩展证据。
- N=10000：154.122 ms 后阶段 A 返回 `physics.field.gravity.budgetExceeded`，近场预算超限。
- N=1000000：37561 ms 后同样拒绝。阶段 B 未执行，不能算完成了百万状态更新。

这些失败保留为优化对照，不通过增大误差、删除远源或缩小验收对象掩盖。
压缩树后用保存的 Release 入口复跑 N=1000：10.9928、10.6011、10.6660 ms，
节点/交互计数和 checksum 不变；此均匀输入未出现可压缩单子路径，不宣称取得加速。
没有整帧 P50/P95/P99，没有百万实时达标结论。完整分布矩阵、总误差参考、
内存峰值、旧功能兼容、生成/可见性/上传/GPU 成本仍需补齐。

## 源码门禁

`check_module_manifest.py` 和 `check_test_manifest.py` 已通过，原 spatial 测试小文件冲突
通过独立卫星模块所有权解决，没有改 checker 或加豁免。完整 `make check` 第二次已通过格式、层级、绑定、测试布局/命名/断言、示例、架构和 ruff；
脚本阶段终态为 529 tests / 5 failures / 1 error / 5 skipped，make check 未通过。
另两项首轮失败为 CMake 找不到编译器；经 with-msvc.cmd 复跑后，SDK 架构用例进一步暴露
Windows 安装路径未经转义、CMake 报 Invalid character escape \U；仍未通过。
单独复现 test_ci_third_party_cache.py：8 个测试，3 failures、1 error、1 skipped。
错误包含 Windows 绝对路径经 Bash 变为 C:Users... 后找不到文件，以及 cache prefix 不存在。
显式把 Git Bash 放在 PATH 首位重试仍失败；未修改这些既有测试或生产缓存脚本来掩盖问题。
nodiscard 脚本未找到受支持编译器而跳过，不能计作该诊断用例已通过。
独立探针通过不能替代仓库门禁、完整 SHARED/OBJECT 或跨平台 CI。
