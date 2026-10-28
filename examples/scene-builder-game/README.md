# 游戏内嵌场景建造

运行：`make run/win32-debug GAME=examples/scene-builder-game`。

复用 `../scene-editor/scene_components.nut` 和同一个 `SceneEditorSession`，
只显示放置、撤销、重做与移动 Gizmo。命令白名单仅允许创建和 TRS 修改；
即使直接调用会话，删除和恢复快照也会被拒绝。白名单不是脚本沙箱。

宿主决定 UI、输入、显示投影和允许的命令；没有依赖一个完整编辑器窗口。
打包独立游戏时，把 scene_components.nut 和 scene_visuals.nut 复制到游戏目录，调整两处 dofile 路径。

![实际运行截图](scene-builder-game.png)
