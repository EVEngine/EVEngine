#include "map/spawn/MapSpawnLoad.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include "common/Assert.h"
#include "common/ServiceInterfaces.h"
#include "common/Uri.h"
#include "map/Map.h"
#include "map/MapGridState.h"
#include "map/Pathfinder.h"
#include "map/spawn/MapSpawnAssets.h"
#include "map/spawn/MapSpawnBinding.h"
#include "map/spawn/MapSpawnNavigation.inl"
#include "map/spawn/MapSpawnSpace.h"
#include "spawn/SpawnRestoreState.h"

namespace eve::map_spawn {
namespace {
struct AdmissionError {
    Diagnostic diagnostic;
};
[[noreturn]] void reject(std::string message, std::string path, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    throw AdmissionError{Diagnostic::error(code, std::move(message), std::move(path))};
}
void fields(const Value& value, std::initializer_list<std::string_view> allowed, std::string_view path) {
    if (!value.isObject()) reject("Expected object", std::string(path));
    for (const auto& key : value.keys())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            reject("Unknown binding field", std::string(path) + "." + key);
}
const Value& required(const Value& value, const std::string& key) {
    const auto* item = value.find(key);
    if (!item) reject("Required binding field missing", key);
    return *item;
}
std::string text(const Value& value, const std::string& key) {
    const auto& item = required(value, key);
    if (!item.isString() || item.asString().empty()) reject("Expected nonempty text", key);
    return item.asString();
}
double number(const Value& value) {
    if (!value.isNumeric()) reject("Expected finite coordinate", "pixel_to_world");
    const auto result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(result)) reject("Expected finite coordinate", "pixel_to_world");
    return result;
}
struct BindingInputs {
    std::string    mapId, mapUri, spawnsUri;
    PixelTransform transform;
    std::size_t    layer = 0;
};
BindingInputs decodeBinding(const Value& binding) {
    fields(binding, {"schema", "version", "map_id", "map_uri", "spawns_uri", "pixel_to_world", "navigation_layer"},
           "$");
    if (text(binding, "schema") != "eve.map-spawn-binding") reject("Unexpected schema", "schema");
    const auto& version = required(binding, "version");
    if (!version.isInt64() || version.asInt() != 1)
        reject("Unsupported binding version", "version", DiagnosticCode::UnknownVersion);
    const auto& transform = required(binding, "pixel_to_world");
    fields(transform, {"origin", "scale"}, "pixel_to_world");
    const auto& origin = required(transform, "origin");
    if (!origin.isArray() || origin.arraySize() != 2)
        reject("Origin requires two coordinates", "pixel_to_world.origin");
    BindingInputs result{text(binding, "map_id"),
                         text(binding, "map_uri"),
                         text(binding, "spawns_uri"),
                         {{number(origin.at(0)), number(origin.at(1))}, number(required(transform, "scale"))},
                         0};
    if (result.transform.scale <= 0) reject("Pixel scale must be positive", "pixel_to_world.scale");
    for (const auto& uriText : {result.mapUri, result.spawnsUri}) {
        auto uri = ResourceUri::parse(uriText);
        if (!uri || uri.value().scheme() != UriScheme::Project || !uri.value().query().empty() ||
            !uri.value().fragment().empty())
            reject("Only explicit project asset URIs are supported", uriText, DiagnosticCode::Unsupported);
    }
    if (const auto* index = binding.find("navigation_layer")) {
        if (!index->isInt64() || index->asInt() < 0 ||
            static_cast<std::uint64_t>(index->asInt()) > std::numeric_limits<std::size_t>::max())
            reject("Navigation layer index out of range", "navigation_layer");
        result.layer = static_cast<std::size_t>(index->asInt());
    }
    return result;
}
struct CandidateNavigation {
    map::MapGridGeometry        geometry;
    SpawnGridTransform          transform;
    map::NavigationGridSnapshot navigation;
};
Result<CandidateNavigation> captureCandidateNavigation(map::PreparedMapLoad& candidate, const BindingInputs& inputs) {
    using R       = Result<CandidateNavigation>;
    auto geometry = candidate.grid(inputs.layer);
    if (!geometry) return R::failure(geometry.status());
    const auto& grid = geometry.value();
    if (grid.orientation != map::MapOrientation::Orthogonal || grid.gapX != 0 || grid.gapY != 0)
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                            "First map spawn grid requires orthogonal contiguous cells",
                                            "navigation_layer"));
    auto navigation = candidate.captureNavigation(inputs.layer);
    if (!navigation) return R::failure(navigation.status());
    const auto&        conversion = inputs.transform;
    SpawnGridTransform transform{{std::fma(grid.originX, conversion.scale, conversion.origin.x),
                                  std::fma(grid.originY, conversion.scale, conversion.origin.y)},
                                 {grid.stepX * conversion.scale, grid.stepY * conversion.scale}};
    if (!std::isfinite(transform.origin.x) || !std::isfinite(transform.origin.y) ||
        !std::isfinite(transform.cellSize.x) || !std::isfinite(transform.cellSize.y) || transform.cellSize.x <= 0 ||
        transform.cellSize.y <= 0)
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Unrepresentable map grid transform"));
    return R::success({grid, transform, std::move(navigation).takeValue()});
}
Result<void> installCandidateSpace(MapSpawnSpace& space, map::PreparedMapLoad& candidate,
                                   map::NavigationGridSnapshot                     navigation,
                                   const std::optional<std::vector<spawn::Point>>& players) {
    auto bound = space.bindMapLifetime(candidate.ownerLease());
    if (!bound) return bound;
    auto installed = space.setNavigation(std::move(navigation));
    if (!installed) return installed;
    return players ? space.setPlayers(*players) : Result<void>::success();
}
class CapturedFiles final : public service::IFileSystem {
    service::IFileSystem& source_;

public:
    std::map<std::string, std::vector<std::uint8_t>> records;
    explicit CapturedFiles(service::IFileSystem& source) : source_(source) {}
    bool readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        if (const auto found = records.find(path); found != records.end()) {
            out = found->second;
            return true;
        }
        std::vector<std::uint8_t> bytes;
        if (!source_.readFile(path, bytes)) return false;
        records.emplace(path, bytes);
        out = std::move(bytes);
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& path) override {
        if (records.contains(path)) return true;
        if (!source_.fileExists(path)) return false;
        std::vector<std::uint8_t> bytes;
        if (!readFile(path, bytes)) reject("Existing asset bytes unavailable", path, DiagnosticCode::NotFound);
        return true;
    }
};
class ReplayFiles final : public service::IFileSystem {
    std::span<const MapSpawnAssetFile> records_;

public:
    explicit ReplayFiles(std::span<const MapSpawnAssetFile> records) : records_(records) {}
    bool readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        auto found = std::lower_bound(records_.begin(), records_.end(), path,
                                      [](const auto& row, const auto& key) { return row.path < key; });
        if (found == records_.end() || found->path != path) return false;
        out = found->bytes;
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& path) override {
        auto found = std::lower_bound(records_.begin(), records_.end(), path,
                                      [](const auto& row, const auto& key) { return row.path < key; });
        return found != records_.end() && found->path == path;
    }
};
std::string read(service::IFileSystem& files, const std::string& locator) {
    auto uri = ResourceUri::parse(locator);
    if (!uri) reject("Invalid project asset URI", locator);
    if (uri.value().scheme() != UriScheme::Project || !uri.value().query().empty() || !uri.value().fragment().empty())
        reject("Only explicit project asset URIs are supported", locator, DiagnosticCode::Unsupported);
    std::vector<std::uint8_t> bytes;
    if (!files.readFile(std::string(uri.value().path()), bytes))
        reject("Map spawn asset could not be read", locator, DiagnosticCode::NotFound);
    if (bytes.empty()) reject("Map spawn asset is empty", locator, DiagnosticCode::ParseError);
    return std::string(bytes.begin(), bytes.end());
}
}  // namespace

struct MapSpawnBinding::Impl {
    BindingInputs inputs;
};
MapSpawnBinding::MapSpawnBinding(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MapSpawnBinding::~MapSpawnBinding()                                            = default;
MapSpawnBinding::MapSpawnBinding(MapSpawnBinding&&) noexcept                   = default;
MapSpawnBinding&        MapSpawnBinding::operator=(MapSpawnBinding&&) noexcept = default;
Result<MapSpawnBinding> MapSpawnBinding::decode(const Value& source) {
    try {
        return Result<MapSpawnBinding>::success(MapSpawnBinding(std::make_unique<Impl>(Impl{decodeBinding(source)})));
    } catch (const AdmissionError& error) {
        return Result<MapSpawnBinding>::failure(error.diagnostic);
    }
}
const std::string& MapSpawnBinding::mapId() const noexcept { return impl_->inputs.mapId; }
const std::string& MapSpawnBinding::mapUri() const noexcept { return impl_->inputs.mapUri; }
const std::string& MapSpawnBinding::spawnsUri() const noexcept { return impl_->inputs.spawnsUri; }
PixelTransform     MapSpawnBinding::transform() const noexcept { return impl_->inputs.transform; }
std::size_t        MapSpawnBinding::navigationLayer() const noexcept { return impl_->inputs.layer; }
MapSpawnSession::MapSpawnSession(std::unique_ptr<spawn::SpawnWorld>     world,
                                 std::shared_ptr<spawn::SpawnProviders> providers,
                                 std::vector<spawn::SpawnerHandle> sources, PixelTransform transform,
                                 std::shared_ptr<MapSpawnSpace> space, map::MapLease owner, std::size_t index,
                                 map::MapGridGeometry geometry, std::string asset)
    : world_(std::move(world)),
      providers_(std::move(providers)),
      sources_(std::move(sources)),
      transform_(transform),
      space_(std::move(space)),
      navigation_(std::make_shared<NavigationBinding>(std::move(owner), index, geometry, std::move(asset))) {}
MapSpawnSession::~MapSpawnSession() = default;
const spawn::SpawnWorld& MapSpawnSession::world() const noexcept { return *world_; }
const MapSpawnSpace&     MapSpawnSession::space() const noexcept { return *space_; }
Result<void>             MapSpawnSession::resetPlacementTrace(PlacementTraceMode mode) {
    if (navigation_->advancing || navigation_->phase != NavigationBinding::Phase::Ready ||
        !navigation_->owner.isCurrent())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Map session cannot reset placement trace"));
    space_->resetTrace(mode);
    return Result<void>::success();
}
std::span<const spawn::SpawnerHandle> MapSpawnSession::sourceHandles() const noexcept { return sources_; }
PixelTransform                        MapSpawnSession::pixelTransform() const noexcept { return transform_; }
std::shared_ptr<const MapSpawnAssets> MapSpawnSession::assets() const noexcept { return navigation_->assets; }
Result<spawn::Point>                  pixelToWorld(PixelTransform transform, spawn::Point pixel) {
    if (!std::isfinite(transform.origin.x) || !std::isfinite(transform.origin.y) || !std::isfinite(transform.scale) ||
        transform.scale <= 0)
        return Result<spawn::Point>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid pixel transform"));
    spawn::Point result{std::fma(pixel.x, transform.scale, transform.origin.x),
                        std::fma(pixel.y, transform.scale, transform.origin.y)};
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(result.x) || !std::isfinite(result.y))
        return Result<spawn::Point>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid pixel coordinate"));
    return Result<spawn::Point>::success(result);
}
Result<spawn::Point> worldToPixel(PixelTransform transform, spawn::Point world) {
    if (!std::isfinite(transform.origin.x) || !std::isfinite(transform.origin.y) || !std::isfinite(transform.scale) ||
        transform.scale <= 0)
        return Result<spawn::Point>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid pixel transform"));
    spawn::Point result{(world.x - transform.origin.x) / transform.scale,
                        (world.y - transform.origin.y) / transform.scale};
    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(result.x) || !std::isfinite(result.y))
        return Result<spawn::Point>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid world coordinate"));
    return Result<spawn::Point>::success(result);
}
Result<spawn::Point> MapSpawnSession::toWorld(spawn::Point pixel) const { return pixelToWorld(transform_, pixel); }
Result<spawn::Point> MapSpawnSession::toPixel(spawn::Point world) const { return worldToPixel(transform_, world); }
Result<void>         MapSpawnSession::close(spawn::DetachPolicy policy) {
    if (navigation_->advancing)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Cannot close during scene advance"));
    navigation_->phase = NavigationBinding::Phase::Closing;
    while (!sources_.empty()) {
        auto lease = world_->sourceLease(sources_.back());
        if (!lease) {
            const auto* diagnostic = lease.status().primaryDiagnostic();
            if (!diagnostic || diagnostic->code() != DiagnosticCode::StaleHandle)
                return Result<void>::failure(lease.status());
            sources_.pop_back();
            continue;
        }
        auto detached = world_->detach(sources_.back(), policy);
        if (!detached) return Result<void>::failure(detached.status());
        sources_.pop_back();
    }
    navigation_->phase = NavigationBinding::Phase::Closed;
    return Result<void>::success();
}
PreparedMapSpawn::PreparedMapSpawn(std::unique_ptr<map::PreparedMapLoad> candidate,
                                   std::unique_ptr<MapSpawnSession>      session)
    : mapCandidate_(std::move(candidate)), session_(std::move(session)) {}
PreparedMapSpawn::~PreparedMapSpawn()                                             = default;
PreparedMapSpawn::PreparedMapSpawn(PreparedMapSpawn&&) noexcept                   = default;
PreparedMapSpawn&        PreparedMapSpawn::operator=(PreparedMapSpawn&&) noexcept = default;
Result<PreparedMapSpawn> PreparedMapSpawn::prepare(map::Map& target, const Value& inputBinding,
                                                   service::IFileSystem&                 inputFiles,
                                                   const spawn::ITemplateResolver&       templates,
                                                   const spawn::IUnitDefinitionResolver& units, PersistentId owner,
                                                   std::uint64_t seed, std::string_view inputContract,
                                                   SpawnDomainServices domain) {
    auto          binding = inputBinding;
    std::string   objectContract(inputContract);
    CapturedFiles files(inputFiles);
    const auto    targetOwner = target.observeLifetime();
    try {
        const auto inputs     = decodeBinding(binding);
        const auto conversion = inputs.transform;
        auto       mapJson    = read(files, text(binding, "map_uri"));
        auto       mapUri     = ResourceUri::parse(text(binding, "map_uri")).expect("URI admitted by read");
        auto       mapTree    = Value::fromJson(mapJson);
        if (!mapTree) return Result<PreparedMapSpawn>::failure(mapTree.status());
        if (!mapTree.value().isObject()) reject("Expected map object", "map_uri", DiagnosticCode::ParseError);
        auto sidecar = Value::fromJson(read(files, text(binding, "spawns_uri")));
        if (!sidecar) return Result<PreparedMapSpawn>::failure(sidecar.status());
        auto compiled = spawn::compileMap(sidecar.value(), templates, units);
        if (!compiled) return Result<PreparedMapSpawn>::failure(compiled.status());
        if (compiled.value().id() != text(binding, "map_id"))
            reject("Sidecar map identity mismatch", "map_id", DiagnosticCode::Conflict);
        if (!domain.factory) reject("Missing domain factory", "domain.factory", DiagnosticCode::NotFound);
        if (!targetOwner.isCurrent())
            reject("Map owner destroyed during source reads", "map_uri", DiagnosticCode::StaleHandle);
        auto candidate =
            target.prepareAssetTextWithObjectContract(mapJson, std::string(mapUri.path()), files, objectContract);
        if (!candidate) return Result<PreparedMapSpawn>::failure(candidate.status());
        const auto navigationLayer = inputs.layer;
        auto       capturedNav     = captureCandidateNavigation(*candidate.value(), inputs);
        if (!capturedNav) return Result<PreparedMapSpawn>::failure(capturedNav.status());
        const auto grid      = capturedNav.value().geometry;
        auto       space     = std::make_shared<MapSpawnSpace>(compiled.value(), owner, capturedNav.value().transform,
                                                               std::move(domain.occupancy));
        auto       installed = installCandidateSpace(*space, *candidate.value(),
                                                     std::move(capturedNav).takeValue().navigation, domain.players);
        if (!installed) return Result<PreparedMapSpawn>::failure(installed.status());
        auto factory   = std::make_shared<SpatialSpawnFactory>(std::move(domain.factory), space);
        auto providers = std::make_shared<spawn::SpawnProviders>(spawn::SpawnProviders{space, factory});
        auto world     = std::make_unique<spawn::SpawnWorld>(owner, seed);
        auto attached  = world->attach(compiled.value(), providers);
        if (!attached) return Result<PreparedMapSpawn>::failure(attached.status());
        auto                           session = std::unique_ptr<MapSpawnSession>(new MapSpawnSession(
            std::move(world), std::move(providers), std::move(attached).takeValue(), conversion, std::move(space),
            candidate.value()->ownerLease(), navigationLayer, grid, std::string(mapUri.path())));
        std::vector<MapSpawnAssetFile> captured;
        captured.reserve(files.records.size());
        for (auto& [path, bytes] : files.records) captured.push_back({path, std::move(bytes)});
        session->navigation_->assets = std::shared_ptr<const MapSpawnAssets>(
            new const MapSpawnAssets(std::move(binding), std::move(objectContract), std::move(captured)));
        return Result<PreparedMapSpawn>::success(
            PreparedMapSpawn(std::move(candidate).takeValue(), std::move(session)));
    } catch (const AdmissionError& error) {
        return Result<PreparedMapSpawn>::failure(error.diagnostic);
    } catch (const std::exception& error) {
        return Result<PreparedMapSpawn>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, error.what(), "map_spawn.prepare"));
    } catch (...) {
        return Result<PreparedMapSpawn>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, "Asset provider threw", "map_spawn.prepare"));
    }
}
Result<PreparedMapSpawn> PreparedMapSpawn::prepareRestored(map::Map&                             target,
                                                           std::shared_ptr<const MapSpawnAssets> assets,
                                                           const map::MapGridState&              gridState,
                                                           const spawn::SpawnRestoreState&       state,
                                                           SpawnDomainServices                   domain) {
    using R = Result<PreparedMapSpawn>;
    if (!assets || !domain.factory)
        return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "Missing restored map/domain owner"));
    try {
        const auto inputs    = decodeBinding(assets->binding());
        auto       candidate = assets->prepareMap(target);
        if (!candidate) return R::failure(candidate.status());
        auto applied = candidate.value()->applyGridState(gridState);
        if (!applied) return R::failure(applied.status());
        auto capturedNav = captureCandidateNavigation(*candidate.value(), inputs);
        if (!capturedNav) return R::failure(capturedNav.status());
        const auto geometry = capturedNav.value().geometry;
        auto space = MapSpawnSpace::prepareRestored(state, capturedNav.value().transform, std::move(domain.occupancy));
        if (!space) return R::failure(space.status());
        auto installed = installCandidateSpace(*space.value(), *candidate.value(),
                                               std::move(capturedNav).takeValue().navigation, domain.players);
        if (!installed) return R::failure(installed.status());
        auto factory   = std::make_shared<SpatialSpawnFactory>(std::move(domain.factory), space.value());
        auto providers = std::make_shared<spawn::SpawnProviders>(spawn::SpawnProviders{space.value(), factory});
        auto attempt   = state.clone();
        if (!attempt) return R::failure(attempt.status());
        auto restored = spawn::SpawnWorld::restore(attempt.value(), providers);
        if (!restored) return R::failure(restored.status());
        auto                              world = std::make_unique<spawn::SpawnWorld>(std::move(restored).takeValue());
        std::vector<spawn::SpawnerHandle> sources;
        sources.reserve(state.sources().size());
        for (const auto& source : state.sources()) {
            auto lease = world->resolveSavedSource(source);
            if (!lease) return R::failure(lease.status());
            sources.push_back(lease.value().source());
        }
        auto uri                     = ResourceUri::parse(inputs.mapUri).expect("admitted saved map URI");
        auto session                 = std::unique_ptr<MapSpawnSession>(new MapSpawnSession(
            std::move(world), std::move(providers), std::move(sources), inputs.transform, std::move(space).takeValue(),
            candidate.value()->ownerLease(), inputs.layer, geometry, std::string(uri.path())));
        session->navigation_->assets = std::move(assets);
        return R::success(PreparedMapSpawn(std::move(candidate).takeValue(), std::move(session)));
    } catch (const AdmissionError& error) {
        return R::failure(error.diagnostic);
    }
}
bool MapSpawnSession::replacementReady(const map::MapLease& owner) const noexcept {
    return world_ && !navigation_->advancing && navigation_->owner.isCurrent() &&
           navigation_->owner.hasSameOwner(owner);
}
void MapSpawnSession::retireForReplacement() noexcept {
    if (!replacementReady(navigation_->owner)) {
        EV_ASSERT(false, "Replacement retirement requires idle live Map owner");
        std::terminate();
    }
    auto retired = world_->retireSources();
    // The composition owner holds its busy guard and exposes no mutable core. No callback occurs after publication.
    if (!retired) std::terminate();
    navigation_->phase = NavigationBinding::Phase::Closed;
    sources_.clear();
}
OptionalRef<const spawn::SpawnWorld> PreparedMapSpawn::world() const noexcept {
    return session_ ? OptionalRef<const spawn::SpawnWorld>{std::cref(session_->world())} : std::nullopt;
}
Result<std::unique_ptr<MapSpawnSession>> PreparedMapSpawn::commit(map::Map& target) {
    if (!session_)
        return Result<std::unique_ptr<MapSpawnSession>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Map spawn candidate already consumed"));
    auto loaded = mapCandidate_->commit(target);
    if (!loaded) return Result<std::unique_ptr<MapSpawnSession>>::failure(loaded.status());
    session_->bindPublishedNavigation(target);
    mapCandidate_.reset();
    return Result<std::unique_ptr<MapSpawnSession>>::success(std::move(session_));
}
}  // namespace eve::map_spawn

namespace eve::map_spawn {
namespace {
LogicalId   assetsSchema() { return *LogicalId::fromParts("map.spawn", "assets"); }
std::string encodeAssetBytes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<std::size_t>::max() / 2) reject("Asset byte encoding overflows", "files");
    constexpr char digits[] = "0123456789abcdef";
    std::string    result(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        result[2 * i]     = digits[bytes[i] >> 4];
        result[2 * i + 1] = digits[bytes[i] & 15];
    }
    return result;
}
std::vector<std::uint8_t> decodeAssetBytes(const std::string& text) {
    if (text.size() % 2) reject("Asset hex requires byte pairs", "files.bytes_hex", DiagnosticCode::ParseError);
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        reject("Expected canonical lower-case hex", "files.bytes_hex", DiagnosticCode::ParseError);
    };
    std::vector<std::uint8_t> bytes(text.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>((digit(text[2 * i]) << 4) | digit(text[2 * i + 1]));
    return bytes;
}
}  // namespace
MapSpawnAssets::MapSpawnAssets(Value binding, std::string contract, std::vector<MapSpawnAssetFile> files)
    : binding_(std::move(binding)), contract_(std::move(contract)), files_(std::move(files)) {}
MapSpawnAssets::~MapSpawnAssets() = default;
const Value&                       MapSpawnAssets::binding() const noexcept { return binding_; }
std::string_view                   MapSpawnAssets::objectContract() const noexcept { return contract_; }
std::span<const MapSpawnAssetFile> MapSpawnAssets::files() const noexcept { return files_; }
Result<SnapshotEnvelope>           MapSpawnAssets::seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                                        const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (epoch.isNil() || revision.isZero())
        return R::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Asset participant requires save epoch/revision"));
    if (files_.size() > 1000000)
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Too many asset files"));
    try {
        Value::Array files;
        files.reserve(files_.size());
        for (const auto& file : files_)
            files.push_back(Value::object({{"path", file.path}, {"bytes_hex", encodeAssetBytes(file.bytes)}}));
        auto payload =
            Value::object({{"binding", binding_}, {"object_contract", contract_}, {"files", Value(std::move(files))}});
        return makeSnapshotEnvelope("eve.map-spawn-assets", assetsSchema(), SchemaVersion(1), epoch, revision, tick,
                                              std::move(payload), hash);
    } catch (const AdmissionError& error) {
        return R::failure(error.diagnostic);
    }
}
Result<std::shared_ptr<const MapSpawnAssets>> MapSpawnAssets::parse(const SnapshotEnvelope& input, PersistentId epoch,
                                                                    const SnapshotHashProvider& hash) {
    using R       = Result<std::shared_ptr<const MapSpawnAssets>>;
    auto snapshot = input;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.type != "eve.map-spawn-assets" || snapshot.schema != assetsSchema())
        return R::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "Expected Map Spawn structural asset participant"));
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown Map Spawn asset version"));
    if (epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Asset participant boundary mismatch"));
    try {
        fields(snapshot.payload, {"binding", "object_contract", "files"}, "assets");
        auto        binding  = required(snapshot.payload, "binding");
        auto        decoded  = decodeBinding(binding);
        auto        contract = text(snapshot.payload, "object_contract");
        const auto* rows     = required(snapshot.payload, "files").getIf<Value::Array>();
        if (!rows || rows->size() > 1000000) reject("Expected bounded asset file array", "files");
        std::map<std::string, std::vector<std::uint8_t>> records;
        for (const auto& row : *rows) {
            fields(row, {"path", "bytes_hex"}, "files");
            auto        path = text(row, "path");
            const auto& hex  = required(row, "bytes_hex");
            if (!hex.isString()) reject("Expected hex source text", "files.bytes_hex");
            auto bytes = decodeAssetBytes(hex.asString());
            if (!records.emplace(std::move(path), std::move(bytes)).second)
                reject("Duplicate asset path", "files", DiagnosticCode::Conflict);
        }
        for (const auto& source : {decoded.mapUri, decoded.spawnsUri}) {
            auto uri   = ResourceUri::parse(source).expect("binding URI verified");
            auto found = records.find(std::string(uri.path()));
            if (found == records.end() || found->second.empty())
                reject("Required captured root source is missing", source, DiagnosticCode::NotFound);
        }
        std::vector<MapSpawnAssetFile> files;
        files.reserve(records.size());
        for (auto& [path, bytes] : records) files.push_back({path, std::move(bytes)});
        return R::success(std::shared_ptr<const MapSpawnAssets>(
            new const MapSpawnAssets(std::move(binding), std::move(contract), std::move(files))));
    } catch (const AdmissionError& error) {
        return R::failure(error.diagnostic);
    }
}
Result<std::unique_ptr<map::PreparedMapLoad>> MapSpawnAssets::prepareMap(map::Map& target) const {
    try {
        const auto  inputs = decodeBinding(binding_);
        ReplayFiles files(files_);
        auto        json = read(files, inputs.mapUri);
        auto        uri  = ResourceUri::parse(inputs.mapUri).expect("binding URI verified");
        return target.prepareAssetTextWithObjectContract(json, std::string(uri.path()), files, contract_);
    } catch (const AdmissionError& error) {
        return Result<std::unique_ptr<map::PreparedMapLoad>>::failure(error.diagnostic);
    }
}
}  // namespace eve::map_spawn
