// eve-hot-path
// Scene-frame admission uses metadata only; explicit preparation owns full-grid capture work.
#include "map/spawn/MapSpawnNavigation.inl"
#include <exception>
#include "common/Assert.h"
#include "map/Map.h"
#include "map/spawn/MapSpawnSpace.h"

namespace eve::map_spawn {
namespace {
template <class T>
Result<T> navigationFailure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "map_spawn.navigation"));
}
bool sameHandle(const ecs::EntityHandle& a, const ecs::EntityHandle& b) {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
}  // namespace
auto MapSpawnSession::NavigationBinding::observe(map::Map& target) const -> Result<Observation> {
    if (!owner.isCurrent() || !owner.hasSameOwner(target.observeLifetime()))
        return navigationFailure<Observation>(DiagnosticCode::StaleHandle, "Map owner is revoked or mismatched");
    if (phase != Phase::Ready)
        return navigationFailure<Observation>(DiagnosticCode::PreconditionViolation, "Scene is closing or closed");
    auto* layer = target.getLayer(static_cast<int>(index));
    if (!layer || !ecs::is_entity_visible(layer))
        return navigationFailure<Observation>(DiagnosticCode::StaleHandle, "Navigation layer was removed");
    if (lastLayer && !sameHandle(*lastLayer, ecs::handle_of(layer)))
        return navigationFailure<Observation>(DiagnosticCode::Conflict,
                                              "Map layer incarnation changed; prepare a new scene");
    auto& manager  = layer->getComponentManager();
    auto* resource = manager.getComponentBuffer<map::TileLayer::Resource>();
    auto* tiles    = manager.getComponentBuffer<map::TileLayer::Tiles>();
    if (!resource || !resource->has(layer->id) || !tiles || !tiles->has(layer->id))
        return navigationFailure<Observation>(DiagnosticCode::NotFound, "Navigation layer lacks canonical components");
    if (resource->ptr(layer->id)->path != asset)
        return navigationFailure<Observation>(DiagnosticCode::Conflict, "Map asset changed; prepare a new scene");
    auto current = map::describeMapGrid(*layer);
    if (!current) return Result<Observation>::failure(current.status());
    if (current.value() != geometry)
        return navigationFailure<Observation>(DiagnosticCode::Unsupported,
                                              "Map coordinate frame changed; prepare a new scene");
    return Result<Observation>::success(
        {std::ref(*layer), ecs::handle_of(layer), Revision(tiles->ptr(layer->id)->revision)});
}
void MapSpawnSession::bindPublishedNavigation(map::Map& target) noexcept {
    auto* layer = target.getLayer(static_cast<int>(navigation_->index));
    if (!layer) {
        EV_ASSERT(false, "Published navigation layer missing");
        std::terminate();
    }
    auto* tiles = layer->getComponentManager().getComponentBuffer<map::TileLayer::Tiles>();
    if (!tiles || !tiles->has(layer->id)) {
        EV_ASSERT(false, "Published navigation tiles missing");
        std::terminate();
    }
    navigation_->lastLayer    = ecs::handle_of(layer);
    navigation_->lastRevision = Revision(tiles->ptr(layer->id)->revision);
}
Result<bool> MapSpawnSession::navigationNeedsRefresh(map::Map& target) const {
    auto observed = navigation_->observe(target);
    if (!observed) return Result<bool>::failure(observed.status());
    return Result<bool>::success(!navigation_->lastLayer ||
                                 !sameHandle(*navigation_->lastLayer, observed.value().handle) ||
                                 navigation_->lastRevision != observed.value().revision);
}
struct PreparedMapNavigation::Impl {
    std::weak_ptr<MapSpawnSession::NavigationBinding> binding;
    std::weak_ptr<MapSpawnSpace>                      space;
    map::NavigationGridSnapshot                       capture;
    ecs::EntityHandle                                 layer;
    Revision                                          revision;
    bool                                              consumed = false;
};
PreparedMapNavigation::PreparedMapNavigation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedMapNavigation::~PreparedMapNavigation()                                           = default;
PreparedMapNavigation::PreparedMapNavigation(PreparedMapNavigation&&) noexcept            = default;
PreparedMapNavigation& PreparedMapNavigation::operator=(PreparedMapNavigation&&) noexcept = default;
Result<std::unique_ptr<PreparedMapNavigation>> MapSpawnSession::prepareNavigationRefresh(map::Map& target) {
    if (navigation_->advancing)
        return navigationFailure<std::unique_ptr<PreparedMapNavigation>>(DiagnosticCode::PreconditionViolation,
                                                                         "Cannot refresh during scene advance");
    auto observed = navigation_->observe(target);
    if (!observed) return Result<std::unique_ptr<PreparedMapNavigation>>::failure(observed.status());
    try {
        map::Pathfinder source(&observed.value().layer.get());
        auto            capture = source.captureNavigationGrid();
        if (!capture) return Result<std::unique_ptr<PreparedMapNavigation>>::failure(capture.status());
        auto impl      = std::make_unique<PreparedMapNavigation::Impl>();
        impl->binding  = navigation_;
        impl->space    = space_;
        impl->capture  = std::move(capture).takeValue();
        impl->layer    = observed.value().handle;
        impl->revision = observed.value().revision;
        return Result<std::unique_ptr<PreparedMapNavigation>>::success(
            std::unique_ptr<PreparedMapNavigation>(new PreparedMapNavigation(std::move(impl))));
    } catch (const std::exception& error) {
        return navigationFailure<std::unique_ptr<PreparedMapNavigation>>(DiagnosticCode::Failed, error.what());
    }
}
Result<spawn::Change> PreparedMapNavigation::commit(map::Map& target) {
    if (!impl_) return navigationFailure<spawn::Change>(DiagnosticCode::StaleHandle, "Moved navigation token");
    auto binding = impl_->binding.lock();
    auto space   = impl_->space.lock();
    if (!binding || !space) return navigationFailure<spawn::Change>(DiagnosticCode::StaleHandle, "Scene destroyed");
    if (binding->advancing)
        return navigationFailure<spawn::Change>(DiagnosticCode::PreconditionViolation,
                                                "Cannot refresh during scene advance");
    auto observed = binding->observe(target);
    if (!observed) return Result<spawn::Change>::failure(observed.status());
    if (!sameHandle(impl_->layer, observed.value().handle) || impl_->revision != observed.value().revision)
        return navigationFailure<spawn::Change>(DiagnosticCode::StaleHandle, "Map changed after navigation capture");
    if (impl_->consumed || (binding->lastLayer && sameHandle(*binding->lastLayer, impl_->layer) &&
                            binding->lastRevision == impl_->revision))
        return Result<spawn::Change>::success(spawn::Change::Unchanged);
    auto applied = space->setNavigation(impl_->capture);
    if (!applied) return Result<spawn::Change>::failure(applied.status());
    binding->lastLayer    = impl_->layer;
    binding->lastRevision = impl_->revision;
    impl_->consumed       = true;
    return Result<spawn::Change>::success(spawn::Change::Changed);
}
Result<spawn::AdvanceReport> MapSpawnSession::advance(map::Map& target, const SimulationStep& step,
                                                      std::span<const spawn::LifecycleInput> lifecycle,
                                                      spawn::ActivationFrame                 activation) {
    if (navigation_->advancing)
        return navigationFailure<spawn::AdvanceReport>(DiagnosticCode::PreconditionViolation, "Scene advance reentry");
    auto dirty = navigationNeedsRefresh(target);
    if (!dirty) return Result<spawn::AdvanceReport>::failure(dirty.status());
    if (dirty.value())
        return navigationFailure<spawn::AdvanceReport>(DiagnosticCode::PreconditionViolation,
                                                       "Prepare and commit navigation refresh before advance");
    navigation_->advancing = true;
    struct Guard {
        bool& value;
        ~Guard() { value = false; }
    } guard{navigation_->advancing};
    std::optional<std::vector<spawn::Point>> previous;
    if (activation.players) {
        auto replaced = space_->replacePlayers(*activation.players);
        if (!replaced) return Result<spawn::AdvanceReport>::failure(replaced.status());
        previous = std::move(replaced).takeValue();
    }
    try {
        auto report = world_->advance(step, lifecycle, activation);
        if (!report && activation.players) space_->restorePlayers(std::move(previous));
        return report;
    } catch (...) {
        if (activation.players) space_->restorePlayers(std::move(previous));
        throw;
    }
}
Result<void> MapSpawnSession::enqueue(spawn::ControlCommand command) {
    if (navigation_->phase != NavigationBinding::Phase::Ready)
        return navigationFailure<void>(DiagnosticCode::PreconditionViolation, "Scene is closing or closed");
    return world_->enqueue(command);
}
Result<Subscription> MapSpawnSession::subscribe(Observer<spawn::SpawnEvent>::Callback callback) {
    return world_->subscribe(std::move(callback));
}
}  // namespace eve::map_spawn
