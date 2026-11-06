# 递归前沿到旧网格格式的显式适配

本切片新增默认关闭的 procgen_recursive_graphics，L6，依赖 procgen 和
procgen_recursive。数学核心保持 L0，不反向包含渲染模块。
选用同步 builder：已知单一场景 owner 在细化/编辑/投影变化时调用，返回拥有数据的
候选结果，由调用方决定发布；没有第二份模型状态或隐式每帧遍历。

buildFrontierMesh 先验证完整覆盖，再把非空节点的已编辑包围体输出到原有 MeshBuild。
每个包围体含 24 顶点、12 三角形，保留节点路径、模型 revision、物理尺度指数及局部投影。
删除节点不输出几何；粗节点仍可包含空隙，不能当成精确碰撞或最终拾取结果。
这是后续远景代理的几何连接点，不等价于战舰/城市细节规则、无限远相机或渲染验收。

局部原点先在 double 中相减，再作二次幂缩放和 float 转换；包围体向外舍入。
量化误差超限、非有限值、范围溢出、投影塌缩和节点预算超限整体拒绝，不替换已有网格。
误差报告针对 double 投影结果，不声称覆盖所有前序运算的认证总误差。
物理长度指数不会直接压成 float，极端尺度可复用相同局部几何。

适用规则包括：模块单向依赖、Result/nodiscard、拥有型输出、成本与线程契约、
派生数据单一真源及默认关闭。六面说明与四轴选择写在模块 README，未引入 ECS System、
事件、脚本、MCP 或持久格式，无架构例外。

## 验证状态

新增模块全文件架构 lint 已通过（162 源码行），真实 Windows CMake 配置通过。
首轮 MSVC SHARED 编译发现导出宏使用了 DOMAINS，但 L6 归属 ORCHESTRATION；
已修正宏并触碰实现与测试消费者，确保重新编译。第二轮构建退出 0，
EVOrchestration.dll 和 unit_test_procgen 链接通过；日志为
build/multiscale-frontier-export-build.log，首轮失败日志单独保留。

ctest --test-dir build/win32-debug --output-on-failure -j 2 -R '^procgen\.recursiveGraphics\.'
已退出 0，2/2 用例通过，日志 build/multiscale-frontier-tests.log。
覆盖正向绕序/法线、稳定节点映射、物理指数 ±900 的局部几何一致性、编辑位移、
删除、修订号、量化/节点预算、重复覆盖与范围失败，失败不影响已产生的输出。
修正后全文件架构 lint 与 git diff --check 也通过。
该切片的完整 make check 已退出 0，529 项测试、5 项既有跳过，
日志 build/multiscale-frontier-source-check.log。后续新增的渲染测试单独重新运行门禁，
不能把这次结果视为后续源码的最终门禁。

## 原生 Vulkan 帧验证

新增 test/integration/multiscale-world/frontier_mesh_render.cpp，使用现有 Graphics、
Window、MeshBuild 上传和 drawMesh 路径，没有脚本求值或替代渲染器。
640×360 正交视图先显示两个叶节点，连续三帧后删除左叶、将右叶平移 2 个局部单位，
重新生成并上传网格，再连续三帧检查旧位置清空与新位置出现。
节点预算拒绝后仍绘制上一张有效网格，最后释放资源并验证背景恢复。

开启 EVENGINE_VULKAN_VALIDATION=1 和 EVE_MULTISCALE_CAPTURE_DIR 后运行：

```powershell
ctest --test-dir build/win32-debug --output-on-failure -R '^procgen\.recursiveGraphics\.nativeFrames'
```

首轮真实宿主构建及测试退出 0，1/1 用例、18 个断言通过；日志
build/multiscale-frontier-render-build.log / build/multiscale-frontier-render-tests.log。
验证层已实际启用，日志无验证错误；有未消费 location 6 顶点属性的性能警告与
VK_LAYER_NV_optimus API 版本警告，不宣称零警告。

帧来自 Graphics::getPixel 的已呈现图像读回，不是桌面/Xvfb 截图。
build/multiscale-captures/frontier-before.ppm 与 frontier-after.ppm 记录编辑前后，
首次图像已人工检查，形状位置与像素断言一致。随后修正导出时的线性颜色到 sRGB 编码，
最终重新构建已退出 0，随后执行 '^procgen\.recursiveGraphics\.'，3/3 用例全部通过。
最终日志为 build/multiscale-frontier-render-final-build.log、
build/multiscale-frontier-render-final-tests.log 和保留完整验证层输出的
build/multiscale-frontier-render-final-validation.log。
最终 PPM 转换成同目录 PNG 后再次人工查看，编辑前为中心完整包围体，编辑后仅右侧
移动后的半体；颜色编码已修正。转换只使用无损格式转换，不修改帧内容。
新增渲染测试后的完整源码门禁 build/multiscale-frontier-render-source-check.log 已退出 0，
529 项脚本测试、5 项既有跳过，source-quality gate passed（base=origin/dev）。

该测试证明包围体网格经过真实后端显示并响应修改；它没有覆盖透视无限远显示、
阴影/透明/拾取、多尺度移动或精细几何规则，也不是完整用户示例或 60 FPS 性能证据。
