// eve-hot-path
// ECS: delegates identity lookup, canonical HP write and structural creation/cleanup to actual RPG/map owners.
// No local View/System. Events use a private owning scope.
// Phase: explicit authoring preview step -> owning diagnostic capture -> presentation. Time/RNG are injected.
#include "map/rpg_spawn/editor/RPGSpawnPreview.h"
#include <cmath>
#include <limits>
#include "common/ECS.h"
#include "common/ServiceInterfaces.h"
#include "common/Uri.h"
#include "graphics/ISolidRectRenderer.h"
#include "map/Map.h"
#include "map/rpg_spawn/MapRPGPresentation.h"
#include "map/rpg_spawn/RPGMapSession.h"
#include "map/spawn/MapSpawnBinding.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/MapSpawnSpace.h"
#include "map/spawn/editor/SpawnForm.h"
#include "rpg/SimulationEvents.h"
#include "rpg/spawn/RPGSpawnFactory.h"
#include "spawn/SpawnDiagnostics.h"
namespace eve::map_rpg_spawn_editor {
namespace {
// Stack-only read view: substitutes the frozen admitted sidecar, never writes or survives asset preparation.
class PreviewFiles final : public service::IFileSystem {
public:
    service::IFileSystem&     base;
    std::string               path;
    std::vector<std::uint8_t> bytes;
    PreviewFiles(service::IFileSystem& files, std::string name, std::string json)
        : base(files), path(std::move(name)), bytes(json.begin(), json.end()) {}
    bool readFile(const std::string& name, std::vector<std::uint8_t>& output) override {
        if (name != path) return base.readFile(name, output);
        output = bytes;
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& name) override { return name == path || base.fileExists(name); }
};
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
template <class T>
Result<T> unavailable() {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Preview stopped or busy"));
}
}  // namespace
struct RPGSpawnPreview::Impl {
    std::shared_ptr<ecs::Table>                   world = std::make_shared<ecs::Table>();
    std::unique_ptr<map::Map>                     map;
    std::unique_ptr<map_rpg_spawn::RPGMapSession> session;
    Revision                                      revision;
    rpg::detail::SimulationEvents                 events;
    mutable bool                                  busy = false;
};
RPGSpawnPreview::RPGSpawnPreview(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGSpawnPreview::~RPGSpawnPreview() = default;
Result<std::unique_ptr<RPGSpawnPreview>> RPGSpawnPreview::create(
    const Value& inputBinding, std::string contract, service::IFileSystem& files,
    spawn_editor::FormSourceSnapshot source, const spawn::ITemplateResolver& templates,
    std::shared_ptr<rpg_spawn::RPGUnitCatalogue> units, PersistentId owner, std::uint64_t seed, double radius) {
    using R = Result<std::unique_ptr<RPGSpawnPreview>>;
    if (source.revision.value() == 0)
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Committed source revision required"));
    const auto binding = inputBinding;
    auto       decoded = map_spawn::MapSpawnBinding::decode(binding);
    if (!decoded) return R::failure(decoded.status());
    auto path = ResourceUri::parse(decoded.value().spawnsUri());
    if (!path) return R::failure(path.status());
    auto json = source.source.toJson();
    if (!json) return R::failure(json.status());
    PreviewFiles view(files, std::string(path.value().path()), std::move(json).takeValue());
    auto         state = std::make_unique<Impl>();
    state->revision    = source.revision;
    ecs::ScopedTable                  scope(*state->world);
    rpg::detail::SimulationEventScope eventScope(state->events);
    state->map     = std::make_unique<map::Map>();
    auto candidate = map_rpg_spawn::PreparedRPGMapSession::prepare(*state->map, binding, view, templates, state->world,
                                                                   std::move(units), owner, seed, contract, radius, {});
    if (!candidate) return R::failure(candidate.status());
    auto committed = candidate.value().commit(*state->map);
    if (!committed) return R::failure(committed.status());
    state->session = std::move(committed).takeValue();
    auto tracing   = state->session->resetPlacementTrace(map_spawn::PlacementTraceMode::RecentAttempts);
    if (!tracing) return R::failure(tracing.status());
    return R::success(std::unique_ptr<RPGSpawnPreview>(new RPGSpawnPreview(std::move(state))));
}
Result<spawn::AdvanceReport> RPGSpawnPreview::advance(const SimulationStep& step, spawn::ActivationFrame activation) {
    if (!impl_->session || impl_->busy) return unavailable<spawn::AdvanceReport>();
    Guard                             guard(impl_->busy);
    ecs::ScopedTable                  scope(*impl_->world);
    rpg::detail::SimulationEventScope eventScope(impl_->events);
    auto result = impl_->session->advance(*impl_->map, step, impl_->events.vitals, activation);
    if (result) impl_->events.vitals.clear();
    return result;
}
Result<spawn::Change> RPGSpawnPreview::defeat(SubjectRef subject) {
    using R = Result<spawn::Change>;
    if (!impl_->session || impl_->busy) return unavailable<spawn::Change>();
    Guard guard(impl_->busy);
    auto  facts = impl_->session->spawning().world().captureDiagnostics();
    if (!facts) return R::failure(facts.status());
    std::optional<spawn::SpawnerHandle> origin;
    for (const auto& source : facts.value().sources)
        for (const auto member : source.state.subjects)
            if (member == subject) origin = source.source;
    if (!origin)
        return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "No current preview member has this identity"));
    ecs::ScopedTable                  scope(*impl_->world);
    rpg::detail::SimulationEventScope eventScope(impl_->events);
    auto                              actor = impl_->session->inspectActor(subject);
    if (!actor) return R::failure(actor.status());
    const auto hp = actor.value().hp;
    if (hp <= 0) return R::success(spawn::Change::Unchanged);
    auto damaged = impl_->session->applyDamage(subject, hp);
    if (!damaged) return R::failure(damaged.status());
    const auto applied = damaged.value();
    // Deferred handoff uses captured generation handles; do not carry the legacy immediate actor borrow forward.
    if (applied != hp)
        return R::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "Preview lethal damage was incomplete"));
    return R::success(spawn::Change::Changed);
}
Result<std::vector<spawn::SpawnerHandle>> RPGSpawnPreview::resolveEvent(std::string_view event) const {
    using R = Result<std::vector<spawn::SpawnerHandle>>;
    if (!impl_->session || impl_->busy) return unavailable<std::vector<spawn::SpawnerHandle>>();
    Guard guard(impl_->busy);
    if (event.empty())
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Preview event name required"));
    auto snapshot = impl_->session->spawning().world().captureDiagnostics();
    if (!snapshot) return R::failure(snapshot.status());
    std::vector<spawn::SpawnerHandle> sources;
    for (const auto& source : snapshot.value().sources)
        if (const auto* activation = std::get_if<spawn::EventActivation>(&source.activation);
            activation && activation->event == event)
            sources.push_back(source.source);
    if (sources.empty())
        return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "No preview source binds this event"));
    return R::success(std::move(sources));
}
Result<spawn::WorldDiagnostics> RPGSpawnPreview::captureDiagnostics() const {
    if (!impl_->session || impl_->busy) return unavailable<spawn::WorldDiagnostics>();
    Guard guard(impl_->busy);
    return impl_->session->spawning().world().captureDiagnostics();
}
Result<map_spawn::PlacementTrace> RPGSpawnPreview::capturePlacementTrace() const {
    if (!impl_->session || impl_->busy) return unavailable<map_spawn::PlacementTrace>();
    Guard guard(impl_->busy);
    return Result<map_spawn::PlacementTrace>::success(impl_->session->spawning().space().captureTrace());
}
Result<std::size_t> RPGSpawnPreview::drawPlacementTrace(graphics::ISolidRectRenderer& renderer) const {
    using R = Result<std::size_t>;
    if (!impl_->session || impl_->busy) return unavailable<std::size_t>();
    Guard                     guard(impl_->busy);
    const auto                trace = impl_->session->spawning().space().captureTrace();
    std::vector<spawn::Point> pixels;
    pixels.reserve(trace.attempts.size());
    for (const auto& attempt : trace.attempts) {
        if (!attempt.point || attempt.decision == map_spawn::PlacementDecision::Accepted) continue;
        auto pixel = impl_->session->spawning().toPixel(*attempt.point);
        if (!pixel) return R::failure(pixel.status());
        const auto limit = double(std::numeric_limits<float>::max()) - 4.0;
        if (std::abs(pixel.value().x) >= limit || std::abs(pixel.value().y) >= limit)
            return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Placement trace pixel overflow"));
        pixels.push_back(pixel.value());
    }
    for (const auto point : pixels)
        renderer.drawSolidRectRGBA(float(point.x - 2), float(point.y - 2), 4, 4, 1, 0.2f, 0.2f, 1);
    return R::success(pixels.size());
}
Result<std::size_t> RPGSpawnPreview::draw(graphics::ISolidRectRenderer&    renderer,
                                          const map_rpg_spawn::ActorStyle& style) const {
    if (!impl_->session || impl_->busy) return unavailable<std::size_t>();
    Guard guard(impl_->busy);
    return impl_->session->draw(renderer, style);
}
Result<void> RPGSpawnPreview::stop() {
    if (impl_->busy) return unavailable<void>();
    if (!impl_->session) return Result<void>::success();
    Guard                             guard(impl_->busy);
    ecs::ScopedTable                  scope(*impl_->world);
    rpg::detail::SimulationEventScope eventScope(impl_->events);
    auto                              stopped = impl_->session->close(spawn::DetachPolicy::RemoveUnits);
    if (!stopped) return stopped;
    impl_->session.reset();
    return Result<void>::success();
}
bool     RPGSpawnPreview::active() const noexcept { return impl_->session != nullptr; }
Revision RPGSpawnPreview::sourceRevision() const noexcept { return impl_->revision; }
}  // namespace eve::map_rpg_spawn_editor
