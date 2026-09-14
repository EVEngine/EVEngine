#include "fluids/VolumeFluidShadowCaster.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"

#include <exception>
#include <glm/gtc/quaternion.hpp>

namespace eve::fluids {
namespace {
Status failure(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::Failed, message, "fluids.volume.shadowCaster"));
}
}  // namespace

VolumeFluidShadowCaster::VolumeFluidShadowCaster(graphics::Graphics& graphics) : graphics_(&graphics) {}

VolumeFluidShadowCaster::~VolumeFluidShadowCaster() {
    graphics::RenderSystem3D::removeShadowExtraDrawer(drawerToken_);
    if (graphics_ && mesh_ && graphics_->releaseMesh(mesh_)) delete mesh_;
}

Result<void> VolumeFluidShadowCaster::update(const VolumeFluid& fluid, unsigned actorGroup, float radiusScale,
                                             float alpha, unsigned maxParticles) {
    std::vector<VolumeFluidParticleInstance> candidate;
    candidate.swap(instances_);
    auto copied = fluid.copyParticleImpostors(actorGroup, radiusScale, glm::vec4(1.f), alpha, maxParticles, candidate);
    if (!copied) {
        candidate.swap(instances_);
        return Result<void>::failure(copied.status());
    }
    const size_t count = candidate.size();
    positions_.resize(count * 18u);
    normals_.resize(count * 18u);
    indices_.resize(count * 24u);
    static constexpr glm::vec3 directions[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static constexpr uint32_t  faces[24]     = {0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4, 2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5};
    for (size_t i = 0; i < count; ++i) {
        const auto q = glm::quat(candidate[i].orientation.w, candidate[i].orientation.x, candidate[i].orientation.y,
                                 candidate[i].orientation.z);
        for (size_t v = 0; v < 6; ++v) {
            const glm::vec3 local  = directions[v] * candidate[i].scale;
            const glm::vec3 normal = q * directions[v];
            const glm::vec3 point  = candidate[i].position + q * local;
            for (size_t c = 0; c < 3; ++c) {
                positions_[(i * 6 + v) * 3 + c] = point[c];
                normals_[(i * 6 + v) * 3 + c]   = normal[c];
            }
        }
        for (size_t k = 0; k < 24; ++k) indices_[i * 24 + k] = uint32_t(i * 6 + faces[k]);
    }
    try {
        if (count != 0 && !mesh_)
            mesh_ = graphics_->newMeshFromArrays(positions_.data(), normals_.data(), nullptr, int(count * 6),
                                                 indices_.data(), int(count * 24));
        else if (count != 0 && !graphics_->updateMeshVertices(mesh_, positions_.data(), normals_.data(), nullptr,
                                                              int(count * 6), indices_.data(), int(count * 24)))
            return Result<void>::failure(failure("Particle shadow mesh update failed"));
    } catch (const std::exception&) {
        return Result<void>::failure(failure("Particle shadow mesh creation failed"));
    }
    if (mesh_ && drawerToken_ == 0)
        drawerToken_ = graphics::RenderSystem3D::addShadowExtraDrawer(
            [this](graphics::Graphics& gfx, const glm::mat4& lightVP, const auto&) {
                if (particleCount_ != 0) gfx.drawMeshShadow(mesh_, lightVP);
            });
    instances_.swap(candidate);
    particleCount_ = unsigned(count);
    return Result<void>::success();
}

}  // namespace eve::fluids
