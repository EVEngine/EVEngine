#pragma once

#include "common/Export.h"

namespace eve {

/** @brief Opaque EditorHost* attach callback registered by the editor module. */
using EditorMcpAttachFn = bool (*)(void* host);
/** @brief Detach callback registered by the editor module. */
using EditorMcpDetachFn = void (*)();

/**
 * @brief Register Host attach hooks used by `eve.mcp.attachEditor`.
 * @note Lives in common so DevTools and editor do not create a cyclic link.
 */
EVENGINE_API_FOUNDATION void setEditorMcpAttachHooks(EditorMcpAttachFn attach, EditorMcpDetachFn detach);

/** @brief Current attach hook, or null. */
[[nodiscard]] EVENGINE_API_FOUNDATION EditorMcpAttachFn editorMcpAttachHook();

/** @brief Current detach hook, or null. */
[[nodiscard]] EVENGINE_API_FOUNDATION EditorMcpDetachFn editorMcpDetachHook();

}  // namespace eve
