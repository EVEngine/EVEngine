#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <memory>

namespace eve::material_editor {

/** @brief Composition adapter that contributes material editing commands and automation targets. */
class EVENGINE_API_EDITORS MaterialEditorModule final : public Module {
public:
    Module_REG(MaterialEditorModule);
    /** @brief Material editor module. */
    MaterialEditorModule();
    /** @brief Material editor module. */
    ~MaterialEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::material_editor
