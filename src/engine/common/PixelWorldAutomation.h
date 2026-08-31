#pragma once

#include "common/Export.h"

#include <string>

namespace eve {

/** @brief Module-neutral JSON bridge from DevTools/MCP to PixelWorld tooling. */
class EVENGINE_API IPixelWorldAutomation {
public:
    static constexpr const char* capabilityName = "IPixelWorldAutomation";
    virtual ~IPixelWorldAutomation() = default;
    /** @brief Invoke a stable PixelWorld tooling operation and return a JSON object. */
    virtual std::string invoke(const std::string& operation,
                               const std::string& requestJson) = 0;
};

}  // namespace eve
