#include "pixelworld_editor/PixelWorldEditorModule.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>

namespace eve::pixelworld_editor {

Module_IMPL(PixelWorldEditorModule, new PixelWorldEditorModule());

eve::Result<void> PixelWorldEditorModule::openCatalog() {
    if (!panel_) panel_ = std::make_unique<PixelWorldCatalogPanel>(PixelWorldCatalogPanel::builtIn());
    auto opened = panel_->open();
    if (!opened) return eve::Result<void>::failure(opened.status());
    return eve::Result<void>::success();
}

void PixelWorldEditorModule::closeCatalog() {
    if (panel_) panel_->close();
}

bool PixelWorldEditorModule::isCatalogOpen() const {
    return panel_ && panel_->isOpen();
}

void PixelWorldEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, PixelWorldEditorModule::create, false);
    expose(module);
}

void PixelWorldEditorModule::expose(ssq::Class& cls) {
    cls.addFunc("openCatalog", [](PixelWorldEditorModule* self) {
        auto result = self->openCatalog();
        if (!result) throw std::runtime_error(result.status().describe());
    });
    cls.addFunc("closeCatalog", [](PixelWorldEditorModule* self) { self->closeCatalog(); });
    cls.addFunc("isCatalogOpen", [](PixelWorldEditorModule* self) {
        return self->isCatalogOpen();
    });
}

}  // namespace eve::pixelworld_editor
