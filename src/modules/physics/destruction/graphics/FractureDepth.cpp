#include <algorithm>
#include <exception>
#include <functional>
#include <glm/mat4x4.hpp>
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "physics/destruction/graphics/GeometryCollectionRenderer.h"
namespace eve::physics {
struct GeometryCollectionRenderer::DepthRegistration {
    graphics::RenderSystem3D::GBufferExtraDrawer drawer;
};
eve::Result<void> GeometryCollectionRenderer::registerDepthContribution() {
    if (depthRegistration_) return eve::Result<void>::success();
    // The legacy GBuffer hook has process lifetime. Install exactly one dispatcher;
    // it owns no renderer, mesh or Graphics. Expired weak leases are pruned on
    // registration and dispatch, so repeated scenes do not accumulate callbacks.
    static std::vector<std::weak_ptr<DepthRegistration>> registrations;
    static bool                                          installed = false;
    try {
        auto candidate    = std::make_shared<DepthRegistration>();
        candidate->drawer = [this](graphics::Graphics& target, const graphics::Camera3D::Data& camera,
                                   const glm::mat4& vp, float) {
            if (!submitted_ || &target != meshOwner_) return;
            for (const auto& batch : fragmentBatches_) {
                if (!batch.mesh) continue;
                auto color = batch.interior ? std::array<float, 4>{interiorR_, interiorG_, interiorB_, interiorA_}
                                            : std::array<float, 4>{exteriorR_, exteriorG_, exteriorB_, exteriorA_};
                if (!batch.interior)
                    if (auto found = groupColors_.find(batch.groupId); found != groupColors_.end())
                        color = found->second;
                target.drawMeshGBuffer(batch.mesh, vp, glm::mat4(1.f), camera.nearZ, camera.farZ, nullptr, color[0],
                                       color[1], color[2], 0.f, 0.f, .85f, 0.f);
            }
        };
        if (!installed) {
            graphics::RenderSystem3D::addGBufferExtraDrawer([](graphics::Graphics&             target,
                                                               const graphics::Camera3D::Data& camera,
                                                               const glm::mat4& vp, float aspect) {
                std::erase_if(registrations, [](const auto& weak) { return weak.expired(); });
                for (const auto& weak : registrations)
                    if (auto live = weak.lock()) live->drawer(target, camera, vp, aspect);
            });
            installed = true;
        }
        std::erase_if(registrations, [](const auto& weak) { return weak.expired(); });
        registrations.push_back(candidate);
        depthRegistration_ = std::move(candidate);
        return eve::Result<void>::success();
    } catch (const std::exception& error) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "fragmentDepth"));
    }
}
}  // namespace eve::physics
