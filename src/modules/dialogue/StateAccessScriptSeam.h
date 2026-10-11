#pragma once

/**
 * @file StateAccessScriptSeam.h
 * @brief 让脚本扮演 `IStateQuery` / `IStateMutation`（阶段 5 的第五、六个插座）。
 */

#include <cstdint>
#include "common/Export.h"

namespace eve::dialogue {

/**
 * @brief 声明"`IStateQuery` 可以由脚本提供"这条接缝。
 *
 * 脚本提供方发布一个对象，实现与接口**同名**的六个问询方法（不需要全实现，见下）：
 *
 * ```squirrel
 * class WorldState {
 *     function value(subject, key)     { ... }   // 通用世界值
 *     function hasTag(subject, tag)    { ... }   // 精确标签成员
 *     function attribute(subject, key) { ... }   // 属性/声望
 *     function resource(subject, key)  { ... }   // 资源
 *     function state(subject, key)     { ... }   // 持久世界状态
 *     function authority(subject, scope) { ... } // 只读权限
 * }
 *
 * eve.provider.publishFor("eve.state.IStateQuery", "game.world-state", WorldState())
 * ```
 *
 * - 返回 `null` = **"这一问不归我"**（接口的 `nullopt` 语义）：调用方会继续问下一个 provider。
 *   因此**没实现某个方法也等于"不拥有那一类问题"**，不算错误。
 * - `hasTag` / `authority` 必须回布尔；`value` / `attribute` / `resource` / `state` 可以回任意值
 *   （数、串、表、数组都行）。
 * - @remarks **接的是 listener 面**：`DialogueState::queryProviders` 先问 `cap::query` 的单实现、
 *   再按优先级问 listener，**第一个给出非空答案的赢**。所以脚本 provider 不会顶掉原生 provider，
 *   而是补在同一张优先级表里（原生可以传更低优先级跑在脚本前面）。注册表按类型路由，per
 *   capability 只有一条接缝。由 `Dialogue` 模块构造时调用。
 * @cost 一次 map 插入；只在模块构造时调用。
 */
EVENGINE_API_DOMAINS void declareScriptStateQuerySeam();

/** @brief 撤回 `IStateQuery` 接缝与仍然在位的脚本适配器。 */
EVENGINE_API_DOMAINS void withdrawScriptStateQuerySeam();

/**
 * @brief 声明"`IStateMutation` 可以由脚本提供"这条接缝。
 *
 * 脚本对象实现 `applyStateMutations(payload)`：`payload` 是一张表
 *
 * ```squirrel
 * {
 *     transactionId = "…", correlationId = "…", causationId = "…",
 *     mutations = [ { subject = "…", key = "gold", kind = "set" | "remove" | "addTag" |
 *                     "removeTag" | "addNumber", persistent = false, value = … }, … ],
 * }
 * ```
 *
 * **返回值是"实际改了几条"这个整数**（与原生适配器同一个收据语义：0 条 → `NoOp`，否则
 * `Applied`，`transactionId` 由适配器从上下文回填）。同一个接口契约要求"先验完再发布"：
 * 脚本要么整批生效，要么在改动任何东西之前抛错——抛错会让本次调用失败，且**承诺没有半套状态**。
 *
 * @remarks 单实现槽位（消费方用 `cap::query`），顶掉时写一行 `warn`。
 * @cost 一次 map 插入；只在模块构造时调用。
 */
EVENGINE_API_DOMAINS void declareScriptStateMutationSeam();

/** @brief 撤回 `IStateMutation` 接缝与仍然在位的脚本适配器。 */
EVENGINE_API_DOMAINS void withdrawScriptStateMutationSeam();

/**
 * @brief 脚本 state query **失败**（抛错或答案形状不对）的累计次数。
 *
 * `IStateQuery` 的每个方法只回 `optional`——**没有失败通道**，而 `nullopt` 的语义是"不拥有"。
 * 所以一次真失败只能按"不拥有"返回（否则整条条件链会因为一个 provider 而崩），同时**计数 + 上报**：
 * 与地块碰撞 sink 同一条理由，让"脚本没接上"和"脚本每次都失败"分得开。
 *
 * @return 进程内累计值；`resetScriptStateQueryFailures()` 归零，供测试隔离。
 */
[[nodiscard]] EVENGINE_API_DOMAINS std::uint64_t scriptStateQueryFailures();

/** @brief 把上面的计数归零（测试隔离用）。 */
EVENGINE_API_DOMAINS void resetScriptStateQueryFailures();

}  // namespace eve::dialogue
