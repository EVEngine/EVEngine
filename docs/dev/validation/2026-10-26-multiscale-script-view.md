# 多尺度参考视图：脚本接入与原生示例

日期：2026-10-26。基线 5f2cd482ad1f88a7e11116000823a2b139c54a61，工作树未提交。
本记录不代表生成世界整体或百万对象性能验收完成。

## 接入与兼容

在已有默认关闭的 graphics_multiscale 声明中增加 SCRIPT MultiscaleView / SLOT multiscaleView。
模块仍只通过既有 manifest 组合，无新启动列表。旧相机、旧脚本循环和默认深度均未改动。
MultiscaleView 是无状态工厂，不是新的场景、实体或模拟所有者。
模块的新增外向面为脚本绑定；没有 ECS System、事件、capability 注册或工具协议变化。

newSphereView 显式接受 Graphics provider；prepareSphereFrame 按显式数量预算准备完整快照。
两者统一使用 common 的 Result 表与 makeOwnedSquirrelInstance 所有权桥。
只注册抽象帧/视图类型，由工厂建立有效实例；视图使用已有 Graphics 弱生命周期保护。
帧复制所有输入，调用者随后修改或销毁输入表不会改变已准备帧。
恢复或重新加载脚本后须重新创建运行对象，没有增加持久化格式。

位置的三个 cell 地址必须是十进制 int64 字符串，拒绝越界、非完整解析、数字替代和非规范 fraction。
未知字段、错误三元数组、非法颜色、非有限值、数量预算以及核心精度约束均显式拒绝。
转换深度上限 8、元素上限 64+32*maxSpheres；数量上限 1,000,000 是输入限制，不是实时能力承诺。
浮点 fraction、半径与颜色仍受调用方脚本数值精度影响，没有声称恢复已损失的输入精度。

已准备帧使用 draw 在专用新帧边界绘制，宿主正常脚本循环负责 present。
GPU 异常仍可能留下部分 pass，必须由宿主恢复，不能宣称 GPU 事务回滚。
所有调用限 Graphics/VM 所在线程、非重入；准备成本 O(N)，创建视图分配一次 GPU 资源，
绘制仍为 O(visible N) 提交与 O(visible N * pixels) 光栅工作。

## 验证证据

- build/multiscale-script-build.log：Windows MSVC 严格警告 SHARED 构建，包含 eve 与 graphics 域，退出 0。
- build/multiscale-script-tests.log：6/6 定向测试通过。新增真实 Squirrel VM 契约覆盖
  int64 最大共享地址、越界/有损数字/未知字段/非规范 fraction/预算/空帧、输入释放和 null provider。
- 首次测试发现绑定库在接收 null 时提前抛类型异常；门面改为接收 Object 并做受检类型转换，
  现在返回 common Result 失败。没有更改绑定库或放宽测试。
- 源码门禁随后发现新用例仅含 CHECK；将有效帧准备改为关键前置 REQUIRE。
  最终断言版构建 build/multiscale-script-final-build.log 已退出 0；最终定向测试
  build/multiscale-script-final-tests.log 6/6 通过。源码门禁
  build/multiscale-script-source-check.log 已退出 0，529 项脚本测试通过、5 项既有跳过。
- examples 布局检查通过。build/multiscale-script-smoke.log：标准 smoke 12 秒通过，存活且无错误标记。

examples/multiscale-spheres 是正常 main.nut/config.nut 游戏入口，未使用 eve_eval 作为玩法路径。
每四秒在近球遮挡/远球单独显示之间切换，两个球在 2^900 cell 尺度下相距 10^17 倍且角尺寸相同。
使用 scripts/capture_example.ps1、显式传入当前工作树 eve.exe，在第 60/300 帧读取引擎呈现帧。
两张 1920x1080 图像已人工查看：build/multiscale-captures/script-near.png 是红球，
script-far.png 是蓝球。按主色通道差大于 50 提取球体像素，两者均 106016 像素，
掩码对称差为 0；中心分别为 [225,99,99] 和 [99,99,225]。
原始检查为 script-image-check.json，捕获日志为 build/multiscale-script-capture-near.log / -far.log。
这是渲染集成的连续状态证据，不是精度总界、平台一致性或百万对象帧率证明。

## 尚未完成

生成世界组合示例、交互编辑保存、物理源接入、详细舰船/城市生成、GPU 提交故障注入、
裁剪和其他图形后端仍待实施。完整文档目标保持未完成。
本次应用 Result/所有权/线程/成本/单一 manifest/源码绑定真源规则，没有用户批准的架构例外，
也没有自行豁免这些规则。

## 后续类型与生命周期审查

实际检查 simplesquirrel 的 args.hpp：Object::to<T*>() 仅取 instanceup，不核对原生类型标签。
因此门面在转换 Graphics/SphereFrame 前显式检查 INSTANCE 和稳定类型标签；draw 还检查
当前调用接收者，阻止通过 .call() 将其他实例当成 SphereView。null、错误原生类型和
错误接收者均返回公共 Result 失败，没有更改第三方库或旧绑定行为。
数量预算先读取完整脚本整数、检查 [1,1000000] 后再转换为 int，避免提前缩窄。
这与 SphereFrame 原有正预算契约一致：空帧合法，但也需正预算。

新增脚本断言已全部使用 REQUIRE：此前 CHECK-only 的空帧预算检查不会使进程失败，
所以之前的 6/6 不能证明零预算合法。现已纠正输入契约与测试，并重新验证。
新增直接拥有 Vulkan provider 的测试（不删除 ModuleManager 单例）实际创建 shader/mesh，
先销毁 provider，再确认弱令牌过期、draw/render 返回失败，最后销毁消费者。
其 WebGPU 分支仅验证 Unsupported；本机只执行了 Vulkan 分支，不冒充 WebGPU 验收。

build/multiscale-lifetime-build.log：Windows 严格警告 SHARED 构建退出 0。
build/multiscale-lifetime-tests.log：最终 7/7 定向用例通过，包含强断言脚本边界与 provider-first。
开启 Vulkan 验证层，build/multiscale-lifetime-validation.log 中没有 Validation Error / VUID 错误，既有属性未使用警告仍存在。
完整源码门禁 build/multiscale-lifetime-source-check.log 已退出 0，529 项测试通过、5 项既有跳过。
这些修改仅约束新增 API，无新 ECS、持久格式或公共旧接口语义变更，无架构例外。
类型检查收紧后的正常示例 smoke 再次通过 12 秒（build/multiscale-lifetime-smoke.log）。
