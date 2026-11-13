# 地图 RTS 增援

通过 `make run/<platform>-debug GAME=examples/spawn-rts-reinforcements` 启动。

示例加载真实单位/武器内容、生成模板、地图 sidecar 与加载描述。`mapRtsSpawn`
拥有独立的 Map、RTS Table、Crowd、ActionRuntime、SensingWorld 和 DamageRuntime；
导航绑定同一张实际地图。脚本注入连续 tick 与纳秒时长，原生入口先推进 RTS 一次，再交接生成生命周期。

地图入口每 0.4 秒请求一个增援，成功总配额为六，落点采用实际地图导航和单位占用检查。
间隔调度显式使用 skip：错过或受阻的间隔不积攒追赶批次，后续间隔仍可请求，不能同时配置独立 retry 策略。
每个真实单位带指定阵营及 AttackMove 命令，实际 RTS 导航与 Crowd 驱动其向右移动。
蓝色方块读取 `captureState` 的真实单位坐标；背景网格是按本例固定资产绘制的示意，未冒充瓦片贴图渲染。
上方六段条显示成功生成数量，下方绿条表示六个实际单位均已移动离开入口。

验证成功打印：

```text
spawn-rts-reinforcements: six real RTS units admitted and moved toward attack-move target
```

生成核心接纳 tick 之前的交接失败，可用完全相同的 tick/delta 重试，原生入口不会重复推进领域。
领域步进失败或生成核心消费 tick 后的异常可能已经部分结算，会进入故障状态，需显式关闭。
生成按批次提交，不承诺整个帧回滚。`captureState().phase` 区分 ready/spawn_pending/faulted/closed。
示例把返回错误显示为脚本错误，不隐藏失败。
正常退出调用 `close()` 删除管理单位和 Crowd agent。没有通过脚本伪造坐标、复制生命状态或轮询抢走领域事件。

本例通过 freezeRuntime/sealRuntime 冻结既有 closed-native 整体文件，使用 fs.setIdentity/setupWriteDirectory 初始化玩家保存目录，再原子写盘、读取并 prepareRuntimeRestore/commitRuntimeRestore。第一份 `reinforcements-moving.json` 在至少三个单位已经开始移动时保存；恢复后逐个稳定 subject 核对坐标、阵营和 AttackMove，并继续推进 Crowd。第二份 `reinforcements-six.json` 在六个单位均离开入口后保存，恢复必须仍为六个单位和六个 agent，后续不得重新生成增援。文件每次运行覆盖，配置资产不变。

两个恢复边界额外打印：

```text
spawn-rts-reinforcements: moving units file restored with Crowd and attack-move orders
spawn-rts-reinforcements: six-unit file restored without duplicate reinforcements
```

存档内容与接纳由 RTSRuntimeCheckpoint/PreparedRTSRuntimeRestore 的原生 canonical 协议负责，脚本不拼装或猜测 RTS/Crowd 状态。准备失败保留旧运行，提交后发布完整隔离候选。freeze/seal 分两步，独立检查点可在源 close 后封装。snapshotHashAlgorithm 可观测当前摘要实现；文件读写沿用 fs，不建立第二层存储。

本例没有敌对单位，因此不展示射击/伤亡；外部未参与闭包的游戏系统、地图热替换、热重载和跨平台验收仍不属于该场景的完成证据。

短 smoke 只验证启动和持续运行；完整分支需核对上面的三条验收输出，可为较慢窗口/渲染环境预留 24 秒：

```sh
EVE="$PWD/build/macosx-debug/src/engine/eve" MIN_RUN_SECONDS=2 RUN_SECONDS=24 bash scripts/smoke_examples.sh spawn-rts-reinforcements
```
