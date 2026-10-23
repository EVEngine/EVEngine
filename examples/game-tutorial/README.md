# Collect and Save：完整游戏开发教程

运行 `eve run examples/game-tutorial`，或
`make run/win32-debug GAME=examples/game-tutorial`。

- 方向键移动蓝色棋盘纹理的玩家，碰到黄色目标增加分数。
- R / Restart 重新开始，S / Save 原子保存分数，L / Load 载入主档或有效备份。
- 热重载保留当前位置/分数、原位更新纹理并重建 HUD；重新启动只通过读档恢复分数。

资源由 ImageData 程序生成，不依赖外部图片。保存格式是严格的 `tutorial.collect|1|score`，
拒绝未知版本、额外字段与非法数量；保存目录身份为 `evescript-game-tutorial`。
Web 的原子保存不受支持，写盘失败会显示状态，不假装保存成功。

`scripts/rules.nut` 是纯玩法与存档候选校验；`main.nut` 连接输入、纹理、HUD、文件系统
与生命周期。完整讲解见 [GAME_DEVELOPMENT.md](../../docs/usr/GAME_DEVELOPMENT.md)。
教程用于小型游戏，不宣称它具备联机确定性或跨多个业务域的原子存档。

game.agent.json 提供 collect-state 观察和 restart/save/load 的正式 eve_play 动作，
与键盘/HUD 调用相同游戏函数。用 `eve run --debug --mcp-port=7529 .` 连接；
协议请求和验证顺序见教程，不能把 manifest 存在当作验收已通过。
