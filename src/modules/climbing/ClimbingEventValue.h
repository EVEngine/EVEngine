#pragma once

/**
 * @file ClimbingEventValue.h
 * @brief 攀爬事件 ↔ `eve::Value` 的**唯一**映射。
 *
 * 这份映射有三个消费者：脚本绑定（`eve.ClimbingRuntime` 的事件数组）、快照编解码
 * （`pendingEvents`）、以及脚本 sink（`IClimbingEventSink` 的脚本提供方）。它原先在
 * `ClimbingBindings.cpp` 与 `ClimbingSnapshot.cpp` 各写了一份（名称表甚至有两份，
 * 一份返回 `const char*`、一份返回 `std::string_view`），新增一个 `ClimbingEventKind`
 * 要改三处且没有任何东西会发现漏改。这里收敛成一张表 + 两个方向。
 */

#include "common/Export.h"
#include "climbing/Climbing.h"

#include "common/Value.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace eve::climbing {

/** @brief `ClimbingEventKind` 的取值个数（`0..kClimbingEventKindCount-1`）。 */
inline constexpr std::uint8_t kClimbingEventKindCount = 11;

/** @brief 事件种类名的解析结果。 */
enum class EventKindParse : std::uint8_t {
    Parsed,  ///< `name` 认识，`out` 已写入。
    Unknown  ///< 不认识这个名字；`out` 未被改动（不猜默认值）。
};

/**
 * @brief 事件种类的稳定脚本名（snake_case，例如 `"anchor_transition_started"`）。
 * @return 名字；表里缺项时返回 `"failed"`（与旧实现同值，行为不变）。
 * @lifetime 指向函数内的静态名表，进程生命周期内有效，不归调用方所有。
 * @remarks 新增枚举值必须同时更新 `kClimbingEventKindCount` 与 `climbingEventValue.cpp` 里的表；
 *          `test/climbing_events.cpp` 会遍历整个范围做双向验证，漏改会红。
 */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view climbingEventKindName(ClimbingEventKind kind);

/**
 * @brief 名字 → `ClimbingEventKind`（`climbingEventKindName` 的严格逆）。
 * @param name 脚本名；区分大小写。
 * @param out 解析结果，仅在返回 `EventKindParse::Parsed` 时写入。
 * @return 解析结果；**不认识时返回 `Unknown` 而不是猜一个默认值**。
 */
[[nodiscard]] EVENGINE_API_DOMAINS EventKindParse parseClimbingEventKind(std::string_view name, ClimbingEventKind& out);

/**
 * @brief 一个事件 → `{kind, actionId, tick, executionId, metadata}`。
 * @remarks `tick` 与 `executionId` 是**十进制字符串**：`u64` 装不进 JSON 数字，这是既有约定
 *          （快照与脚本绑定都照它写），不要改成数字。
 */
[[nodiscard]] EVENGINE_API_DOMAINS Value climbingEventValue(const ClimbingEvent& event);

/** @brief 一批事件 → Value 数组，顺序与传入一致。 */
[[nodiscard]] EVENGINE_API_DOMAINS Value climbingEventArrayValue(std::span<const ClimbingEvent> events);

}  // namespace eve::climbing
