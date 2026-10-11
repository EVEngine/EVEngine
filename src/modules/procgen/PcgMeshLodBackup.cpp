#include "procgen/PcgMeshLodBackup.h"

#include "graphics/RenderSystem3D.h"

#include <optional>
#include <utility>

namespace eve::procgen {

struct PcgMeshLodBackup::Impl {
    std::optional<graphics::Renderable3D::MeshRenderer> renderer;
    std::uint32_t entityId = 0;
    std::uint32_t entityGeneration = 0;
};

PcgMeshLodBackup::PcgMeshLodBackup() : impl_(std::make_unique<Impl>()) {}
PcgMeshLodBackup::~PcgMeshLodBackup() = default;
PcgMeshLodBackup::PcgMeshLodBackup(PcgMeshLodBackup&&) noexcept = default;
PcgMeshLodBackup& PcgMeshLodBackup::operator=(PcgMeshLodBackup&&) noexcept = default;

Result<void> PcgMeshLodBackup::capture(graphics::Renderable3D& renderable) {
    Impl candidate;
    candidate.renderer = *renderable.meshRenderer();
    candidate.entityId = renderable.getEntityId();
    candidate.entityGeneration = renderable.getEntityGeneration();
    *impl_ = std::move(candidate);
    return Result<void>::success();
}

Result<void> PcgMeshLodBackup::restore(graphics::Renderable3D& renderable) {
    if (!impl_->renderer)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Pcg mesh LOD backup is empty", "procgen.pcgMeshLodBackup"));
    if (renderable.getEntityId() != impl_->entityId ||
        renderable.getEntityGeneration() != impl_->entityGeneration)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Pcg mesh LOD backup belongs to another entity generation",
            "procgen.pcgMeshLodBackup"));
    auto restored = *impl_->renderer;
    *renderable.meshRenderer() = std::move(restored);
    discard();
    return Result<void>::success();
}

void PcgMeshLodBackup::discard() noexcept {
    impl_->renderer.reset();
    impl_->entityId = 0;
    impl_->entityGeneration = 0;
}

bool PcgMeshLodBackup::isCaptured() const noexcept { return impl_->renderer.has_value(); }
std::uint32_t PcgMeshLodBackup::getEntityId() const noexcept { return impl_->entityId; }
std::uint32_t PcgMeshLodBackup::getEntityGeneration() const noexcept { return impl_->entityGeneration; }

}  // namespace eve::procgen
