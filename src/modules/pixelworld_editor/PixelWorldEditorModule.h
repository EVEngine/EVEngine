#pragma once

#include "common/Module.h"
#include "pixelworld_editor/PixelWorldCatalogPanel.h"

#include <memory>

namespace eve::pixelworld_editor {

/** @brief Script-facing owner for the dedicated PixelWorld Catalog panel. */
class PixelWorldEditorModule final : public eve::Module {
public:
    Module_REG(PixelWorldEditorModule);

    /** @brief Destroy the panel before the module and its script VM are released. */
    ~PixelWorldEditorModule() override = default;

    /** @brief Open the built-in Catalog draft panel, creating it on first use. */
    [[nodiscard]] eve::Result<void> openCatalog();
    /** @brief Hide the Catalog panel while preserving its draft. */
    void closeCatalog();
    /** @brief Report whether the Catalog panel is currently visible. */
    bool isCatalogOpen() const;

private:
    std::unique_ptr<PixelWorldCatalogPanel> panel_;
};

}  // namespace eve::pixelworld_editor
