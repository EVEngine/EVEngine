# Control Rig 自由度与关节范围

## 实施顺序

1. 在 RigControl 中增加拥有型约束数据及唯一原生投影函数。平移 XYZ、参考姿态相对旋转 XYZ、统一缩放分别支持 free/locked/limited。伸缩模式沿参考父局部骨段方向移动，比例区间控制长度，禁止侧向脱离。轴范围必须包含参考值。
2. eve.control-rig v2 保存完整约束；v1 原子迁移到无限制默认值。未知字段/版本拒绝。保持现有 replace 命令与同一文档事务后端，更新参数 schema。
3. 对编辑输入、拖拽预览、采样、FK/Aim/IK 最终骨姿态、反向匹配、烘焙共用约束。约束投影后保存编辑结果。受约束关节的 FK 控制层级须与骨层级相符，编译时解析，运行时仅使用索引。
4. 示例面板可选择每轴模式、范围、伸缩比例，并提交为一次可撤销编辑；Agent 使用相同 schema 和命令。增加原生回归与实际 MCP 流程证明。

## 契约

RigDefinition 是唯一权威数据；参考 TRS 是约束自身持有的校准姿态，普通姿态编辑不能移动它。平移范围为父局部参考位置的偏移，旋转为参考旋转右乘固定轴 XYZ（qz*qy*qx）欧拉角，单位度，规范分支 X/Z [-180,180]、Y [-90,90]。这不是多圈旋转系统。缩放/伸缩为参考比例，1 为参考值。伸缩模式不能同时设置平移轴约束，防止互相矛盾的约束。

默认不约束；locked 值为平移/旋转 0、缩放 1。limited 上下界包含参考值。有效越界姿态被确定性投影，非法配置、NaN、零参考骨长拒绝且不发布。FK 关节约束在每个求解节点后重建世界矩阵；达到限制时 IK 目标允许不可达，不承诺物理碰撞或最优多关节 IK。

线程/所有权沿用 ControlRig：同步借用输入，拥有快照，无回调、无新增 ECS 或跨模块生命周期。浮点结果 tolerance-bounded（1e-4）；时间显式注入，无 RNG。旧文档迁移不新增第二份数据。检查包括原子失败、v1/v2 roundtrip、采样和烘焙、IK 后限制、UI/Agent 共用撤销；最后执行 make check 和聚焦构建/测试。

## 实施结果（2026-10-23）

四步已完成。公开类型/API 使用 owning data、Result 和 nodiscard；约束留在 animation，编辑器继续使用唯一文档与既有事务。没有新增模块边界或架构例外。新热路径和脚本绑定已登记在 animation 架构契约分片。

- 隔离验证树 control-rig-authoring（基线 804540af60307c2eccc8f75ca831e87447d63ec2）完成 MSVC SHARED 测试构建与 OBJECT 示例构建；19 个任务源码/测试文件与主工作区内容一致，避免混入其他正在进行的 Value 等迁移。
- 22 个 `animation.controlRig.*` / `editor.controlRig.*` 测试通过，含相对参考旋转、±180 度插值、轴锁、伸缩、IK 后约束、重复 FK 拒绝、v1 迁移、烘焙、原子失败与共享撤销。
- animation=OFF、animation_editor=ON 的两个编辑目标编译通过（L1 裁剪证据）。
- `make check CI_BASE=804540af60307c2eccc8f75ca831e87447d63ec2` 通过，脚本测试 250 项通过；最终架构契约单独复验通过。独立 nodiscard 编译诊断探针因 PATH 无 GCC/Clang 跳过，实际目标使用 MSVC 严格警告构建。
- 原生 smoke 存活 6 秒，无错误标记；Python lint/编译检查通过。
- `agent_limits_demo.py` 实际通过：锁定位置/缩放、±80 度请求收敛至 ±12 度、4 倍长度请求限制为 1.2 倍、UI/Agent 交叉撤销、非法范围不改变 revision、原生烘焙播放。源码热重载重新注册成功，约束文档完整保留。
- `build/control-rig-limits/` 保存 result.json、limited.rig.json、截图及 20 张不同引擎画面组成的 limits.gif（约 7.2 秒）。本轮未声称此前发现的 Vulkan 图片布局问题已修复。

### 重影修复

用户发现的重影存在于原始 PNG：热重载丢弃脚本引用后，旧 ECS Renderable3D 仍存在，新建模型与旧姿态叠绘。此前只验证文档恢复，未验证预览对象生命周期，视觉验收不足。现在保留预览表并复用模型部件和相机，仅重建动画/蒙皮绑定。单一干净进程连续两次重载，9 个实体 ID/generation 不变，文档相等，重新运行 MCP 流程通过。新原始帧已人工检查，`build/control-rig-limits/limits-no-ghost.gif` 是重录的 20 帧约 7.1 秒证据，替代旧重影录制。
