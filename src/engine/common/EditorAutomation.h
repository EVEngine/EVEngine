#pragma once

#include "common/Export.h"

#include <string>

namespace eve {

/**
 * @brief Module-neutral bridge from DevTools/MCP to the editor command protocol.
 *
 * JSON keeps the lower-layer DevTools module independent from editor C++ types.
 * Implementations must return a JSON object for every operation, including
 * rejected requests.
 */
class EVENGINE_API IEditorAutomation {
public:
    static constexpr const char* capabilityName = "IEditorAutomation";
    virtual ~IEditorAutomation()                = default;
    /**
     * @brief Invoke commands, plan, commit, cancel, undo, redo or diagnostics.
     * @param operation Stable automation operation name.
     * @param requestJson UTF-8 JSON request object.
     * @return UTF-8 JSON result object with status and diagnostics.
     */
    virtual std::string invoke(const std::string& operation, const std::string& requestJson) = 0;
};

}  // namespace eve
