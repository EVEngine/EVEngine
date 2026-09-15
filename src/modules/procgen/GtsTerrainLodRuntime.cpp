#include "procgen/GtsTerrainLodRuntime.h"

#include "common/ECS.h"
#include "graphics/RenderSystem3D.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/Procgen.h"

#include <vector>

namespace eve::procgen {
struct GtsTerrainLodRuntime::Impl {
    std::vector<ecs::EntityHandle> tiles;
    ecs::EntityHandle              sourceTerrain{};
    bool                           sourceWasVisible = false;
    bool                           hasSource        = false;
    std::uint64_t                  revision         = 0;
};
namespace {
graphics::Renderable3D* resolve(const ecs::EntityHandle& handle) {
    return dynamic_cast<graphics::Renderable3D*>(ecs::try_get(handle));
}
void destroy(std::vector<ecs::EntityHandle>& handles) {
    for (auto& h : handles)
        if (auto* entity = resolve(h)) ecs::DestroyEntity(entity);
    handles.clear();
}
void restoreSource(ecs::EntityHandle& handle, bool wasVisible, bool& hasSource) {
    if (hasSource)
        if (auto* source = resolve(handle)) source->setVisible(wasVisible);
    handle    = {};
    hasSource = false;
}
}  // namespace
GtsTerrainLodRuntime::GtsTerrainLodRuntime() : impl_(std::make_unique<Impl>()) {}
GtsTerrainLodRuntime::~GtsTerrainLodRuntime() {
    destroy(impl_->tiles);
    restoreSource(impl_->sourceTerrain, impl_->sourceWasVisible, impl_->hasSource);
}
Result<std::uint64_t> GtsTerrainLodRuntime::replace(const GtsTerrainLodSet& lods, Procgen& procgen,
                                                    graphics::Graphics& gfx, float diameter, float fov, float ox,
                                                    float oy, float oz) {
    std::vector<ecs::EntityHandle> candidate(size_t(lods.getTileCount()));
    for (int i = 0; i < lods.getTileCount(); ++i) {
        auto* tile = lods.tileAt(i);
        if (!tile || tile->levels.empty() || tile->levels.front().empty()) continue;
        auto* entity = graphics::Renderable3D::create();
        if (!entity) {
            destroy(candidate);
            return Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                                    "failed to create GTS terrain tile renderable",
                                                                    "procgen.gtsTerrainLodRuntime"));
        }
        entity->setVisible(false);
        candidate[size_t(i)] = ecs::handle_of(entity);
        auto configured      = procgen.configureGtsTerrainTileLods(lods, i, *entity, gfx, diameter, fov, ox, oy, oz);
        if (!configured) {
            destroy(candidate);
            return Result<std::uint64_t>::failure(*configured.error());
        }
    }
    for (auto& h : impl_->tiles)
        if (auto* entity = resolve(h)) entity->setVisible(false);
    for (auto& h : candidate)
        if (auto* entity = resolve(h)) entity->setVisible(true);
    destroy(impl_->tiles);
    impl_->tiles = std::move(candidate);
    ++impl_->revision;
    if (impl_->revision == 0) ++impl_->revision;
    return Result<std::uint64_t>::success(impl_->revision);
}
Result<std::uint64_t> GtsTerrainLodRuntime::replaceAndHideSource(const GtsTerrainLodSet& lods, Procgen& procgen,
                                                                 graphics::Graphics&     gfx,
                                                                 graphics::Renderable3D& source, float diameter,
                                                                 float fov, float ox, float oy, float oz) {
    for (const auto& handle : impl_->tiles)
        if (resolve(handle) == &source)
            return Result<std::uint64_t>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "GTS source terrain cannot be one of the managed LOD tiles", "sourceTerrain"));
    const auto sourceHandle       = ecs::handle_of(&source);
    const bool sameSource         = impl_->hasSource && resolve(impl_->sourceTerrain) == &source;
    const bool enteringVisibility = sameSource ? impl_->sourceWasVisible : source.getVisible();
    auto       replaced           = replace(lods, procgen, gfx, diameter, fov, ox, oy, oz);
    if (!replaced) return replaced;
    if (!sameSource) restoreSource(impl_->sourceTerrain, impl_->sourceWasVisible, impl_->hasSource);
    impl_->sourceTerrain    = sourceHandle;
    impl_->sourceWasVisible = enteringVisibility;
    impl_->hasSource        = true;
    source.setVisible(false);
    return replaced;
}
graphics::Renderable3D* GtsTerrainLodRuntime::getSourceTerrain() const {
    return impl_->hasSource ? resolve(impl_->sourceTerrain) : nullptr;
}
Result<int> GtsTerrainLodRuntime::clear() {
    int count = 0;
    for (auto& h : impl_->tiles)
        if (resolve(h)) ++count;
    destroy(impl_->tiles);
    restoreSource(impl_->sourceTerrain, impl_->sourceWasVisible, impl_->hasSource);
    ++impl_->revision;
    if (impl_->revision == 0) ++impl_->revision;
    return Result<int>::success(count);
}
Result<int> GtsTerrainLodRuntime::applyMaterial(graphics::Material& material) {
    int count = 0;
    for (auto& h : impl_->tiles)
        if (auto* entity = resolve(h)) {
            entity->setMaterial(&material);
            ++count;
        }
    return Result<int>::success(count);
}
int                     GtsTerrainLodRuntime::getTileCount() const { return int(impl_->tiles.size()); }
std::uint64_t           GtsTerrainLodRuntime::getRevision() const { return impl_->revision; }
graphics::Renderable3D* GtsTerrainLodRuntime::getRenderable(int index) const {
    return index >= 0 && index < int(impl_->tiles.size()) ? resolve(impl_->tiles[size_t(index)]) : nullptr;
}
}  // namespace eve::procgen
