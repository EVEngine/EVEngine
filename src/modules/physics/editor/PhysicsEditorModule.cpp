#include "physics/editor/PhysicsEditorModule.h"

#include "editing/EditingExtension.h"
#include "editor/Editor.h"
#include "physics/editing/PhysicsEditingProvider.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <utility>

namespace eve::physics_editor {

struct PhysicsEditorModule::Impl {
    editing::ProviderHandle provider;
};

Module_IMPL(PhysicsEditorModule, new PhysicsEditorModule());

PhysicsEditorModule::PhysicsEditorModule() : impl_(std::make_unique<Impl>()) {
    auto* editor = requireModInst(eve::editor, Editor);
    if (!editor) throw std::runtime_error("Physics editor requires the Editor host");
    auto registered = physics_editing::registerEditingProvider(editor->extensionProviders());
    if (!registered.ok())
        throw std::runtime_error("Failed to register the Physics editing provider: " +
                                 registered.status().describe());
    impl_->provider = std::move(registered).takeValue();
}

PhysicsEditorModule::~PhysicsEditorModule() {
    auto* editor = getModInst(eve::editor, Editor);
    if (!editor || impl_->provider.id.empty()) return;
    editor->extensionProviders()
        .unload(impl_->provider)
        .ignore("physics editor adapter shutdown");
}

void PhysicsEditorModule::expose(ssq::Table& table) {
    table.addClass(name, PhysicsEditorModule::create, false);
}

void PhysicsEditorModule::expose(ssq::Class&) {}

}  // namespace eve::physics_editor
