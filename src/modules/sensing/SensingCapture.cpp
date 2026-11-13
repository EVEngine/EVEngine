#include "sensing/SensingCapture.h"
#include <cmath>
#include <limits>
#include "sensing/Sensing.h"
#include "sensing/SensingStateCodecInternal.h"
#include "spatial/SpatialHash2D.h"
#include "spatial/SpatialHash3D.h"
namespace eve::sensing {
struct SensingCapture::Impl {
    std::vector<Subject> subjects;
    SpatialIndexKind     kind = SpatialIndexKind::None;
    float                cell = 1.f;
};
SensingCapture::SensingCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SensingCapture::~SensingCapture()                                             = default;
SensingCapture::SensingCapture(SensingCapture&&) noexcept                     = default;
SensingCapture&          SensingCapture::operator=(SensingCapture&&) noexcept = default;
std::span<const Subject> SensingCapture::subjects() const noexcept {
    return impl_ ? std::span<const Subject>(impl_->subjects) : std::span<const Subject>{};
}
std::optional<SpatialIndexKind> SensingCapture::indexKind() const noexcept {
    if (!impl_) return std::nullopt;
    return impl_->kind;
}
float                  SensingCapture::cellSize() const noexcept { return impl_ ? impl_->cell : 0.f; }
Result<SensingCapture> SensingWorld::capture() const {
    if (indexKind_ > SpatialIndexKind::XYZ ||
        (indexKind_ == SpatialIndexKind::None && (spatialIndex_ || spatialIndex3D_)) ||
        (indexKind_ == SpatialIndexKind::XYZ && (!spatialIndex3D_ || spatialIndex_)) ||
        (indexKind_ != SpatialIndexKind::None && indexKind_ != SpatialIndexKind::XYZ &&
         (!spatialIndex_ || spatialIndex3D_)))
        return Result<SensingCapture>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Sensing index policy has no matching native storage", "spatial"));
    auto result  = std::make_unique<SensingCapture::Impl>();
    result->kind = indexKind_;
    result->cell = spatialIndex3D_ ? spatialIndex3D_->getCellSize()
                   : spatialIndex_ ? spatialIndex_->getCellSize()
                                   : 1.f;
    result->subjects.reserve(subjects_.size());
    for (const auto& [id, subject] : subjects_) result->subjects.push_back(subject);
    return Result<SensingCapture>::success(SensingCapture(std::move(result)));
}
PreparedSensingRestore::PreparedSensingRestore(std::unique_ptr<SensingWorld> world) : world_(std::move(world)) {}
PreparedSensingRestore::~PreparedSensingRestore()                                                   = default;
PreparedSensingRestore::PreparedSensingRestore(PreparedSensingRestore&&) noexcept                   = default;
PreparedSensingRestore&        PreparedSensingRestore::operator=(PreparedSensingRestore&&) noexcept = default;
Result<PreparedSensingRestore> PreparedSensingRestore::prepare(const SensingCapture& captured) {
    using R = Result<PreparedSensingRestore>;
    if (!captured.impl_)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Sensing capture was moved"));
    auto world = std::make_unique<SensingWorld>();
    for (const auto& subject : captured.impl_->subjects)
        if (!world->subjects_.emplace(subject.id, subject).second)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Duplicate captured sensing identity"));
    auto built = world->rebuildIndex(captured.impl_->kind, captured.impl_->cell);
    if (!built) return R::failure(built.status());
    return R::success(PreparedSensingRestore(std::move(world)));
}
Result<void> SensingWorld::restore(PreparedSensingRestore&& prepared) {
    if (!prepared.world_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Sensing restore token consumed"));
    publishPrepared(std::move(*prepared.world_));
    prepared.world_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::sensing

namespace eve::sensing {
namespace {
LogicalId              sensingSchema() { return *LogicalId::fromParts("sensing", "world-runtime"); }
Result<SensingCapture> invalidSensing(const char* message) {
    return Result<SensingCapture>::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "sensing.runtime"));
}
}  // namespace
Result<SnapshotEnvelope> SensingCapture::seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                              const SnapshotHashProvider& hash) const {
    if (!impl_ || epoch.isNil() || revision.isZero())
        return Result<SnapshotEnvelope>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Sensing sealing requires live capture and nonnil epoch/revision"));
    auto payload =
        Value::object({{"world", detail::sensingWorldPayload(impl_->subjects)},
                       {"index", Value::object({{"kind", int(impl_->kind)}, {"cell_size", double(impl_->cell)}})}});
    return makeSnapshotEnvelope("eve.sensing-runtime", sensingSchema(), SchemaVersion(1), epoch, revision, tick,
                                std::move(payload), hash);
}
Result<SensingCapture> SensingCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                             const SnapshotHashProvider& hash) {
    using R       = Result<SensingCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown sensing runtime version"));
    if (snapshot.type != "eve.sensing-runtime" || snapshot.schema != sensingSchema() || epoch.isNil() ||
        snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Sensing participant boundary mismatch"));
    const auto* root = snapshot.payload.getIf<Value::Object>();
    if (!root || root->size() != 2 || !root->contains("world") || !root->contains("index"))
        return invalidSensing("Unknown or missing sensing payload fields");
    const auto* index = root->at("index").getIf<Value::Object>();
    if (!index || index->size() != 2 || !index->contains("kind") || !index->contains("cell_size"))
        return invalidSensing("Unknown or missing sensing index fields");
    const auto& kindValue = index->at("kind");
    const auto& cellValue = index->at("cell_size");
    if (!kindValue.isInt64() || kindValue.asInt() < 0 || kindValue.asInt() > int(SpatialIndexKind::XYZ) ||
        (!cellValue.isDouble() && !cellValue.isInt64()))
        return invalidSensing("Invalid sensing index kind or size type");
    const auto kind = static_cast<SpatialIndexKind>(kindValue.asInt());
    const auto cell = cellValue.isDouble() ? cellValue.asDouble() : static_cast<double>(cellValue.asInt());
    if (!std::isfinite(cell) || cell <= 0 || cell > std::numeric_limits<float>::max() ||
        (kind == SpatialIndexKind::None && cell != 1.0))
        return invalidSensing("Invalid sensing cell size");
    auto text = root->at("world").toJson();
    if (!text) return R::failure(text.status());
    SensingWorld candidate;
    auto         restored = candidate.restoreJson(text.value());
    if (!restored) return R::failure(restored.status());
    auto indexed = candidate.rebuildIndex(kind, static_cast<float>(cell));
    if (!indexed) return R::failure(indexed.status());
    return candidate.capture();
}
}  // namespace eve::sensing
