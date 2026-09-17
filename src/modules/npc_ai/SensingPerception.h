#pragma once

/**
 * @file SensingPerception.h
 * @brief Adapter that copies sensing PerceptionFact into npc_ai PerceptionMemory.
 *
 * Sensing never depends on npc_ai. Gameplay / AI owners call these helpers after a
 * SensingWorld query to write adapter-owned memory. Threat / primary selection stay
 * outside both modules.
 */

#include "npc_ai/NpcAi.h"
#include "sensing/Sensing.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace eve::npc_ai {

/**
 * @brief Builds one PerceptionMemory from a neutral sensing fact.
 * @param fact Borrowed sensing projection (no ownership transfer).
 * @param observedTick Simulation tick when the fact was observed.
 * @param forgetAfterTicks Exclusive forget tick; must be > observedTick.
 * @param sense Stable sense key; defaults to "sensing.query".
 * @return Owning memory ready for NpcAiWorld::remember.
 * @remarks Confidence maps score through a soft logistic so distance scores stay in (0,1].
 * @thread Call on the npc_ai world's owning simulation thread.
 */
[[nodiscard]] inline PerceptionMemory perceptionMemoryFrom(const eve::sensing::PerceptionFact& fact,
                                                           std::uint64_t observedTick, std::uint64_t forgetAfterTicks,
                                                           std::string_view sense = "sensing.query") {
    PerceptionMemory memory;
    memory.subject          = fact.subjectId;
    memory.sense            = std::string(sense);
    memory.observedTick     = observedTick;
    memory.forgetAfterTicks = forgetAfterTicks;
    // score is higher-is-better (Phase 1: -distance). Map into (0,1].
    const float confidence = 1.f / (1.f + std::exp(fact.score <= 0.f ? -fact.score : fact.score));
    memory.confidence      = confidence > 0.f ? static_cast<double>(confidence) : 0.0;
    std::ostringstream payload;
    payload << "{\"x\":" << fact.x << ",\"y\":" << fact.y << ",\"distance\":" << fact.distance
            << ",\"score\":" << fact.score << ",\"scoreReason\":\"" << fact.scoreReason << "\"}";
    memory.payloadJson = payload.str();
    return memory;
}

/**
 * @brief Projects a sequence of sensing facts into npc_ai perception memories.
 * @param facts Borrowed sensing facts in ranked order.
 * @param observedTick Simulation tick when the facts were observed.
 * @param forgetAfterTicks Exclusive forget tick; must be > observedTick.
 * @param sense Stable sense key shared by all projected memories.
 * @return Owning memories in the same order as `facts`.
 */
[[nodiscard]] inline std::vector<PerceptionMemory> perceptionMemoriesFrom(
    std::span<const eve::sensing::PerceptionFact> facts, std::uint64_t observedTick, std::uint64_t forgetAfterTicks,
    std::string_view sense = "sensing.query") {
    std::vector<PerceptionMemory> memories;
    memories.reserve(facts.size());
    for (const auto& fact : facts)
        memories.push_back(perceptionMemoryFrom(fact, observedTick, forgetAfterTicks, sense));
    return memories;
}

}  // namespace eve::npc_ai
