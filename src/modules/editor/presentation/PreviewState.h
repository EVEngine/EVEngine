#pragma once

#include <cstdint>

namespace eve::editor {
/** @brief Owner-thread observation; request and provider generation are local monotonic epochs, not registry handles.
 */
enum class PreviewPhase { Idle, Pending, Ready, Failed, Cancelled, Unavailable, Closed };
/** @brief Owning preview lifecycle observation; payloads remain owned by the domain. */
struct PreviewState {
    PreviewPhase  phase              = PreviewPhase::Idle;
    std::uint64_t request            = 0;
    std::uint64_t providerGeneration = 1;
};

/** @brief Stable UI label for lifecycle observations (not a persisted format).
 * @ownership Borrowed static string literal. @lifetime Valid for the process lifetime.
 */
inline const char* previewPhaseName(PreviewPhase phase) noexcept {
    switch (phase) {
        case PreviewPhase::Idle: return "idle";
        case PreviewPhase::Pending: return "pending";
        case PreviewPhase::Ready: return "ready";
        case PreviewPhase::Failed: return "failed";
        case PreviewPhase::Cancelled: return "cancelled";
        case PreviewPhase::Unavailable: return "unavailable";
        case PreviewPhase::Closed: return "closed";
    }
    return "failed";
}
}  // namespace eve::editor
