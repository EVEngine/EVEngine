#pragma once

/**
 * @file SensingScriptCandidateSeam.h
 * @brief 让脚本实现 `ISensingCandidateProvider`（阶段 5：第三个落地的插座）。
 */

#include "common/Export.h"

namespace eve::sensing {

/**
 * @brief 声明"`ISensingCandidateProvider` 可以由脚本提供"这条接缝。
 *
 * 脚本提供方发布一个对象，实现 `queryCandidates(query)`：**收一张表、回一个数组**。
 *
 * 下行（`query` 表，就是那次查询本身；字段都是已校验过的值）：
 *
 * ```squirrel
 * {
 *     origin         = "<subject uuid>",   // 发起查询的主体（不会自动成为候选）
 *     space          = "world2d",          // world2d | world3d | grid2d | grid3d
 *     originLocation = { space = "world2d", x = .., y = .., z = .. },
 *     domain         = "enemy",            // any | self | ally | enemy | neutral
 *     minRange       = 0.0,
 *     maxRange       = 12.5,               // **null 表示无界**
 *     requiredTags   = ["unit"],
 *     excludedTags   = ["hidden"],
 *     zone           = "arena:central",    // null 表示不限制
 *     area           = null,               // 或下表的区域对象
 * }
 * ```
 *
 * **位置一律自带 `space`**（两个方向都一样）：`originLocation.space` 与 `query.space` 相等
 * （查询已经校验过），候选也必须自己声明空间。
 *
 * 区域对象（判据：`space` 决定它是世界还是格子）：
 *
 * | shape | 字段 |
 * | --- | --- |
 * | `circle2d` | `center = {x,y}`、`radius` |
 * | `box2d` | `min = {x,y}`、`max = {x,y}` |
 * | `sphere3d` | `center = {x,y,z}`、`radius` |
 * | `box3d` | `min = {x,y,z}`、`max = {x,y,z}` |
 * | `cone2d` | `apex = {x,y}`、`dirX`、`dirY`、`halfAngle`、`range` |
 * | `gridBox2d` | `min = {x,y}`、`max = {x,y}`（整数） |
 * | `gridBox3d` | `min = {x,y,z}`、`max = {x,y,z}`（整数） |
 *
 * 上行（返回值）：**候选数组**，每条一个表：
 *
 * ```squirrel
 * return [
 *     { subject = "<candidate uuid>",
 *       location = { space = "world2d", x = 3.0, y = 4.0 },
 *       domain = "enemy", tags = ["unit"], zones = ["arena:central"] },
 * ];
 * ```
 *
 * @remarks **候选的空间必须与 `query.space` 一致**。这不是啰嗦：格子与世界单位不可互换，静默换算
 *          只会得到悄悄错的距离；不一致时适配器返回 `Unsupported`（"请自己显式换算"），而不是猜。
 *          `domain` 缺省是 `neutral`，`tags`/`zones` 缺省为空。
 * @remarks **不下发的字段是 resolver 的策略，不是 provider 的活**：`lineOfSight`、`minCount`/
 *          `maxCount`/`countPolicy`。接口契约本来就写着"返回粗筛候选"，可见性与数量策略由
 *          `TargetingResolver` 在候选回来之后执行；下发它们只会诱导脚本重复实现别人的契约。
 * @remarks 幂等：重复调用替换上一对回调。由 `Sensing` 模块构造时调用。
 * @cost 一次 map 插入；只在模块构造时调用。
 */
EVENGINE_API_PLATFORM void declareScriptSensingCandidateSeam();

/** @brief 撤回接缝与仍然在位的脚本适配器（由 `Sensing` 模块析构时调用）。 */
EVENGINE_API_PLATFORM void withdrawScriptSensingCandidateSeam();

}  // namespace eve::sensing
