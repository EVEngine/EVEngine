#pragma once
#include "common/Export.h"

#include "editor/EditorResult.h"
#include "editor/EditorSelection.h"
#include "editor/EditorWorkspace.h"

#include <string>
#include <string_view>

namespace eve::editor {

class EditorHost;

/**
 * @brief Default selection → inspector panel activation for a mounted host.
 *
 * Routes selection changes on the host channel to the first visible inspector
 * panel whose capability matches a mounted package selectionDomain / capability.
 */
class EVENGINE_API_ORCHESTRATION EditorSelectionRouter {
public:
    explicit EditorSelectionRouter(EditorHost& host);

    /** @brief Host selection channel. */
    [[nodiscard]] const std::string& channel() const;

    /**
     * @brief Apply a selection and activate a matching inspector panel.
     * @return Applied with the activated panel id (empty when none matched).
     */
    [[nodiscard]] Result<std::string> select(const SelectionItem& item, bool additive = false);

    /** @brief Clear the host channel selection. */
    [[nodiscard]] Result<void> clear();

private:
    EditorHost* host_ = nullptr;
};

}  // namespace eve::editor
