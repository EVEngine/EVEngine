#include "map/rpg_spawn/RPGMapArchive.h"
#include "map/MapGridState.h"
#include "map/PreparedMapLoad.h"
#include "map/spawn/MapSpawnAssets.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
namespace eve::map_rpg_spawn {
namespace {
LogicalId schema() { return *LogicalId::fromParts("map.rpg", "save"); }
template <class T>
Result<T> fail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "map.rpg.save"));
}
bool sameBoundary(const SnapshotEnvelope& a, const SnapshotEnvelope& b) {
    return a.instanceId == b.instanceId && a.revision == b.revision && a.tick == b.tick;
}
}  // namespace
struct RPGMapArchive::Impl {
    std::unique_ptr<rpg_spawn::RPGSpawnJointRestore> admitted;
    SnapshotEnvelope                                 runtime;
    std::shared_ptr<const map_spawn::MapSpawnAssets> assets;
    std::shared_ptr<const map::MapGridState>         grid;
};
RPGMapArchive::RPGMapArchive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGMapArchive::~RPGMapArchive() = default;
const SnapshotEnvelope&                RPGMapArchive::runtime() const noexcept { return impl_->runtime; }
const rpg_spawn::RPGSpawnJointRestore& RPGMapArchive::runtimeState() const noexcept { return *impl_->admitted; }
std::shared_ptr<const map_spawn::MapSpawnAssets> RPGMapArchive::assets() const noexcept { return impl_->assets; }
std::shared_ptr<const map::MapGridState>         RPGMapArchive::gridState() const noexcept { return impl_->grid; }
Result<SnapshotEnvelope> RPGMapArchive::sealParticipants(SnapshotEnvelope runtime, SnapshotEnvelope assets,
                                                         SnapshotEnvelope grid, const SnapshotHashProvider& hash) {
    using R = Result<SnapshotEnvelope>;
    if (!sameBoundary(runtime, assets) || !sameBoundary(runtime, grid))
        return fail<SnapshotEnvelope>(DiagnosticCode::Conflict, "Map save participant boundary mismatch");
    auto r = snapshotEnvelopeValue(runtime);
    if (!r) return R::failure(r.status());
    auto a = snapshotEnvelopeValue(assets);
    if (!a) return R::failure(a.status());
    auto g = snapshotEnvelopeValue(grid);
    if (!g) return R::failure(g.status());
    return makeSnapshotEnvelope("eve.rpg-map-save", schema(), SchemaVersion(1), runtime.instanceId, runtime.revision,
                                runtime.tick,
                                Value::object({{"runtime", std::move(r).takeValue()},
                                               {"assets", std::move(a).takeValue()},
                                               {"grid", std::move(g).takeValue()}}),
                                hash);
}
Result<std::shared_ptr<const RPGMapArchive>> RPGMapArchive::prepare(
    const SnapshotEnvelope& input, PersistentId epoch, const SnapshotHashProvider& hash,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R       = Result<std::shared_ptr<const RPGMapArchive>>;
    auto envelope = input;
    auto verified = verifySnapshotEnvelope(envelope, hash);
    if (!verified) return R::failure(verified.status());
    if (envelope.type != "eve.rpg-map-save" || envelope.schema != schema())
        return fail<std::shared_ptr<const RPGMapArchive>>(DiagnosticCode::Unsupported, "Expected RPG map save");
    if (envelope.schemaVersion != SchemaVersion(1))
        return fail<std::shared_ptr<const RPGMapArchive>>(DiagnosticCode::UnknownVersion,
                                                          "Unknown RPG map save version");
    if (epoch.isNil() || envelope.instanceId != epoch || envelope.revision.isZero())
        return fail<std::shared_ptr<const RPGMapArchive>>(DiagnosticCode::Conflict, "Map save boundary mismatch");
    const auto* obj = envelope.payload.getIf<Value::Object>();
    if (!obj || obj->size() != 3 || !obj->contains("runtime") || !obj->contains("assets") || !obj->contains("grid"))
        return fail<std::shared_ptr<const RPGMapArchive>>(DiagnosticCode::ParseError,
                                                          "Unknown or missing map save fields");
    auto runtime = parseSnapshotEnvelopeValue(obj->at("runtime"), hash);
    if (!runtime) return R::failure(runtime.status());
    auto assets = parseSnapshotEnvelopeValue(obj->at("assets"), hash);
    if (!assets) return R::failure(assets.status());
    auto grid = parseSnapshotEnvelopeValue(obj->at("grid"), hash);
    if (!grid) return R::failure(grid.status());
    if (!sameBoundary(envelope, runtime.value()) || !sameBoundary(envelope, assets.value()) ||
        !sameBoundary(envelope, grid.value()))
        return fail<std::shared_ptr<const RPGMapArchive>>(DiagnosticCode::Conflict,
                                                          "Nested map save boundaries disagree");
    auto admitted = rpg_spawn::RPGSpawnJointRestore::prepareBundle(runtime.value(), epoch, hash, std::move(conditions));
    if (!admitted) return R::failure(admitted.status());
    auto sources = map_spawn::MapSpawnAssets::parse(assets.value(), epoch, hash);
    if (!sources) return R::failure(sources.status());
    auto cells = map::MapGridState::parse(grid.value(), epoch, hash);
    if (!cells) return R::failure(cells.status());
    auto impl      = std::make_unique<Impl>();
    impl->runtime  = std::move(runtime).takeValue();
    impl->admitted = std::make_unique<rpg_spawn::RPGSpawnJointRestore>(std::move(admitted).takeValue());
    impl->assets   = std::move(sources).takeValue();
    impl->grid     = std::move(cells).takeValue();
    return R::success(std::shared_ptr<const RPGMapArchive>(new const RPGMapArchive(std::move(impl))));
}
Result<std::unique_ptr<map::PreparedMapLoad>> RPGMapArchive::prepareMap(map::Map& target) const {
    using R        = Result<std::unique_ptr<map::PreparedMapLoad>>;
    auto candidate = impl_->assets->prepareMap(target);
    if (!candidate) return R::failure(candidate.status());
    auto applied = candidate.value()->applyGridState(*impl_->grid);
    if (!applied) return R::failure(applied.status());
    return candidate;
}
}  // namespace eve::map_rpg_spawn
