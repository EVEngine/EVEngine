# 曲线悬崖工具

运行：`eve run examples/curve-cliff`，或 `make run/win32-debug GAME=examples/curve-cliff`。
需要提供 `eve.json`、SplinePath、mesh.rock、mesh.splineExtrude 的当前引擎构建；
2026-10-14 的旧 SDK 缺少 JSON 接口，不能运行此示例。

在左下角俯视图拖动控制点，点击空白处追加点（3–16 个点，32×20 米编辑范围）。
右侧调整崖高、岩块间距、厚度、随机强度；New seed 更换随机种子，Close/Open curve
切换闭环。松开拖动后合并重建；Orbit / Zoom 控制观察角度和距离。
左上角可选择控制点、在相邻点之间插点，并用 X/Z 按钮每次移动 0.5 米；删除作用于选中点。
Undo/Redo 保存最多 32 个常规编辑快照。Save/Load 使用引擎保存目录中的
`curve-cliff-v1.json`，不会写入源代码目录。保存的是曲线和参数，可重新生成模型。

## 调研依据与设计选择（2026-11-13）

- [Unreal PCG 节点参考](https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-framework-node-reference-in-unreal-engine)：
  Spline Sampler → Transform Points → Static Mesh Spawner 是沿曲线采样、变换与模型选择的参考。
- [SideFX Project Elderwood / Cliffs](https://www.sidefx.com/tutorials/project-elderwood-cliffs/)：
  可控、模块化、快速迭代的大面积岩壁；该教程自身使用独立几何而非实例，细节依赖 Nanite displacement。
- [SideFX Project Pegasus / Cliff Generation](https://www.sidefx.com/tutorials/project-pegasus-cliff-generation/)：
  从高度场提取崖面、细分位移、分块生成。适合作为后续地形融合路线，当前不依赖 Houdini 或 Nanite。
- [Poly Haven Rock Face 01](https://polyhaven.com/a/rock_face_01)：层状主岩壁模型；
  [Rock Moss Set 01](https://polyhaven.com/a/rock_moss_set_01)：六种苔藓石块，适合崖脚装饰；
  [Rock Face 材质](https://polyhaven.com/a/rock_face)：岩壁表面参考。
  [官方许可](https://polyhaven.com/license)为 CC0。
- [Kenney Nature Kit](https://kenney.nl/assets/nature-kit)：CC0 的低多边形自然模型，可作为风格化替代。

美术方向采用**风格化低多边形悬崖**，不捆绑或加载写实岩面贴图。
上述外部模型、材质仅保留为调研参考。本工具缓存九个引擎生成的岩块网格：六个扁平岩层、
三个大尺度断裂岩体。每个源网格使用一级细分（最多 80 个三角面），上传时拆分共享顶点，
为每个面计算独立几何法线，形成真实的平面明暗；不是贴图模拟的低多边形。
按弧长错层布置，低频起伏控制大轮廓，大岩体打断规则水平接缝，崖脚添加碎石。
岩色通过每块的轻微明度差表现，表面不使用噪声纹理或法线贴图。
连续挤出背衬填补岩块间隙；模型保持刚体形状，不沿曲线弯曲岩石。
闭环不重复末端实例。渲染对象复用共享网格，并未宣称 GPU instancing 性能收益。

## 所有权、确定性与边界

- `cliffDoc` 是唯一作者文档；预览、曲线采样和网格是派生数据。生成器是项目内可复用脚本，
  不新增引擎模块、公共 C++ API、ECS System、跨域 Link 或第三方实现。
- 使用 canonical `SplinePath`、`mesh.rock`、`mesh.splineExtrude`，无另一套样条算法。
- 显式 seed + 索引 + 独立通道生成随机数；无墙钟、全局 RNG 或逐帧网格生成。
  相同构建/输入重复结果一致；跨平台浮点只要求容差内一致，不承诺 bit-exact。
  控制点改动导致采样数量改变时，下游岩块可重新排列，不承诺局部编辑稳定身份。
- 计算/上传完成后再替换预览；载入先验证并准备，失败保留旧文档和预览。
  API 调用同步且仅限 VM 主线程，无保存临时原生指针或未知回调。
- 持久格式 `eve.curve-cliff` v1，通过 `eve.json` 唯一 codec 编解码；拒绝未知字段、
  版本及无效范围。尚无旧版本需要迁移，未来版本必须显式迁移，不能静默解释。
- 最多 512 个采样区间、16 个控制点；复用已有 renderable，隐藏减少的对象。
  代价与采样点和输出数量相关，UI 合并更新，九个源网格仅初始化一次。每层不超过 513 块，最多八层，另有崖脚碎石与大岩体；
  不把 512 区间误报成总岩块上限。
- 当前工具是独立 authoring example；没有地形高度吸附、碰撞烘焙、外部模型选择器、
  场景编辑器插件注册或 glTF 导出。急弯/自交允许重叠，背衬不保证无自交。

## 验证

```sh
MIN_RUN_SECONDS=2 EVE=/path/to/current/eve bash scripts/smoke_examples.sh curve-cliff
make check
```

自动交互检查（会通过 Save 覆盖该工具的测试存档）：

```sh
eve run --mcp-port=7548 examples/curve-cliff
python examples/curve-cliff/verify.py
```

已在 2026-11-13 的本机 SHARED 开发构建验证换种子、开闭曲线、控制点定量移动、
插点、删除、撤销/重做、保存/载入，以及引擎截帧。低多边形最终版完整 `make check`、示例布局检查、交互验证与冒烟均通过。
原生示例冒烟通过完整 6 秒窗口。固定场景裁剪区域换种子的 RGB 平均差最大通道为 4.759/255，
撤销回原种子后为 0/255，验证了可见随机变化与本机构建的重复生成一致性。
物理鼠标拖动尚未验收：宿主 MCP 的 SDL 事件注入没有更新轮询鼠标状态；
验证器明确记录 `physicalDragVerified=false`，控制点按钮操作另有自动验证。
代码热重载关闭：编辑脚本后重启，作者数据通过 Save/Load 恢复，不尝试把原生渲染对象
作为跨脚本重载的可序列化状态恢复。UI 参数编辑仍立即生效。

截图必须由引擎 MCP `eve_screenshot` 获取。宿主全量测试留给 CI；此示例不修改引擎源码。
