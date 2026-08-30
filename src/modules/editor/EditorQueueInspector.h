#pragma once

#include "editor/EditorProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::orders { class CommandQueue; }
namespace eve::production { class WorkQueue; }

namespace eve::editor {

/** @brief Runtime-neutral retained queue item for inspector tables. */
struct QueueItemSnapshot {
    std::string id;
    std::string owner;
    std::string kind;
    std::string product;
    std::string state;
    int priority = 0;
    double progress = 0.0;
    double duration = 0.0;
    std::string payloadJson;
};

/** @brief Runtime-neutral lifecycle event for timeline presenters. */
struct QueueEventSnapshot {
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::string item;
    std::string kind;
    std::string from;
    std::string to;
    std::string reason;
};

/** @brief Immutable queue inspection result with structured diagnostics. */
struct QueueInspectionSnapshot {
    std::string domain;
    std::vector<QueueItemSnapshot> items;
    std::vector<QueueEventSnapshot> events;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Safely copies short-lived Orders and Production observations for editor use. */
class RuntimeQueueInspector {
public:
    /** @brief Capture an Orders queue and optionally filter by state or kind. */
    QueueInspectionSnapshot capture(const orders::CommandQueue& queue,
                                    const std::string& state = {},
                                    const std::string& kind = {}) const;
    /** @brief Capture a Production queue and optionally filter by owner, state or kind. */
    QueueInspectionSnapshot capture(const production::WorkQueue& queue,
                                    const std::string& owner = {},
                                    const std::string& state = {},
                                    const std::string& kind = {}) const;
};

}  // namespace eve::editor
