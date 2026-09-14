# 场景编辑组件闭环

本次提供可独立实例化的 `SceneEditorSession`，不实现固定编辑器产品。
场景操作由 `scene/editing` 生成，`scene/editor` 只组合命令注册、权限与事务历史。
开发工具和游戏内建造宿主自行选择 UI、输入、选择集、渲染方式与文件位置。

## 所有权与生命周期

- Session 独占 Target，Coordinator 的借用严格嵌套在 Session 内；销毁顺序为
  Coordinator、命令服务、Target。没有新的 ECS System 或通用实体根。
- 文档 Target 是层级/TRS 的唯一权威状态。显示对象只是可重建投影。
- Live Target 观察 SceneHost；Host 是运行时权威，Target 保存事务基线。
  外部编辑与基线冲突时拒绝提交，宿主应在明确编辑边界重新建立 Session；新会话不继承旧历史。
- 所有入口只在 owner thread 调用。输入 Value 和输出快照独立拥有数据。
  不在锁内调用 UI、脚本或用户代码。
- Live Target 通过带 generation 的 EntityHandle 检测 Host 销毁；ECS table 必须比会话长寿。
  原子发布后发出一次 `tree_changed`；观察者不得销毁或重入当前会话。
  修改保留现有 Link/行为；删除带 Link 或行为的节点会拒绝，先由其领域解除关联。

## 持久化与确定性

`saveJson` / `restoreJson` 只保存层级、稳定的 host-local object id、名称和完整 TRS。
新快照使用 `schemaId: eve.scene.hierarchy`、`schemaVersion: 1`；没有 schemaId 的
schemaVersion 1 旧快照继续可读；未知版本、未知字段、重复 id、父级缺失、环、
非有限数和零缩放均拒绝。恢复先验证整个候选，再进入可撤销事务。
场景组件、资源、脚本行为和物理状态不在此格式内；宿主继续调用对应领域保存接口。
不序列化 ECS slot、generation、裸指针或外部 Link。

操作与 JSON 编码在相同输入下确定；Live SceneHost 的浮点转换以 `1e-5` 容差比较。
不引入 wall clock、随机流或仿真时间。动画/物理预览不是模式字符串隐含的副作用。

## 验证范围

测试覆盖文档/运行时模型的相同命令、无副作用 planning、层级拒绝、保存恢复、
失败不修改数据、恢复与对象操作共用撤销历史。实际 GUI、裁剪构建和运行时验证结果
在交付时单独报告，不由测试名称或接口存在推断。

本次 Windows 实际验证：`minimal` profile 加 `scene`、`scene_editing`、`scene_editor`、
`devtools`、`font`，构建 `eve` 和 `scene_editor_component_test`。18 个用例、205 条断言通过。
默认全域 unit_test 在此裁剪配置中包含未启用领域的测试，不能成功链接，因此没有宣称全套通过。
两个示例均运行于真实 Vulkan；通过 MCP 调用示例动作处理器验证 Inspector、放置、存盘读取、
撤销重做及命令白名单，使用引擎截图确认面板和 Gizmo 渲染。此验证不等于人工鼠标逐项验收。

场景事务失败后 Coordinator 会丢弃其拥有的单次批次，让已有撤销历史保持可用。
配套用例先拒绝删除关联节点，再撤销前一次合法修改，覆盖这条失败恢复路径。

Gizmo 后续补全双轴缩放、保比例统一缩放和移动吸附的锁轴约束；底层依旧只提供几何描述与
射线交互，示例绘制箭头、轴端方块、平面和中心手柄。缩放吸附作用于相对起始缩放的倍率。
运行时脚本验证了平面拾取、双轴缩放提交/撤销与统一缩放比例；光照网格和阴影使用引擎截图确认。
