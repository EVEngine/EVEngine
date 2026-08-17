# 用户模块文档逐章 Review 记录

本记录不是对文字长度的检查，而是对每章执行相同的 API 审核。审核基准是当前代码，而不是 `docs/dev` 中的设计草案。

## Review 标准

每章合入前必须确认：

1. **入口正确**：模块名来自 `Module_REG`，辅助类型来自 `addClass` 或脚本注入类。
2. **示例可调用**：代码块中的方法存在于 `addFunc` / `addVar` / 注入脚本中，不把未绑定的 C++ public 方法写成脚本 API。
3. **调用顺序明确**：说明对象由谁创建、谁持有，以及 init/update/render 中应在哪一阶段调用。
4. **任务可落地**：至少有两个游戏开发目标，而不是只复述模块用途。
5. **限制不隐藏**：明确当前绑定缺失、线程限制、单位、资源生命周期和平台差异。
6. **API 不遗漏**：本章 API 快查覆盖对应模块源码中全部 `addFunc` 名称。
7. **证据可追踪**：至少链接模块源码，并用测试或仓库示例交叉检查典型流程。

## 逐章结果

| 章节 | 核对实现 | 交叉检查 | Review 结论 |
|---|---|---|---|
| Window | `src/modules/window/Window.cpp` | `test/window.nut`、`test/window_cpp.cpp` | 补齐 Graphics → Settings → 回读尺寸顺序与移动端差异 |
| Event | `src/modules/event/*/Event.cpp` | `test/event.cpp` | 区分平台 pump、一次性消息和持久状态 |
| Rx | `src/modules/rx/Rx.cpp` | `test/rx.cpp` | 区分推送流/事件桥接；脚本闭包仅主线程调用；subscribe 与 subscribe3 参数必填 |
| Timer | `src/modules/timer/Timer.cpp` | `test/timer.nut`、`test/timer_cpp.cpp` | 确认秒单位、单次 step 与暂停大 dt 风险 |
| System | `src/modules/system/System.cpp` | `test/system.cpp` | 区分 wall time、帧计时和阻塞 sleep |
| Keyboard | `src/modules/keyboard/Keyboard.cpp` | `test/keyboard_cpp.cpp` | 说明状态查询、边沿检测、key/scancode 和文本输入 |
| Mouse | `src/modules/mouse/Mouse.cpp` | `test/mouse.nut`、`test/mouse_cpp.cpp` | 明确当前脚本没有相对模式 setter |
| Touch | `src/modules/touch/Touch.cpp` | `test/touch.cpp` | 明确触点索引仅当前帧有效 |
| Joystick | `src/modules/joystick/Joystick.cpp` | `test/joystick_cpp.cpp` | 明确当前只绑定数量与 mapping，未绑定轴/按钮 |
| Filesystem / HotReload | `src/modules/filesystem/Filesystem.cpp`、`HotReload.cpp` | `test/filesystem.nut`、`test/hotreload.cpp` | 区分虚拟路径、写目录、watch 与资源替换 |
| Data | `src/modules/data/DataModule.cpp` | `test/data.cpp` | 区分 ByteData 所有权、DataView 引用和文档树 |
| Image | `src/modules/image/Image.cpp` | `test/image.cpp` | 发现并明确：C++ 图像方法当前未暴露给脚本 |
| Font | `src/modules/font/Font.cpp` | `test/font.cpp`、`test/graphics_font.cpp` | 补充 advance/bearing/kerning 的布局职责 |
| Sound | `src/modules/sound/Sound.cpp` | `test/sound.cpp` | 区分 Decoder、PCM SoundData 和 Audio Source |
| Audio | `src/modules/audio/Audio.cpp` | `test/audio.cpp` | 补充 listener/source、3D/relative 与资源生命周期 |
| Model3D | `src/modules/model3d/Model3D.cpp` | `test/model3d.cpp` | 区分 CPU ModelData 与 Graphics GPU/渲染职责 |
| Network | `src/modules/network/Network.cpp` | `test/network.cpp` | 补充 pump、HTTP、Socket、Channel、Session 和消息边界 |
| Thread | `src/modules/thread/Thread.cpp`、`ThreadPool.cpp` | `test/thread.cpp` | 明确 worker 禁止访问 VM/GPU/UI，主线程消费 Channel |
| Plugins | `src/modules/plugins/Plugins.cpp` | `examples/native-plugin/` | 补充 ABI、平台扩展名与依赖库排错 |
| Entity | `src/modules/entity/EntityModule.cpp` | `examples/ecs/main.nut`、`test/ECS.cpp` | 说明 Component/Entity/System 分工和系统顺序 |
| Physics | `src/modules/physics/Physics.cpp`、`World.cpp`、`World3D.cpp` | `examples/basic/main.nut`、`test/box2d.cpp`、`test/box3d.cpp` | 确认 2D 像素/米与 3D 米制、Body 中心、sensor、每帧 update，以及 rayCast/queryAABB/testPoint |
| Map | `src/modules/map/Map.cpp` | `examples/basic/maps/`、`test/map.cpp` | 补充 GID、投影坐标、运行时修改和 JSON 热更 |
| Particles | `src/modules/particles/Particles.cpp` | `examples/basic/particles/`、`test/particles.cpp` | 补充容量、生命周期、模块统一 update/render |
| Animation | `src/modules/animation/Animation.cpp`、`Tween.cpp` | `test/animation.cpp` | 说明 Tween 只算值，游戏负责写回属性 |
| RPG | `src/modules/rpg/RPG.cpp` | `examples/rpg/`、`test/rpg.cpp` | 串联 definition、actor、update、事件和 settlement |
| Inventory | `src/modules/inventory/Inventory.cpp` | `examples/inventory/`、`test/inventory.cpp` | 串联物品定义、Bag、转移、装备与变更事件 |
| Procgen | `src/modules/procgen/Procgen.cpp` | `examples/procgen/`、`test/procgen.cpp` | 补充 seed 可复现、Params/Grid/Output 与缓存策略 |
| Graphics | `src/modules/graphics/Graphics.cpp` | `examples/basic/main.nut`、渲染测试 | 明确资源创建与逐帧提交边界、2D/3D/UI 顺序，以及 Camera2D/3D 拾取换算 |
| UI | `src/modules/ui/UI.cpp` | `examples/basic/ui_demo.nut`、UI 测试 | 补充 Host、稳定 ID、事件消费和局部更新 |
| Scene | `src/modules/scene/Scene.cpp` | `test/scene.cpp` | 补充 Host/Node、local/world TRS、link 和 build 配对 |
| Math | `src/modules/math/Math.cpp` | `test/math.cpp` | 补充随机确定性、角度单位、临时对象风险，以及 2D/3D 拾取/重叠/射线几何 |
| IK | `src/modules/ik/IK.cpp` | `test/ik.cpp` | 补充 Skeleton/Solver 生命周期、目标和迭代成本 |
| GPGPU | `src/modules/gpgpu/Gpgpu.cpp` | `examples/basic/compute/`、`test/gpgpu.cpp` | 补充 binding、group 数、readback 同步与可用性检查 |
| Tensor | `src/modules/tensor/TF.cpp` | `test/tensor.cpp` | 区分 eager/symbolic/compiled，确认 run0–run4 |
| Demo | `src/modules/demo/Demo.cpp` | `src/scripts/demo.nut` | 明确可选构建、验证用途和生成资源生命周期 |

## 自动一致性检查

本次 review 另执行三个仓库级检查：

- 各 `src/modules/*` 目录均有同名用户章节；
- 各目录 C++ 文件中的全部 `addFunc("...")` 均出现在对应章节 API 快查；
- 所有 Squirrel 示例代码块中的方法调用均能在实际绑定或注入脚本中找到（模块构造器单独由 `Module_REG` 校验）。

这些检查用于防止后续 API 演进时文档悄悄失真；语义和调用顺序仍必须按上表人工复核。
