// eve-hot-path
// describeMapGrid is constant frame metadata work; preparation/publication remain explicit boundaries.
#include "map/PreparedMapLoad.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <type_traits>
#include "common/BorrowedRef.h"
#include "map/Map.h"
#include "map/MapGridStateInternal.inl"
#include "map/MapObjectContract.h"
#include "map/Pathfinder.h"
#include "map/TileConfigInternal.inl"
#include "map/TileGridInternal.h"
#include "map/TileProjection.h"

namespace eve::map {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "map.prepare"));
}
}  // namespace
struct PreparedMapLoad::Impl {
    MapLease                       owner;
    std::unique_ptr<ecs::Table>    table = std::make_unique<ecs::Table>();
    std::vector<ecs::EntityHandle> layers;
    std::vector<MapObject>         objects;
    bool                           consumed = false;
    ResultRef<TileLayer>           layer(std::size_t index) const {
        if (consumed || !owner.isCurrent())
            return ResultRef<TileLayer>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "Map candidate consumed or owner destroyed"));
        if (index >= layers.size())
            return ResultRef<TileLayer>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Candidate layer index out of range"));
        auto* entity = ecs::try_get(layers[index]);
        if (!entity)
            return ResultRef<TileLayer>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "Candidate entity revoked"));
        return ResultRef<TileLayer>::success(std::ref(*static_cast<TileLayer*>(entity)));
    }
};
PreparedMapLoad::PreparedMapLoad(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedMapLoad::~PreparedMapLoad()                                     = default;
PreparedMapLoad::PreparedMapLoad(PreparedMapLoad&&) noexcept            = default;
PreparedMapLoad& PreparedMapLoad::operator=(PreparedMapLoad&&) noexcept = default;
MapLease         PreparedMapLoad::ownerLease() const noexcept { return impl_ ? impl_->owner : MapLease{}; }
std::size_t      PreparedMapLoad::layerCount() const noexcept {
    return !impl_ || impl_->consumed ? 0 : impl_->layers.size();
}
Result<std::unique_ptr<PreparedMapLoad>> PreparedMapLoad::prepare(MapLease owner, std::string_view json,
                                                                  const std::string& path, service::IFileSystem& files,
                                                                  std::string_view contract) {
    if (!owner.isCurrent())
        return failure<std::unique_ptr<PreparedMapLoad>>(DiagnosticCode::StaleHandle, "Map owner destroyed");
    try {
        auto impl   = std::make_unique<Impl>();
        impl->owner = std::move(owner);
        ecs::ScopedTable scope(*impl->table);
        std::string      error;
        auto             layers = detail::prepareMapAssetText(std::string(json), path, files, &impl->objects, &error);
        if (layers.empty())
            return failure<std::unique_ptr<PreparedMapLoad>>(
                DiagnosticCode::ParseError, error.empty() ? "No candidate map layers" : std::move(error));
        auto admitted = validateMapObjects(impl->objects, contract);
        if (!admitted) return Result<std::unique_ptr<PreparedMapLoad>>::failure(admitted.status());
        impl->layers.reserve(layers.size());
        for (auto* layer : layers) impl->layers.push_back(ecs::handle_of(layer));
        return Result<std::unique_ptr<PreparedMapLoad>>::success(
            std::unique_ptr<PreparedMapLoad>(new PreparedMapLoad(std::move(impl))));
    } catch (const std::exception& error) {
        return failure<std::unique_ptr<PreparedMapLoad>>(DiagnosticCode::Failed, error.what());
    } catch (...) {
        return failure<std::unique_ptr<PreparedMapLoad>>(DiagnosticCode::CallbackFailure, "Map asset provider threw");
    }
}
Result<MapGridGeometry> PreparedMapLoad::grid(std::size_t index) const {
    if (!impl_) return failure<MapGridGeometry>(DiagnosticCode::StaleHandle, "Moved map candidate");
    auto layer = impl_->layer(index);
    if (!layer) return Result<MapGridGeometry>::failure(layer.status());
    return describeMapGrid(layer.value().get());
}
Result<MapGridGeometry> describeMapGrid(TileLayer& layer) {
    auto* buffer = layer.getComponentManager().getComponentBuffer<TileLayer::Config>();
    if (!buffer || !buffer->has(layer.id))
        return failure<MapGridGeometry>(DiagnosticCode::NotFound, "Layer lacks grid configuration");
    const auto& cfg = *buffer->ptr(layer.id);
    float       x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    tileToWorld(cfg, 0, 0, x0, y0);
    tileToWorld(cfg, 1, 0, x1, y1);
    tileToWorld(cfg, 0, 1, x2, y2);
    MapGridGeometry result{x0, y0, x1 - x0, y2 - y0, cfg.cellGapX, cfg.cellGapY, cfg.orientation};
    if (!std::isfinite(result.originX) || !std::isfinite(result.originY) || !std::isfinite(result.stepX) ||
        !std::isfinite(result.stepY) || !std::isfinite(result.gapX) || !std::isfinite(result.gapY))
        return failure<MapGridGeometry>(DiagnosticCode::InvalidArgument, "Nonfinite layer geometry");
    return Result<MapGridGeometry>::success(result);
}
Result<NavigationGridSnapshot> PreparedMapLoad::captureNavigation(std::size_t index) const {
    if (!impl_) return failure<NavigationGridSnapshot>(DiagnosticCode::StaleHandle, "Moved map candidate");
    auto layer = impl_->layer(index);
    if (!layer) return Result<NavigationGridSnapshot>::failure(layer.status());
    Pathfinder navigation(&layer.value().get());
    return navigation.captureNavigationGrid();
}
Result<void> PreparedMapLoad::applyGridState(const MapGridState& state) {
    if (!impl_ || impl_->consumed || !impl_->owner.isCurrent())
        return failure<void>(DiagnosticCode::StaleHandle, "Map candidate consumed or owner revoked");
    if (impl_->layers.size() != state.impl_->layers.size())
        return failure<void>(DiagnosticCode::Conflict, "Runtime and structural layer counts disagree");
    struct Attempt {
        TileLayer::Config  config;
        TileLayer::Tiles   tiles;
        TileLayer::Tileset tileset;
    };
    std::vector<Attempt> attempts;
    attempts.reserve(impl_->layers.size());
    for (std::size_t i = 0; i < impl_->layers.size(); ++i) {
        auto source = impl_->layer(i);
        if (!source) return Result<void>::failure(source.status());
        auto&       layer = source.value().get();
        const auto& saved = state.impl_->layers[i];
        if (layer.resource()->path != saved.path)
            return failure<void>(DiagnosticCode::Conflict, "Runtime layer source does not match structural layer");
        Attempt attempt;
        attempt.config           = saved.config;
        attempt.config.entity    = &layer;
        attempt.tiles.gids       = saved.gids;
        attempt.tiles.terrainIds = saved.terrain;
        attempt.tiles.revision   = saved.revision - 1;
        detail::rebuildTileChunks(attempt.tiles, saved.config.mapW, saved.config.mapH);
        attempt.tileset = *layer.tileset();
        for (auto& visual : attempt.tileset.visuals) {
            visual.footprintW = visual.footprintH = 1;
            visual.walkable                       = true;
            visual.opaque                         = false;
            visual.cost                           = 1;
            visual.enterMask = visual.exitMask = 255;
            visual.semanticFlags               = 0;
        }
        for (const auto& profile : saved.profiles) {
            auto found = std::find_if(attempt.tileset.visuals.begin(), attempt.tileset.visuals.end(),
                                      [&](const auto& visual) { return visual.gid == profile.gid; });
            if (found == attempt.tileset.visuals.end()) {
                attempt.tileset.visuals.emplace_back();
                found      = std::prev(attempt.tileset.visuals.end());
                found->gid = profile.gid;
            }
            found->footprintW    = profile.footprintW;
            found->footprintH    = profile.footprintH;
            found->walkable      = profile.walkable;
            found->opaque        = profile.opaque;
            found->cost          = profile.cost;
            found->enterMask     = profile.enter;
            found->exitMask      = profile.exit;
            found->semanticFlags = profile.flags;
        }
        attempt.tileset.terrainRules    = saved.rules;
        attempt.tileset.terrainFamilies = saved.families;
        attempt.tileset.customData      = saved.custom;
        attempts.push_back(std::move(attempt));
    }
    static_assert(std::is_nothrow_swappable_v<TileLayer::Tiles> && std::is_nothrow_swappable_v<TileLayer::Tileset>);
    for (std::size_t i = 0; i < attempts.size(); ++i) {
        auto* layer      = static_cast<TileLayer*>(ecs::try_get(impl_->layers[i]));
        *layer->config() = attempts[i].config;
        std::swap(*layer->tiles(), attempts[i].tiles);
        std::swap(*layer->tileset(), attempts[i].tileset);
    }
    return Result<void>::success();
}
Result<int> PreparedMapLoad::commit(Map& target) {
    if (!impl_ || impl_->consumed || !impl_->owner.isCurrent() || impl_->owner.owner_.lock() != target.lifetime_)
        return failure<int>(DiagnosticCode::StaleHandle, "Map load candidate belongs to a revoked/different owner");
    if (ecs::current()->is_deferred())
        return failure<int>(DiagnosticCode::PreconditionViolation, "Map publication requires owner outside Views");
    std::vector<TileLayer*> created;
    try {
        auto objects = impl_->objects;
        created.reserve(impl_->layers.size());
        for (std::size_t index = 0; index < impl_->layers.size(); ++index) {
            auto source = impl_->layer(index);
            if (!source) {
                for (auto* layer : created) ecs::DestroyEntity(layer);
                return Result<int>::failure(source.status());
            }
            auto& candidate = source.value().get();
            auto* layer     = TileLayer::create();
            created.push_back(layer);
            *layer->config()        = *candidate.config();
            layer->config()->entity = layer;
            *layer->tiles()         = *candidate.tiles();
            *layer->tileset()       = *candidate.tileset();
            *layer->draw()          = *candidate.draw();
            *layer->resource()      = *candidate.resource();
        }
        // All allocations/copies are complete; publication only hides/clears prior layers and moves vectors.
        target.publishPreparedLayers(std::move(created), std::move(objects));
        impl_->consumed = true;
        return Result<int>::success(static_cast<int>(impl_->layers.size()));
    } catch (const std::exception& error) {
        for (auto* layer : created) ecs::DestroyEntity(layer);
        return failure<int>(DiagnosticCode::Failed, error.what());
    } catch (...) {
        for (auto* layer : created) ecs::DestroyEntity(layer);
        return failure<int>(DiagnosticCode::CallbackFailure, "Candidate publication threw");
    }
}
}  // namespace eve::map
