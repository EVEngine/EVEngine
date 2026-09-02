#include "material_editor/MaterialEditorModule.h"

#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "graphics/RenderSystem3D.h"
#include "material_editing/MaterialEditingCommands.h"
#include "material_editing/MaterialTarget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <typeindex>

namespace eve::material_editor {
namespace {

const std::int64_t* integerField(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    return found == request.end() ? nullptr : found->second.getIf<std::int64_t>();
}

class AutomationMaterialAssetResolver final : public material_editing::IMaterialRuntimeAssetResolver {
public:
    editor::EditorResult<graphics::Texture*> resolveTexture(const std::string& asset) const override {
        return unresolved<graphics::Texture>(asset);
    }
    editor::EditorResult<graphics::Shader*> resolveShader(const std::string& asset) const override {
        return unresolved<graphics::Shader>(asset);
    }

private:
    template <class T>
    static editor::EditorResult<T*> unresolved(const std::string& asset) {
        return eve::editing::failed<T*>(
            editor::EditorStatus::Unsupported, editor::RuleId("editor.automation.material-asset"),
            "Automation binding cannot resolve material asset: " + asset);
    }
};

graphics::Renderable3D* findRenderable(std::uint32_t id, std::uint32_t generation) {
    ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(graphics::Renderable3D)), id, generation};
    return dynamic_cast<graphics::Renderable3D*>(ecs::try_get(handle));
}

editor::EditorValue runtimeMaterialSnapshot(const graphics::Renderable3D::MeshRenderer& renderer) {
    material_editing::MaterialDocumentTarget defaults("material.runtime.seed");
    editor::EditorValue snapshot = defaults.snapshotValue();
    auto* root = snapshot.getIf<editor::EditorValue::Object>();
    auto* properties = root ? root->at("properties").getIf<editor::EditorValue::Object>() : nullptr;
    (*properties)["shading.tint"] = editor::EditorValue::Array{double(renderer.r), double(renderer.g),
                                                               double(renderer.b), double(renderer.a)};
    (*properties)["shading.metallic"] = double(renderer.metallic);
    (*properties)["shading.roughness"] = double(renderer.roughness);
    (*properties)["parallax.scale"] = double(renderer.parallaxScale);
    (*properties)["lighting.receive"] = renderer.receiveLight;
    (*properties)["shadow.cast"] = renderer.castShadow;
    (*properties)["shadow.receive"] = renderer.receiveShadow;
    return snapshot;
}

}  // namespace

class MaterialEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override {
        return type == "material" || type == "material-renderable3d";
    }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type,
        const editor::EditorValue::Object& request) override {
        editor::AutomationOwnedTarget owned;
        if (type == "material") {
            owned.target = std::make_unique<material_editing::MaterialDocumentTarget>(target.value());
            return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
        }
        const auto* id = integerField(request, "entityId");
        const auto* generation = integerField(request, "generation");
        constexpr auto maxId = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
        if (!id || !generation || *id < 0 || *generation < 0 || *id > maxId || *generation > maxId)
            return eve::editing::failed<editor::AutomationOwnedTarget>(
                editor::EditorStatus::Rejected, editor::RuleId("editor.automation.material-handle"),
                "Live material target requires uint32 entityId and generation");
        auto* renderable = findRenderable(static_cast<std::uint32_t>(*id), static_cast<std::uint32_t>(*generation));
        if (!renderable)
            return eve::editing::failed<editor::AutomationOwnedTarget>(
                editor::EditorStatus::Conflict, editor::RuleId("editor.automation.material-stale"),
                "Renderable3D handle is missing or stale");
        auto renderer = renderable->meshRenderer();
        if (renderer->material || renderer->texture || renderer->normalTexture || renderer->heightTexture ||
            renderer->shader)
            return eve::editing::failed<editor::AutomationOwnedTarget>(
                editor::EditorStatus::Unsupported, editor::RuleId("editor.automation.material-assets"),
                "Live material binding currently supports field-backed materials without asset overrides");
        static const AutomationMaterialAssetResolver assets;
        auto sink = std::make_shared<material_editing::Renderable3DMaterialRuntimeSink>(renderable, &assets);
        auto publishing = std::make_unique<material_editing::MaterialPublishingTarget>(target.value(), sink.get());
        auto loaded = publishing->authoringTarget().loadSnapshot(runtimeMaterialSnapshot(*renderer));
        if (!loaded.ok()) {
            return editor::EditorResult<editor::AutomationOwnedTarget>::failure(loaded.status());
        }
        owned.support = std::move(sink);
        owned.target = std::move(publishing);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(MaterialEditorModule, new MaterialEditorModule());

MaterialEditorModule::MaterialEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !material_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register material editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

MaterialEditorModule::~MaterialEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("material_editing").ignore("material editor adapter shutdown");
}

void MaterialEditorModule::expose(ssq::Table& table) { table.addClass(name, MaterialEditorModule::create, false); }
void MaterialEditorModule::expose(ssq::Class&) {}

}  // namespace eve::material_editor
