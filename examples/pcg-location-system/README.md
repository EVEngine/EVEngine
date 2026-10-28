# PCG Location System（位置书签系统）

验证 **位置书签**：`eve.LocationProfile` 保存"相机位姿 + 玩家位姿 + 控制器 + 场景名"
这样一个命名书签，之后按索引读回并把相机放到保存的位置上。用于关卡内的快速跳转、
编辑器预览与自动化取景。

## 运行

```bash
cd examples/pcg-location-system
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-location-system
```

## 验证内容

- `eve.LocationBookmark`：`name = "Overlook"`、`controller = "FlyingCamera"`、
  `scene = "PcgLocationDemo"`，`setCamera(overlook)` / `setPlayer(player)` 分别绑定两个
  `eve.LocationPose`；
- `LocationProfile.addBookmark(bookmark)` 与 `loadBookmark(0)` 都必须返回 `ok`，且读回的
  书签 `hasPlayer` 为真，否则 `assert` 失败；
- 相机的 `setEye` 直接取自读回的 `camera.x/y/z`；
- 读回 `getBookmarkController(0)` / `getBookmarkScene(0)`，确认控制器与场景名往返一致。

成功标记：`PCG_LOCATION_SYSTEM_READY bookmark=Overlook controller=FlyingCamera scene=PcgLocationDemo`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 书签构造、存取校验、相机取景、成功标记 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
