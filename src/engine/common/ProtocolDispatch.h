#pragma once

/**
 * @file ProtocolDispatch.h
 * @brief Exhaustive outcomes for one LSP, DAP, or MCP input message.
 */

#include "common/DiagnosticValue.h"
#include "common/Result.h"

#include <string_view>

namespace eve {

/** @brief Observable disposition of one protocol input message. */
enum class ProtocolDispatch {
    ReplySent,
    NotificationHandled,
    Rejected,
    Terminate,
};

/**
 * @brief Structured result returned by protocol dispatch boundaries.
 * @note Transport/parse failures belong in the Result status; successful
 *       values describe every valid protocol disposition exhaustively.
 */
using ProtocolDispatchResult = Result<ProtocolDispatch>;

/** @brief DiagnosticValue mapping for protocol dispatch outcomes. */
template <>
struct DiagnosticValueTraits<ProtocolDispatch> {
    static constexpr std::string_view name(ProtocolDispatch value) noexcept {
        switch (value) {
            case ProtocolDispatch::ReplySent: return "reply_sent";
            case ProtocolDispatch::NotificationHandled: return "notification_handled";
            case ProtocolDispatch::Rejected: return "rejected";
            case ProtocolDispatch::Terminate: return "terminate";
        }
        return "unknown";
    }

    static constexpr DiagnosticCode code(ProtocolDispatch value) noexcept {
        return value == ProtocolDispatch::Rejected ? DiagnosticCode::PreconditionViolation : DiagnosticCode::None;
    }
};

}  // namespace eve
