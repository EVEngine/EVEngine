# AI 场景搭建验证记录

2026-10-11，Windows/MSVC、原生 Vulkan，最小宿主加 graphics、scene_editor、procgen、
sceneloader、devtools、font；构建测试目标仅选择 eve 和 scene_authoring_contract_test。

## 已验证

- SHARED（严格警告）和 OBJECT（Debug 单 exe 路线）宿主及定向契约目标链接成功。
- 两条路线各 23 个逐 case 契约通过；使用 `-LE bundle`，未运行完整本地套件。
- 定向目标的 CTest 名称使用 authoring-contract/ 前缀，保留正常模块测试的注册和过滤；
  5 个真实 CMake discovery 测试通过，包括前缀与 bundle 标签的独立验证。
- 反馈工具 4 个测试通过：实际像素偏移、空前景、错误返回、并发版本和首次读回重试。
- `scripts/scene_authoring_smoke.py` 在两条原生路线通过；使用引擎 eve_screenshot。
- 固定相机的红色立方体故意偏移后，轮廓 IoU 0.165165 → 1.0；中心与面积误差归零。
  后续退步候选 IoU 0.041270 被拒绝，最终保持 1.0。逐候选版本、帧与截图在 report.json。
- 现有 Blender GLB 作为资产导入，实际顶点 bounds 可查询，undo/redo 重建成功；
  SceneLoader 整体导入后，bind_host、native 节点移动与 undo 成功，并捕获 blender-live.png。
- 缺失 GLB 的资源准备失败后，原文档 JSON 未变化。
- module_depgraph 无新增反向依赖；接口目录排序与组合快照有效，ruff 与 diff whitespace 检查通过。

复现（仓库根目录）：

```powershell
python scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/authoring-evidence
python scripts/scene_authoring_smoke.py --exe build/win32/src/engine/eve.exe --output build/authoring-object-evidence
ctest --test-dir build/win32-debug -L '^scene_authoring_contract_test$' -LE bundle --output-on-failure
ctest --test-dir build/win32 -L '^scene_authoring_contract_test$' -LE bundle --output-on-failure
```

本地依赖缓存较旧：在工作区 build/authoring-deps 内重新构建 SDL/Box2D/box3d/Squirrel，
使用仓库已有依赖补丁；原有预编译依赖树未改动。SHARED 宿主现在通过 manifest 派生的
无状态 anchor 引用保留模块 DLL，无新增模块 boot 清单。MCP JSON 指针容器转码也已修复，
不再丢弃 params 并使用默认值；精度测试覆盖嵌套字段和运行库浮点表示。

## 源码门禁的未通过项

已执行 `make check`，保持默认 origin/dev 基线。最终架构阶段报 59 条：
RenderCapture.h 3 条 bool API、SceneQuery.h 1 条 bool API、StatePatch.h 1 条 bool API、
Procgen.cpp 54 条 runtime ModuleManager/getModInst 调用。四个文件相对本次 HEAD 无工作区
改动；此次新增/修改行的独立诊断（`check_architecture_contracts.py --base HEAD`）通过。
这项独立诊断只用于定位归属，不替代默认门禁；没有新增 baseline 或 allowlist 消除失败。
完整日志位于 build/authoring-check.log。

## 架构规则与边界

使用领域短根、单一 native authoring owner、Result/nodiscard、既有命令/事务/历史、版本化
完整候选恢复、generation 检查与销毁顺序、owner/render 线程、显式 seed、模块自有接口目录。
没有新增 ECS System、玩法根类型或 L2/L3/L4 trim 声明；没有主动豁免架构规则。

测量只证明受控静态场景中的投影与反馈闭环。颜色前景由作者指定，参考图与输出须共享
相机、曝光和尺寸；不代表任意效果图的语义恢复率。GPU 蒙皮和着色器位移不计入 placement
bounds。asset revision 是调用方提供的不可变身份，当前不计算 hash 或打包资源。
导入 live host 的 source/camera/lighting 恢复继续由原来的 loader/viewport 管理，
authoring payload 命令在此模式明确返回 Unsupported。跨平台及不同图形后端视觉一致性未测量。
