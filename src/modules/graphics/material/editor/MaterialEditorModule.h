#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::material_editor {

/** @brief Composition adapter that contributes material editing commands and automation targets. */
class MaterialEditorModule final : public Module {
public:
    Module_REG(MaterialEditorModule);
    MaterialEditorModule();
    ~MaterialEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::material_editor
