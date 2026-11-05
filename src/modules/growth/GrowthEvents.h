#pragma once
#include "common/Export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::growth {

/** @brief Kind of a deterministic growth lifecycle event. */
enum class GrowthEventKind : std::uint8_t {
    StageChanged = 0,
    Matured,
    Produced,
    Harvested,
    Watered,
    Fed,
    Fertilized,
    Diseased,
    Cured,
    Bred,
    Offspring,
    Died,
};

/** @brief One queued lifecycle observation. */
struct GrowthEvent {
    GrowthEventKind kind       = GrowthEventKind::StageChanged;
    std::uint64_t   organismId = 0;
    std::string     speciesId;
    std::string     detail;
};

/** @brief Append-only event log cleared explicitly (economy-style). */
class EVENGINE_API_FOUNDATION GrowthEventLog {
public:
    void              push(GrowthEvent event);
    void              clear();
    [[nodiscard]] int count() const;
    /**
     * @brief Borrowed event at index, or nullptr when out of range.
     * @ownership Borrowed from this log; caller must not free it.
     * @lifetime Valid until the next mutating call on this log (`push`/`clear`).
     * @nullable Yes when index is out of range.
     */
    [[nodiscard]] const GrowthEvent* at(int index) const;
    /**
     * @brief Stable English spelling for an event kind.
     * @return Non-owning pointer to a static string literal; never null.
     * @ownership Caller does not own the pointer and must not free it.
     * @lifetime Valid for the process lifetime (static storage).
     */
    [[nodiscard]] static const char* kindName(GrowthEventKind kind) noexcept;

private:
    std::vector<GrowthEvent> events_;
};

}  // namespace eve::growth
