# 地图生成：RPG 三波遭遇

运行 `make run GAME=examples/spawn-rpg-waves`，构建需启用 `map_rpg_spawn`。

版本化地图、sidecar 和模板交给完整 RPGMapRuntime 的 prepareRuntime/commitRuntime，依次创建三只狼、两只狼和一只头狼。每波必须收到所有成员的真实死亡，才开始下一波的 0.5 / 0.75 秒等待；总配额 6，地图存活预算 3。脚本每隔 0.25 秒在推进前调用 damage，然后 advanceRuntime 交接死亡并清理身体，最后 draw；不另行 RPG.update，也不模拟 AI/攻击公式。

第二波生成后冻结完整运行文件，原子写入玩家保存目录的 `waves-mid.json`，读回并整体替换 owner。该存档包含此前三次死亡的待确认事件；恢复后旧回执必须拒绝，新回执确认原生前缀。场景检查第二波仍为 2 个存活、总数为 5，继续推进第三波，禁止重新生成早期波次。

全部六次真实死亡交接后，确认剩余事件，保存 `waves-complete.json`，再次恢复。Completed、零存活和总数 6 必须保持；后续帧如果再次生成会立即报错。两个文件每次运行覆盖，由 fs.setIdentity/setupWriteDirectory 和 writeTextAtomic 管理；配置资产不会被改写。

橙色矩形读取实际活单位的位置；顶部进度条来自拥有式 captureRuntimeState 的实际 wave/phase。source.total 为精确十进制字符串，wave 从 0 开始；它们是 owning 只读投影。固定 seed 为 42，tick/正纳秒步长由游戏循环注入。关闭显式 close(remove)，示例关闭脚本热重载。

控制台应依次出现三次 published（3、2、1），以及：

```text
spawn-rpg-waves: second wave file restored with pending deaths, no wave replay
spawn-rpg-waves: three waves completed, six real deaths reconciled
spawn-rpg-waves: completed file restored without restarting waves
```

```sh
EVE="$PWD/build/macosx-debug/src/engine/eve" MIN_RUN_SECONDS=2 RUN_SECONDS=8 bash scripts/smoke_examples.sh spawn-rpg-waves
```

该场景验证完整地图生成运行状态，不将未参与归档的外部游戏系统、热更新、像素正确性或跨平台验收视为完成。
