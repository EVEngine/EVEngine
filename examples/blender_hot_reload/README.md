# Blender scene hot reload (MVP)

Blender 保存 → 完整 GLB → 发布版本文件 → SceneLoader.load → 卸载旧 SceneHost。
无需重启引擎，无需手动重新加载。只做整个场景替换，不调用 `reload()`、
`SceneDiff`，也不保留导入场景的运行时状态。

## 运行

需要包含 graphics、scene、model3d、sceneloader 的桌面引擎构建（默认完整构建）。
这是普通 EveScript 示例，与 `examples/basic` 一样，无需新增 CMake target。
在仓库根目录：

```sh
make run/win32-debug GAME=examples/blender_hot_reload
# Linux / macOS 使用对应平台的 run target。
```

也可以使用已构建的引擎：

```sh
build/win32-debug/src/engine/eve.exe run examples/blender_hot_reload
```

1. 在 Blender 中打开本目录的 `scene.blend`。
2. 切换到 Scripting 工作区，在 Text Editor 中打开磁盘上的
   `blender_bridge.py`，点击 **Run Script**。每次启动 Blender 后执行一次；
   再次运行不会重复注册。不要依赖 `.blend` 自动执行脚本。
3. 移动 Cube 或 Suzanne，按 **Ctrl+S** 保存原来的 `scene.blend`。
4. 引擎窗口应自动显示新位置，终端输出 `[blender] replaced scene: ...`。

脚本也可在 Blender Python Console 中显式运行（替换为实际绝对路径）：

```python
import runpy
runpy.run_path(r"C:\path\to\EVEngine\examples\blender_hot_reload\blender_bridge.py", run_name="__main__")
```

监听会触发刷新，另有每 250 ms 检查版本文件的轮询来处理事件丢失或短暂读取失败。
总延迟包含 Blender 导出、检测及主线程导入时间；大场景会短暂停顿。
示例采用固定引擎相机和灯光，Blender 相机、灯光及动画不在本 MVP 导出范围内。

## 为什么使用 GLB

当前 SceneLoader 通过 Model3D/Assimp 导入场景。这里不把现代 Blender `.blend`
直接导入的兼容性当作前提，而使用 Blender 内置 glTF 导出器产生自包含 GLB，
几何和纹理在一个文件中发布。提供的 `scene.glb` 可直接运行，不需要安装 Blender；
`scene.blend` 是可编辑源文件。桥接脚本仅处理本目录 `scene.blend` 的保存，
另存为其他位置后不会发布。

## 替换和失败语义

- 复用 `load.nut` / Filesystem / FileWatch 的主线程资产通知；不添加线程或模块。
- 每次保存导出到临时 GLB，再重命名为唯一 `scene-<32位hex>.glb`。
  最后用 `os.replace` 原子发布 `current-v1.txt`，避免读到半写入文件。
- v1 版本文件是无换行的单个相对文件名，只接受 `scene.glb` 或上述版本名；
  未知内容被拒绝。不存在版本文件时使用随附的 `scene.glb`。
- 引擎先以不同路径加载候选场景，返回空时保留旧场景并自动重试。
  成功后在同一次主线程 update 中卸载旧路径，下一次绘制只看到新场景。
  此处“原子”指帧边界可见性及解码失败保留，不是 GPU 分配失败的事务回滚。
- SceneLoader 是单例，而且 `load()` 同一路径会覆盖其拥有关系记录，所以不能
  用“再次 load 同一路径”实现替换。唯一版本路径也避免重用旧纹理缓存。
- Blender 导出失败不会更新版本文件。运行中不删除旧版本，以避免读者竞争。
  历史 GLB 与引擎资源缓存会随保存次数增长；这是短时、小场景 MVP，
  长时间编辑应定期重启引擎。关闭引擎后可删除 `scene-*.glb` 和 `current-v1.txt`。
  这些临时文件均已被 Git 忽略。

## 重建示例资产

**会重置示例场景；先保存自己的修改到别处。** 使用 Blender 5.2.1 验证：

```sh
blender --background --python examples/blender_hot_reload/blender_bridge.py -- --create
```

关闭引擎后删除旧 `current-v1.txt`，下次启动便使用新生成的 `scene.glb`。
Windows Store 版 Blender 如不能直接启动 `blender.exe`，可在 Blender 的
Python Console 运行桥接脚本后，调用返回字典中的 `create_scene()`。

## 验证要点

初次加载、连续两次保存移动对象、重复注册桥接脚本、导出失败不发布、
损坏 GLB 不卸载旧场景，以及之后发布有效版本能够恢复。
退出时示例显式卸载当前导入场景；相机与 SceneLoader 由现有引擎生命周期管理。
未更改公共 API、模块边界或构建系统。
