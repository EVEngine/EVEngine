#include "decal_editing/DecalTarget.h"

#include "decal/DecalManager.h"

#include <algorithm>
#include <utility>

namespace eve::decal_editing {
namespace {

template <class T>
EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

double number(const DecalDocumentTarget& document, const char* path) {
    return *document.value(path)->getIf<double>();
}

const EditorValue::Array& array(const DecalDocumentTarget& document, const char* path) {
    return *document.value(path)->getIf<EditorValue::Array>();
}

EditorResult<graphics::Texture*> resolve(const DecalDocumentTarget& document,
                                         const IDecalRuntimeAssetResolver* assets, const char* path) {
    const auto& name = *document.value(path)->getIf<std::string>();
    if (name.empty()) return eve::editing::applied<graphics::Texture*>(nullptr);
    if (!assets)
        return fail<graphics::Texture*>(EditorStatus::Rejected, "editor.decal.assets",
                                        "Decal texture resolver is required");
    return assets->texture(name);
}

}  // namespace

EditorResult<void> DecalRuntimeBinding::publish(const DecalDocumentTarget& document) {
    if (!manager_)
        return fail<void>(EditorStatus::Rejected, "editor.decal.manager", "Live DecalManager is required");
    const auto diagnostics = document.validate();
    if (std::any_of(diagnostics.begin(), diagnostics.end(),
                    [](const auto& diagnostic) { return diagnostic.severity() == DiagnosticSeverity::Error; }))
        return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));

    auto albedo = resolve(document, assets_, "texture.albedo");
    if (!albedo.ok()) return EditorResult<void>::failure(albedo.status());
    auto normal = resolve(document, assets_, "texture.normal");
    if (!normal.ok()) return EditorResult<void>::failure(normal.status());
    auto params = resolve(document, assets_, "texture.params");
    if (!params.ok()) return EditorResult<void>::failure(params.status());

    decal::DecalInstance candidate;
    const auto& position = array(document, "transform.position");
    const auto& direction = array(document, "transform.normal");
    const auto& uv = array(document, "texture.uvRect");
    candidate.x = static_cast<float>(*position[0].getIf<double>());
    candidate.y = static_cast<float>(*position[1].getIf<double>());
    candidate.z = static_cast<float>(*position[2].getIf<double>());
    candidate.nx = static_cast<float>(*direction[0].getIf<double>());
    candidate.ny = static_cast<float>(*direction[1].getIf<double>());
    candidate.nz = static_cast<float>(*direction[2].getIf<double>());
    candidate.yaw = static_cast<float>(number(document, "transform.yaw"));
    candidate.size = static_cast<float>(number(document, "projection.size"));
    candidate.depth = static_cast<float>(number(document, "projection.depth"));
    candidate.kind = *document.value("decal.kind")->getIf<std::string>();
    candidate.albedo = albedo.value();
    candidate.normal = normal.value();
    candidate.params = params.value();
    for (int index = 0; index < 4; ++index)
        candidate.uvRect[index] = static_cast<float>(*uv[index].getIf<double>());
    candidate.normalStrength = static_cast<float>(number(document, "channel.normal"));
    candidate.roughnessStrength = static_cast<float>(number(document, "channel.roughness"));
    candidate.metalStrength = static_cast<float>(number(document, "channel.metal"));
    candidate.emissiveStrength = static_cast<float>(number(document, "channel.emissive"));
    candidate.blendMode = *document.value("blend.mode")->getIf<std::string>() == "add" ? 1 : 0;
    candidate.lifetime = static_cast<float>(number(document, "lifetime.seconds"));
    candidate.fadeIn = static_cast<float>(number(document, "lifetime.fadeIn"));
    candidate.fadeOut = static_cast<float>(number(document, "lifetime.fadeOut"));

    const int next = manager_->replace(runtimeId_, std::move(candidate));
    if (next == 0)
        return fail<void>(EditorStatus::Conflict, "editor.decal.replace",
                          "Decal runtime generation is stale or invalid");
    runtimeId_ = next;
    return eve::editing::applied<void>(diagnostics);
}

EditorResult<void> DecalRuntimeBinding::clear() {
    if (runtimeId_ == 0) return eve::editing::noOp();
    if (!manager_ || !manager_->remove(runtimeId_))
        return fail<void>(EditorStatus::NotFound, "editor.decal.runtime-stale",
                          "Published decal generation no longer exists");
    runtimeId_ = 0;
    return eve::editing::applied<void>();
}

EditorResult<void> DecalPublishingTarget::applyDomainOperation(const DomainOperation& operation) {
    if (staging_) return document_.applyDomainOperation(operation);
    auto candidate = cloneDomainState();
    auto applied = candidate->applyDomainOperation(operation);
    if (!applied.ok()) return applied;
    return commitDomainState(std::move(candidate));
}

std::unique_ptr<IDomainOperationTarget> DecalPublishingTarget::cloneDomainState() const {
    auto candidate = std::make_unique<DecalPublishingTarget>(*this);
    candidate->staging_ = true;
    return candidate;
}

EditorResult<void> DecalPublishingTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<DecalPublishingTarget*>(candidate.get());
    if (!typed || typed->targetId() != targetId() || typed->sink_ != sink_ || !typed->staging_)
        return fail<void>(EditorStatus::Conflict, "editor.decal.publish-candidate",
                          "Decal publishing candidate mismatch");
    if (!sink_)
        return fail<void>(EditorStatus::Rejected, "editor.decal.publish-sink",
                          "Decal publishing target requires a runtime sink");
    auto published = sink_->publish(typed->document_);
    if (!published.ok()) return published;
    document_ = typed->document_;
    return eve::editing::applied<void>();
}

}  // namespace eve::decal_editing
