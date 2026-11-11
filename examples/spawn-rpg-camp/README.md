# 地图生成：RPG 营地

运行 `make run GAME=examples/spawn-rpg-camp`（构建需启用 `map_rpg_spawn`）。

示例从 `data/` 的版本化地图、模板和实例配置准备 RPGMapRuntime，显式提交后创建四个真实 RPGActor。运行 owner 拥有私有地图、领域、生成核心和事件队列；橙色矩形由同一 owner 的 canonical 呈现路径绘制。脚本注入 tick、正纳秒 dt 和固定 seed，不另行调用 RPG.update。

首个成功生成帧将整个运行状态冻结并封装成文件，先设置示例保存 identity 并 setupWriteDirectory，再通过 `fs.writeTextAtomic` 写入玩家保存目录的 `spawn-rpg-camp-save.json`，再读取并准备恢复，显式提交替换完整 owner；打印 `spawn-rpg-camp: whole runtime file restored`。存档包括地图资源、领域内容和状态、生成核心、队列及帧水位，不能将该文件当配置资产编辑。

一秒后在帧推进前通过正式 `mapRpgSpawn.damage` 对一个单位施加训练伤害，死亡交接后由运行 owner 删除身体；模板等待两秒后补充单位。脚本检查原主体身份已离开，存活恢复到四个，打印 `spawn-rpg-camp: replacement published after real death`。该训练不运行 AI 或攻击公式。退出显式 close(remove)。

所有 Result 均检查；恢复准备失败不替换旧 owner，提交后旧事件回执和激活权限失效。示例关闭脚本热重载；正式热更新仍需单独验收。保存文件每次运行覆盖，使用的是引擎当前可观测的 snapshot hasher（`mapRpgSpawn.snapshotHashAlgorithm()`）。
