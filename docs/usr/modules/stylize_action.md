# 动作驱动的风格化特效

**脚本入口：** `eve.StylizeAction()`；`config.modules` 使用槽位 `"stylizeAction"`。

`stylize_action` 把 Action 的时间线 cue 接到 Stylize 的攻击特效执行器。它负责特效实例
的阶段推进；命中判定、扣血、目标选择仍由 Combat/Action 的领域逻辑决定。

## 从攻击事件播放效果

先加载带 `schema: eve.stylize.attack-vfx`、`schemaVersion: 1`、稳定 id 和 phases 的
recipe。`registerRecipeJson(json)` 与 `registerSkinJson(json)` 返回 Result，检查 ok 后
才能 play。`play(recipeId,sourceId,targetId)` 返回实例 handle；后续 signal、inspect、stop
都传 slot 与 generation，不能只保存 slot。单位身份参数是此运行时约定的整数。

在游戏 update 中由唯一 owner 调用 `advance(dt)`，时间单位为秒。蓄力/释放等阶段通过
`signal(slot,generation,cue)` 驱动；普通 render 只读取可见实例。不把特效完成当作
“伤害已提交”的依据。`activeCount()` 可用于排查未释放实例。

## 把效果绑定到动作时间线

Action attack_vfx payload 的 recipeId 与 uri 二选一；不能两者都给或两者都缺。
把资产注册放在加载阶段，时间线 notify 只携带逻辑引用。角色退出或动作取消时检查 stop
结果并按明确模式处理，旧 generation 的 handle 不能用于新实例。

相机、粒子、音频和 decal 各层经对应 executor/provider 执行；缺少某个 provider 的行为
需要观察返回状态，不能把“已注册 recipe”当作所有效果均已显示。先验证无资源依赖的阶段
与取消，再逐层接入资源。测试中的 `particles://missing` 是失败注入，不能当成可用资源。

源码：[StylizeAction.cpp](../../../src/modules/stylize/action/StylizeAction.cpp)；可复用的
recipe 与 provider 测试：[stylize_action_attack_vfx.cpp](../../../test/integration/gameplay-composition/stylize_action_attack_vfx.cpp)。
完整资源层组合见 [stylize_attack_vfx_audio_decal.cpp](../../../test/integration/gameplay-composition/stylize_attack_vfx_audio_decal.cpp)。
