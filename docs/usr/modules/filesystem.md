# 文件系统与热重载模块

**脚本入口：** `eve.Filesystem()`

通过统一虚拟文件系统读写、枚举、挂载并监视游戏资源。

该目录公开两个构造器：`eve.Filesystem()` 负责文件访问，`eve.HotReload()` 负责登记资源并在文件变化后执行替换。基础运行环境已分别创建为 `fs` 和 `hot`。

## 基本用法

```squirrel
local fs = eve.Filesystem();
local data = fs.read("save/profile.json");
fs.write("save/backup.json", data);
fs.watch("config/game.json");
```

## 对象关系与调用时机

`Filesystem` 管理虚拟路径、源目录、写目录、File/FileData 和 watcher；`HotReload` 管理可替换资源。游戏路径应使用正斜杠相对路径，写操作只能落在配置后的写目录。

## 目标导向指南

### 读写存档

先用 `createDirectory()` 创建存档目录，再用 `write()` 写入序列化文本。读取前可用 `getRealDirectory(path)` 判断资源是否可解析；写临时文件后再替换正式存档可避免中途退出损坏数据。

### 热更新配置

对配置文件调用 `fs.watch(path)`，在更新阶段轮询 `pollWatch()`；返回变化后读取 `getLastWatchPath()` 并重新解析。用 `hot.watchTree(root)` 递归监视内容目录时，若运行期间收到新建目录事件，再调用 `hot.watchNewDirectory(path)`，它会为该目录及其当前子目录补注册监视。资源对象可登记到 `hot`，但热更新回调中只替换成功加载的新对象，失败时保留旧对象。

### 远程热更新（`eve dev` + 全平台）

本地 watcher 只在游戏源目录可写时生效；iOS 应用包 / Android APK 内的资源只读，无法直接在设备上改文件。为此提供了“开发服务器 + 客户端同步”的远程热更新：

1. 在开发机（macOS）上运行 `eve dev [--port 8765] [游戏目录]`，启动 HTTP 文件服务器（`/manifest` 递归清单、`/raw/<相对路径>` 拉取文件）。
2. 在 `config.nut` 中设置：
   ```squirrel
   config.devServer = "http://192.168.1.5:8765"  // 开发机局域网 IP
   config.devSyncMs = 1000                        // 轮询间隔（毫秒）
   ```
   或在命令行注入：`eve run --dev-server http://192.168.1.5:8765`。
3. 设备端 `load.nut` 调用 `hot.startRemoteSync()`：后台线程按清单比对 mtime/size，把变更文件下载到可写覆盖目录（iOS: Application Support/EVEngine/hotreload；Android: 内部存储 /hotreload；桌面: <appdata>/EVE/hotreload），该目录以最高优先级挂载到 `/`，随后复用本地热更新管线（`.nut` → `dofile`，资源 → `tryReload`）。

改开发机上的 `main.nut` / 贴图 / JSON 后，设备会在下一个轮询周期内自动重载，无需重装应用。

## 常见问题

- 用 OS 绝对路径读取包内资源：应走虚拟文件系统。
- watcher 注册后从不轮询：必须在更新阶段调用 `pollWatch()`。
- 热更新失败后把资源置空：先构造新资源，成功后原子替换。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `append()`、`areSymlinksEnabled()`、`bind()`、`createDirectory()`、`getAppdataDirectory()`、`getCRequirePath()`、`getDirectoryItems()`、`getExecutablePath()`
- `getIdentity()`、`getLastWatchPath()`、`getLastWatchRealPath()`、`getName()`、`getRealDirectory()`、`getRequirePath()`、`getSaveDirectory()`、`getSource()`
- `getSourceBaseDirectory()`、`getUserDirectory()`、`getWatchCount()`、`getWorkingDirectory()`、`isAndroidSaveExternal()`、`isFused()`、`isRealDirectory()`、`newFile()`
- `newFileData()`、`pollWatch()`、`read()`、`readText()`、`remove()`、`setAndroidSaveExternal()`、`setFused()`、`setIdentity()`、`setSource()`
- `setSymlinksEnabled()`、`setupWriteDirectory()`、`tryReload()`、`unbind()`、`unwatch()`、`unwatchAll()`、`watch()`、`watchTree()`、`watchNewDirectory()`
- `startRemoteSync()`、`stopRemoteSync()`、`isRemoteSyncing()`、`remoteSyncStatus()`、`pollRemoteChange()`、`setRemoteHotDir()`
- `write()`、`writeText()`、`writeTextAtomic()`、`mountExternalReadOnly()`

玩家存档应使用 `writeTextAtomic(relativePath, text)`：原生平台先写同目录临时文件并刷新，
再原子替换目标，写入阶段失败时保留旧文件。路径必须位于配置的保存目录内且不能包含父级穿越。
WebGPU 当前无法提供同等级替换保证，会明确返回失败；普通缓存或可重建输出仍可使用 `writeText()`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/filesystem/`](../../../src/modules/filesystem/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `filesystem`。
