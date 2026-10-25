# HUD Gallery

运行 `make run/linux-debug GAME=examples/hud-gallery`，或在本目录执行 `eve run`。

示例使用一棵 retained UI 控件树构建各个 Host，不在 render 回调中重建界面或读取资源。
左上资源条、右上任务信息及顶部提示为 display-only Host；底部技能栏独立处理输入。
画布以 1920×1080 为设计尺寸，等比适配当前显示区域，并按安全区域锚定各个 Host。

- 点击场景或装饰 HUD：扣除 15 点生命，展示即时值与延迟拖尾。
- Q / 点击 PULSE：启动四秒冷却，径向遮罩显示剩余比例。
- E 对应按钮：验证第二个技能槽独立路由。
- R：恢复生命。
- F1：切换 HUD 用户缩放 1 / 1.25。
- F2：切换安全边距 24 / 90 个显示逻辑像素。
- V：切换 1280×720、1280×800、1680×720。
- 切换窗口尺寸时检查四角与底部锚点；技能按钮支持已有焦点导航。

`ui.resourceBar` 子节点为 `/background`、`/trail`、`/value` 和 `/label`；
`ui.skillSlot` 子节点为 `/activate`、`/cooldown` 和 `/key`。
组合件只接收显示值，生命和冷却仍归 `hudState` 所有。拖尾在生命值变化时调用 `animateItemValue`，延迟 350ms 后用 450ms 缓降；重复受伤从当前显示值重新过渡。

首版径向填充是纯色扇形/圆环，不采样贴图；线性填充支持贴图及 UV 裁剪。
Host 级输入穿透用于纯展示面板，混合界面应将交互区域分到独立 Host。
世界坐标标记继续使用现有 `setHostWorldAnchor`，本示例不构造 3D 场景。

`Window.setSize` 当前会重建窗口与 UI 后端，因此示例在切换尺寸后重新注册纹理并重挂载 HUD。
游戏状态保持不变；纹理注册 ID 只在所属后端的生命周期内有效。
