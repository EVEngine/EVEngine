#include "asset/graphics/StaticPrefabRenderInstance.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "common/ECS.h"
#include "graphics/RenderSystem3D.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace eve::asset_graphics {
namespace {
bool validAffine(const glm::mat4& matrix) {
    const auto* values = glm::value_ptr(matrix);
    return std::all_of(values, values + 16, [](float value) { return std::isfinite(value); }) && matrix[0][3] == 0.f &&
           matrix[1][3] == 0.f && matrix[2][3] == 0.f && matrix[3][3] == 1.f &&
           std::isfinite(glm::length(glm::vec3(matrix[0]))) && std::isfinite(glm::length(glm::vec3(matrix[1]))) &&
           std::isfinite(glm::length(glm::vec3(matrix[2])));
}
void setMatrix(graphics::Renderable3D& renderer, const glm::mat4& matrix) {
    renderer.setPosition(matrix[3].x, matrix[3].y, matrix[3].z);
    renderer.setScale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                      glm::length(glm::vec3(matrix[2])));
    renderer.transform()->linkedWorld    = matrix;
    renderer.transform()->hasLinkedWorld = true;
}
}  // namespace
struct StaticPrefabRenderInstance::Impl {
    std::unique_ptr<PreparedStaticPrefabScene> preparation;
    std::vector<ecs::EntityHandle>             entities;
};
StaticPrefabRenderInstance::StaticPrefabRenderInstance(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
StaticPrefabRenderInstance::~StaticPrefabRenderInstance() {
    auto result = release();
    if (!result.ok()) std::fprintf(stderr, "prefab renderer cleanup failed: %s\n", result.status().describe().c_str());
}
Result<std::unique_ptr<StaticPrefabRenderInstance>> StaticPrefabRenderInstance::create(
    std::unique_ptr<PreparedStaticPrefabScene> preparation, const std::array<float, 16>& world) {
    using Output      = Result<std::unique_ptr<StaticPrefabRenderInstance>>;
    const auto matrix = glm::make_mat4(world.data());
    if (!preparation || preparation->parts().empty() || !validAffine(matrix) || ecs::current()->is_deferred())
        return Output::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "prefab renderer requires live nonempty preparation, affine pose and non-deferred ECS"));
    for (const auto& part : preparation->parts()) {
        if (!part.mesh || !part.material || !validAffine(matrix * glm::make_mat4(part.localTransform.data())))
            return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                     "prefab renderer part or composed transform is invalid"));
    }
    std::unique_ptr<StaticPrefabRenderInstance> candidate;
    try {
        candidate.reset(new StaticPrefabRenderInstance(std::make_unique<Impl>(Impl{std::move(preparation), {}})));
        candidate->impl_->entities.reserve(candidate->impl_->preparation->parts().size());
        for (const auto& part : candidate->impl_->preparation->parts()) {
            auto* entity = graphics::Renderable3D::create();
            candidate->impl_->entities.push_back(ecs::handle_of(entity));
            entity->setVisible(false);
            entity->setMesh(part.mesh);
            entity->setMaterial(part.material);
            entity->setCastShadow(part.castShadows);
            entity->setReceiveShadow(part.receiveShadows);
            setMatrix(*entity, matrix * glm::make_mat4(part.localTransform.data()));
        }
    } catch (const std::exception& error) {
        return Output::failure(Diagnostic::error(DiagnosticCode::Failed, error.what()));
    }
    for (const auto& handle : candidate->impl_->entities)
        static_cast<graphics::Renderable3D*>(ecs::try_get(handle))->setVisible(true);
    return Output::success(std::move(candidate));
}
std::size_t StaticPrefabRenderInstance::rendererCount() const noexcept { return impl_->entities.size(); }
Result<graphics::Renderable3D*> StaticPrefabRenderInstance::renderer(std::size_t index) const {
    if (index >= impl_->entities.size())
        return Result<graphics::Renderable3D*>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "prefab renderer index is absent"));
    auto* entity = ecs::try_get(impl_->entities[index]);
    if (!entity)
        return Result<graphics::Renderable3D*>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "prefab renderer entity was deleted or reused"));
    return Result<graphics::Renderable3D*>::success(static_cast<graphics::Renderable3D*>(entity));
}
Result<void> StaticPrefabRenderInstance::setWorldTransform(const std::array<float, 16>& world) {
    if (!impl_->preparation)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab renderer instance was released"));
    const auto matrix = glm::make_mat4(world.data());
    if (!validAffine(matrix))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid prefab world matrix"));
    const auto& parts = impl_->preparation->parts();
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto target = renderer(i);
        if (!target.ok()) return Result<void>::failure(target.status());
        if (!validAffine(matrix * glm::make_mat4(parts[i].localTransform.data())))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab composed matrix is not representable"));
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto* target = static_cast<graphics::Renderable3D*>(ecs::try_get(impl_->entities[i]));
        setMatrix(*target, matrix * glm::make_mat4(parts[i].localTransform.data()));
    }
    return Result<void>::success();
}
Result<void> StaticPrefabRenderInstance::release() {
    for (const auto& handle : impl_->entities) {
        if (auto* entity = static_cast<graphics::Renderable3D*>(ecs::try_get(handle))) {
            entity->setVisible(false);
            entity->setMesh(nullptr);
            entity->setMaterial(nullptr);
            ecs::DestroyEntity(entity);
        }
    }
    impl_->entities.clear();
    if (impl_->preparation) {
        auto result = impl_->preparation->release();
        if (!result.ok()) return result;
        impl_->preparation.reset();
    }
    return Result<void>::success();
}
}  // namespace eve::asset_graphics
