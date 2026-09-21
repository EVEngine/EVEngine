#pragma once

#include "common/Export.h"

#include <string>
#include <vector>

namespace eve {

/** @brief Last completed query of one live sensing world. */
struct EVENGINE_API SensingWorldQuery {
    /** Slot index of the world in its owner registry; stable while it lives. */
    int index = 0;
    /**
     * Last query document (schema `eve.sensing.lastQuery` version 1).
     *
     * A world that has never run a query reports its zero state (origin 0,0 and
     * an empty `ranked` array) rather than an error: the document describes the
     * world, not a call result.
     */
    std::string lastQueryJson;
};

/**
 * @brief Runtime sensing read surface (provided by the sensing module).
 *
 * AI targeting and perception already published `debugLastQueryJson()` with MCP
 * consumption in mind, but no tool ever fetched it, so "why did the NPC pick that
 * target" was unanswerable from an agent. This capability exposes the same
 * documents for every live world.
 */
class EVENGINE_API ISensingQuery {
public:
    static constexpr const char* capabilityName = "ISensingQuery";

    virtual ~ISensingQuery() = default;

    /** @brief Number of live sensing worlds created by the project. */
    [[nodiscard]] virtual int worldCount() const = 0;

    /**
     * @brief Last completed query of every live world, in slot order.
     * @return One entry per live world; empty when the project created none.
     */
    [[nodiscard]] virtual std::vector<SensingWorldQuery> lastQueries() const = 0;
};

}  // namespace eve
