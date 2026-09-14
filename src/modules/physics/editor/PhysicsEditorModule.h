#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::physics_editor {

/**
 * @brief Composition adapter that publishes Physics editing capabilities into the live Editor host.
 *
 * The module owns only the provider registration handle. The Editor owns the
 * extension registry and the registered provider owns its factory. Construction
 * and destruction are main/composition-thread-only and invoke no user callbacks.
 */
class PhysicsEditorModule final : public Module {
public:
    Module_REG(PhysicsEditorModule);
    PhysicsEditorModule();
    ~PhysicsEditorModule() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::physics_editor
