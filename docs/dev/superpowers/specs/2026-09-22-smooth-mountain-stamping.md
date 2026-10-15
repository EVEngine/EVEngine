# 山体与程序化地形平滑融合

## 范围与数据流

本次实现高度图印章 SmoothRaise，以及 CPU 三角网格到印章的烘焙 builder。
输入网格使用已有 MeshBuild，坐标为 Y 向上；先应用所需模型变换，再烘焙。
模型文件解码复用调用方的导入流程，本接口不新增 FBX/OBJ 文件解析器。
俯视采样取最高三角形交点，因此不保留洞穴、悬挑、UV 或材质。

生成流程为：基础高度图 → 烘焙或手绘印章 → SmoothRaise → 网格/法线/碰撞重建。
烘焙输出高度与覆盖遮罩由调用者独占，成功后同时发布。无交点高度为 0、遮罩为 0。
覆盖边界可在烘焙时向内淡出；矩形边缘另外支持 world-space edgeFade。
两者都不能修复原山体内部的尖锐或不连续几何。

## API 与成本

- TerrainStampOperation 在现有 0..5 后追加 SmoothRaise=6。
- smoothWidth：高度差单位的多项式融合带宽；0 等同普通 max。
- edgeFade：矩形四边向内的世界距离；0 禁用，使用两轴 cubic smoothstep 的乘积。
- TerrainMeshStampBuilder::setSource 拷贝并验证 MeshBuild 的位置/索引。
- builder::bake 写入调用者预分配的同尺寸高度图和覆盖图；三角形先投影到栅格
  包围盒，只遍历可能覆盖的样本，最高 Y 获胜，绕序不影响结果。
- common/SmoothMax.h 为 Math 脚本适配器与 procgen 共用纯数学实现，避免
  procgen-core-only 对 Math 模块及其脚本/GLM 依赖的链接耦合。

昂贵烘焙走 builder，注明按三角形投影包围盒覆盖量累积的成本并设工作量上限；
不在每帧重新烘焙。同一印章可多次放置。调用期间输入不可变、输出独占；
不保留借用指针，无 callback、锁、ECS、Link、能力查询、时钟或 RNG。
多印章顺序固定，跨平台使用浮点容差，分块复用同一世界坐标。

## 持久化

TerrainGenerationSession 从版本 1 升至 2；TerrainSpawnPlan 从 3 升至 4。
旧记录迁移时两个新字段为 0，旧版本不能携带操作 6。保持已有旧版本读取能力。
schema id 不变，未知根字段、未知版本、尾随或缺失 payload 拒绝。
先验证并重放候选状态再发布，失败保持当前数据与历史。
builder 为临时生成对象，不新增持久格式；会话保存其烘焙后的高度/遮罩。

## 验证

原六种操作回归；SmoothRaise 数值、零宽度、旋转、遮罩、世界淡出、相邻瓦片一致；
网格最高面、绕序、无覆盖、退化面、无效索引、预算、所有权和双输出回滚；
会话 undo/redo/重放、新格式 round-trip、旧格式迁移、损坏记录回滚；
脚本接口和示例检查，procgen-core-only 编译/链接证据，make check。
示例真实运行与截图只有在可用 engine/显示环境验证后才能声明通过。

## 本次验证记录

- GCC 独立链接运行 24 个原生用例、2042 条断言，0 warning/failure；覆盖原六种操作、
  smoothMax、印章/瓦片、网格烘焙、会话和计划迁移。
- 使用仓库锁定的 Squirrel/simplesquirrel 构建并执行新绑定测试，通过；示例 main/config
  也通过真实 Squirrel 字节码编译。此处不等同于完整引擎启动。
- `procgen-core-only` 标准 profile 编译和 capability present/absent smoke 通过。
  新原生核心为 L1 编译证据，独立测试提供这两个算法路径的无 graphics 链接/运行证据；
  不扩展为整个 procgen 模块的 L2/L3/L4 裁剪主张。
- `make check` 通过，含 180 个仓库脚本测试；binding catalogue 无 unresolved binding。
- `make debug JOBS=4` 在配置阶段因缺少 Vulkan 开发库停止；示例 smoke 因缺少 eve
  可执行文件未启动。因此完整引擎、真实画面和碰撞场景尚未验证。
- 已按要求使用 Result、候选原子发布、拥有式 builder、线程/成本说明和版本迁移；
  补齐 module-interface 目录类型的基础 schema 校验，不引入架构例外。
