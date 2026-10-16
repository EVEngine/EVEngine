#pragma once

/**
 * @file TileCollisionScriptSink.h
 * @brief 让脚本实现 `ITileCollisionSink`（阶段 5：把被"裸指针参数"卡住的插座接上）。
 */

#include <cstdint>
#include "common/Export.h"

namespace eve::map {

/**
 * @brief 声明"`ITileCollisionSink` 可以由脚本提供"这条接缝。
 *
 * 脚本提供方发布一个对象，实现 `replaceTileCollision(payload)`：`payload` 是一张表
 *
 * ```squirrel
 * function replaceTileCollision(payload) {
 *     // payload.layer.id / payload.layer.generation  ← 与 eve.TileLayer.getEntityId() 同一个数
 *     // payload.rects[i].x / .y / .width / .height
 * }
 * ```
 *
 * 发布走脚本侧既有 API（能力名就是 `ITileCollisionSink::capabilityName`）：
 *
 * ```squirrel
 * eve.provider.publishFor("ITileCollisionSink", "game.collision", this)
 * ```
 *
 * @remarks **接的是 listener 面**：消费方 `Map::publishCollision` 用 `cap::forEach` 派发，所以
 *          适配器注册成 listener（原生 sink 与脚本 sink 可以同时存在，按优先级依次收到）。
 * @remarks 幂等：重复调用替换上一对回调（与 `declareScriptDamageSeam`、
 *          `declareScriptClimbingEventSeam` 同一条纪律）。由 `Map` 模块构造时调用。
 * @cost 一次 map 插入；只在模块构造时调用。
 */
EVENGINE_API_WORLD void declareScriptTileCollisionSinkSeam();

/** @brief 撤回接缝与仍然在位的脚本适配器（由 `Map` 模块析构时调用）。 */
EVENGINE_API_WORLD void withdrawScriptTileCollisionSinkSeam();

/**
 * @brief 脚本 tile-collision sink **拒绝/抛错**的累计次数（诊断与测试用）。
 *
 * `ITileCollisionSink::replaceTileCollision` 是 `void`（通知，不是事务），失败没有返回值可放，
 * 所以给它一个可观测计数——与 `eve::script::reportedScriptCallFailures()` 同一条理由：**回调的
 * 失败必须看得见**，否则"脚本没接上"和"脚本接上但每次都失败"从外部完全一样。
 *
 * @return 进程内累计值；`resetScriptTileCollisionSinkFailures()` 归零，供测试隔离。
 */
[[nodiscard]] EVENGINE_API_WORLD std::uint64_t scriptTileCollisionSinkFailures();

/** @brief 把上面的计数归零（测试隔离用）。 */
EVENGINE_API_WORLD void resetScriptTileCollisionSinkFailures();

}  // namespace eve::map
