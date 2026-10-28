#pragma once

#include "game_event/GameEvent.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace eve::game_event::test {

/** @brief Creates a deterministic non-nil event identity for envelope tests. */
inline EventId eventId(std::uint64_t serial) {
    EventId::Bytes bytes{};
    bytes[6] = 0x40;
    bytes[8] = 0x80;
    for (int index = 0; index < 8; ++index)
        bytes[8 + index] = static_cast<std::uint8_t>(bytes[8 + index] ^ (serial >> ((7 - index) * 8)));
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);
    return EventId(bytes);
}

/** @brief Builds a canonical envelope for a test without using the removed emit facade. */
inline GameEvent envelope(std::uint64_t serial, std::string type, std::string source, std::string subject,
                          std::string causation, std::string correlation, std::uint64_t tick, std::uint32_t flags,
                          std::string payload = "null") {
    GameEvent result;
    result.eventId = eventId(serial);
    result.type    = std::move(type);
    result.source  = std::move(source);
    result.subject = std::move(subject);
    if (!causation.empty()) {
        const auto parsed = EventId::parse(causation);
        result.causation = parsed ? CausationRef::fromEventId(*parsed) : CausationRef::fromLegacy(std::move(causation));
    }
    if (!correlation.empty()) {
        const auto parsed = EventId::parse(correlation);
        result.correlation =
            parsed ? CorrelationId::fromEventId(*parsed) : CorrelationId::fromLegacy(std::move(correlation));
    }
    if (const auto schema = eve::LogicalId::fromParts("game_event", result.type)) result.schemaId = *schema;
    result.schemaVersion = SchemaVersion(1);
    result.tick          = SimulationTick(tick);
    result.flags         = flags;
    result.payload       = std::move(payload);
    return result;
}

}  // namespace eve::game_event::test
