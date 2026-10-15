# AI 场景搭建：Blender 输入与引擎权威编辑

Blender 继续制作模型、材质和初始布局，输出现有 GLB 导入链路。引擎负责最终镜头、
几何约束、正式场景命令和真实帧验证。MCP 传输命令，不保存独立的场景状态。

## 所有权、生命周期和确定性

`SceneAuthoringDocumentTarget` 是作者数据的唯一所有者：标准层级/TRS 加 source recipe、
相机和光照三个值对象。它继承 scene 领域短根；没有通用 GameObject 或新 ECS System。
source 描述资源/程序化配方，不是玩法或物理组件。TRS 旋转用弧度，矩阵顺序与
TransformSystem 相同：T * Ry * Rx * Rz * S。旧 yaw_deg 入口负责单位转换。

Session 独占 target，命令、保存恢复、undo/redo 使用同一事务协调器。spawn 将对象和
配方作为一个历史操作提交，失败不发布部分候选。source 记录允许保留删除对象的配方，
供普通 hierarchy delete 的逆操作恢复；新 spawn 替换同 id 配方。document target 无图形
依赖，普通 scene/live target 明确拒绝 authoring payload 命令。

Director 只缓存可重建资源和不可变几何描述，不复制位置。命令先在独立 native candidate
验证，再准备隐藏资源，最后发布文档并建立 SceneHost 投影。投影失败回滚并重建旧状态，
回滚失败单独报告 RollbackFailed。删除/替换通过 id + generation 销毁 renderable；
SceneHost Link 推送前检查存活。mesh/model 仍由图形/模型模块拥有，不得比模块活得更久。
所有操作在 owner/render thread，同步无锁；ECS 停止后不得调用 kit。

世界矩阵是 scene link 的派生值，保留反射和非均匀父缩放导致的剪切；直接 Renderable3D
TRS setter 清除该模式。球裁剪使用矩阵 Frobenius 上界。几何约束使用 CPU 顶点本地 AABB，
静态导入为 bind geometry；CPU morph baking 会更新它，GPU 蒙皮和着色器位移不包含在内。
无新随机流或时钟，source seed 显式保存。几何计算 double、当前脚本和 native rendering
float；原生数学测试 1e-9，跨脚本/渲染按 float 容差，视觉结果按像素报告。

## 持久化、导入与命令

格式为 `eve.scene.authoring` version 2，字段 schemaId、schemaVersion、hierarchy、sources、
camera、lighting、relations；hierarchy 使用现有 eve.scene.hierarchy version 1。未知版本/字段、
非有限数、非法相机拒绝。v1 显式迁移为空 relations；旧 TRS-only session 保持独立格式。
不保存原生指针、ECS slot、generation、仿真、动画或物理状态。

asset source 需要 kind=asset、path、revision。revision 是调用方提供的不可变版本身份，
当前不计算文件 hash、不打包模型；调用方须保证同版本对应相同内容。Model3D 烘入导入
节点变换，Director 为每个 mesh 创建渲染子节点。整套 Blender 布局用 SceneLoader 加载，
再 bind_host 到 native live session：Host 是权威，Director 借用；source/camera/lighting
命令返回 Unsupported，资源恢复继续由 SceneLoader 管理。Live 保存不是资源打包文件。

正式命令：scene.authoring.spawn.v1、scene.authoring.sources.set.v1、
scene.authoring.camera.set.v1、scene.authoring.lighting.set.v1。
scene.placement.ground.v1 以世界八角点最低 Y 贴到 height；align.v1 沿 axis 把目标 min
移到 reference max + gap；screen.v1 保持前向深度，将本地 anchor 投到画面 UV。
UV 左上为 [0,0]，右下为 [1,1]；这些约束不保证遮挡、碰撞或视觉语义。
奇异父矩阵、无效相机、移动参考对象的祖先均拒绝。

MCP eve_scene_modify 新增 ground、align、screen、save、restore、undo、redo、command、
bind_host。修改可传 expectedRevision，过期返回 Conflict。eve_scene_info 返回 native document、
实际 bounds 派生几何、revision、projectedRevision、renderedRevision、frame。宿主 update 调用
refresh，render3D 后调用 mark_rendered；每帧无修改时不遍历文档。命令 planning 不变更状态。

## 实测参考反馈

运行 `python -m scene_qc_agent.reference task.json --port 7529 --output <目录>`。
任务 schema eve.scene.reference-task version 1，字段 reference、mask、candidates。
mask 示例 `{ "rgb": [255,0,0], "tolerance": 80 }`；candidates 为 MCP action 批次数组。
reference 相对任务文件解析；机位、曝光和尺寸须预先标定一致。当前显式选择前景颜色，
不是自动从任意效果图恢复语义和隐藏几何。

每个候选经过正式命令、等待相同 revision 渲染、原生 eve_screenshot、前后 revision 核对、
silhouette IoU/centroid/area 测量。只保留 IoU 改善，否则恢复原文档。并发写入中止并拒绝
覆盖其他作者；空 mask、尺寸不一致、缺失截图均失败。输出 baseline/candidate/final PNG
和 report.json，没有模拟评分代替视觉证据。

## 验证契约

test/integration/gameplay-composition/scene_authoring.cpp 覆盖事务、schema、历史、父反射/剪切、贴地、间距和画面锚点。
test/integration/gameplay-composition/graphics_authoring_projection.cpp 覆盖矩阵保真、保守裁剪、mesh bounds。
工具测试覆盖像素偏移、空 mask、并发 revision。实际 SHARED/OBJECT 链接、定向测试和
真实帧验证在交付时报告；不声明未测量的还原率或跨后端视觉等价性。

可复现的原生验证：`python scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe`。
它在输出目录建立隔离的测试游戏，使用现有 Blender GLB，验证目标帧、画面锚点、
退步回滚、缺失资源事务、资产 undo/redo 和导入 live host 的移动/undo。输出原生 PNG、
report.json、import-proof.json 与日志；Linux 需按仓库说明提供 SDL 显示和 Vulkan 环境。

关系摆放第一阶段见 [实现与验证](2026-10-11-scene-relations-validation.md)。
