#pragma once

// Capability interface: semantic inspection and interaction for retained game
// UI. DevTools consumes this without depending on the ui module.

#include "common/Export.h"

#include <string>

namespace eve {

/** @brief Semantic retained-UI automation provided by the ui module. */
class EVENGINE_API IUIAutomation {
public:
    static constexpr const char* capabilityName = "IUIAutomation";

    virtual ~IUIAutomation() = default;

    /**
     * @brief Serialize visible and hidden retained UI hosts and their widget trees.
     * @param host Optional exact host name; empty returns all hosts.
     * @return Compact JSON, or an error string beginning with `error:`.
     */
    virtual std::string tree(const std::string& host) const = 0;

    /**
     * @brief Read one widget's semantic state.
     * @param host Optional exact host name used to disambiguate widget ids.
     * @param widget Widget id.
     * @return Compact JSON, or an error string beginning with `error:`.
     */
    virtual std::string get(const std::string& host, const std::string& widget) const = 0;

    /**
     * @brief Queue a semantic click through the normal UI event path.
     * @param host Optional exact host name used to disambiguate widget ids.
     * @param widget Clickable widget id.
     * @return Compact acknowledgement JSON, or an error string beginning with `error:`.
     */
    virtual std::string click(const std::string& host, const std::string& widget) = 0;
};

}  // namespace eve
