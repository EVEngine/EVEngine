#include "procgen/heightmap/TerrainGenerationSession.h"
#include <bit>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include "common/Value.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainWaterField.h"

namespace eve::procgen {
using namespace raster_detail;
namespace {
constexpr std::size_t kMaximumSessionSnapshotBytes = 128U * 1024U * 1024U;
constexpr std::uint32_t kMaximumSessionCommands = 65536;
struct SessionWriter {
    std::string bytes;
    template <class T> void scalar(T value) {
        if constexpr (std::is_enum_v<T>) scalar(static_cast<std::underlying_type_t<T>>(value));
        else if constexpr (std::is_same_v<T, bool>) scalar(static_cast<std::uint8_t>(value));
        else if constexpr (std::is_same_v<T, float>) scalar(std::bit_cast<std::uint32_t>(value));
        else if constexpr (std::is_same_v<T, double>) scalar(std::bit_cast<std::uint64_t>(value));
        else {
            using U = std::make_unsigned_t<T>;
            U bits = static_cast<U>(value);
            for (std::size_t i = 0; i < sizeof(T); ++i) bytes.push_back(static_cast<char>((bits >> (i * 8)) & 255U));
        }
    }
};
struct SessionReader {
    std::string_view bytes;
    std::size_t offset = 0;
    template <class T> bool scalar(T& value) {
        if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> raw{}; if (!scalar(raw)) return false; value = static_cast<T>(raw); return true;
        } else if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t raw{}; if (!scalar(raw) || raw > 1) return false; value = raw != 0; return true;
        } else if constexpr (std::is_same_v<T, float>) {
            std::uint32_t raw{}; if (!scalar(raw)) return false; value = std::bit_cast<float>(raw); return std::isfinite(value);
        } else if constexpr (std::is_same_v<T, double>) {
            std::uint64_t raw{}; if (!scalar(raw)) return false; value = std::bit_cast<double>(raw); return std::isfinite(value);
        } else {
            if (bytes.size() - offset < sizeof(T)) return false;
            using U = std::make_unsigned_t<T>; U bits = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i)
                bits |= static_cast<U>(static_cast<unsigned char>(bytes[offset++])) << (i * 8);
            value = static_cast<T>(bits); return true;
        }
    }
};
void writeSessionHeightmap(SessionWriter& writer, const Heightmap& map) {
    writer.scalar(map.getWidth()); writer.scalar(map.getHeight());
    for (float value : map.data()) writer.scalar(value);
}
bool readSessionHeightmap(SessionReader& reader, Heightmap& map) {
    int width{}, height{};
    if (!reader.scalar(width) || !reader.scalar(height) || width <= 0 || height <= 0 || width > INT_MAX / height)
        return false;
    Heightmap candidate(width, height);
    for (float& value : candidate.data()) if (!reader.scalar(value)) return false;
    map = std::move(candidate); return true;
}
std::string encodeSessionHex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(bytes.size() * 2);
    for (unsigned char value : bytes) { result.push_back(digits[value >> 4]); result.push_back(digits[value & 15]); }
    return result;
}
Result<std::string> decodeSessionHex(std::string_view text) {
    if ((text.size() & 1U) != 0 || text.size() / 2 > kMaximumSessionSnapshotBytes)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "terrain.session.restore: invalid payload size"));
    auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string result(text.size() / 2, '\0');
    for (std::size_t i = 0; i < result.size(); ++i) {
        int high = nibble(text[i * 2]), low = nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: invalid payload hex"));
        result[i] = static_cast<char>((high << 4) | low);
    }
    return Result<std::string>::success(std::move(result));
}
struct SessionState {
    Heightmap         terrain, sediment;
    TerrainWaterField water;
};
struct SimulationResetRecord {
    float depth;
};
struct ThermalRecord {
    TerrainThermalSettings settings;
};
struct HydraulicRecord {
    TerrainWaterSettings    water;
    TerrainSedimentSettings sediment;
    TerrainThermalSettings  thermal;
    int                     iterations;
};
Result<int> execute(SessionState& state, const SimulationResetRecord& c) {
    auto result = state.water.reset(state.terrain.getWidth(), state.terrain.getHeight(), c.depth);
    if (!result.ok()) return result;
    std::fill(state.sediment.data().begin(), state.sediment.data().end(), 0.0F);
    return result;
}
Result<int> execute(SessionState& state, const ThermalRecord& c) {
    return applyTerrainThermal(state.terrain, state.sediment, c.settings);
}
Result<int> execute(SessionState& state, const HydraulicRecord& c) {
    return state.water.advanceHydraulic(state.terrain, state.sediment, c.water, c.sediment, c.thermal, c.iterations);
}
struct StampRecord {
    Heightmap            stamp, local, global;
    TerrainStampSettings settings;
};
struct ContrastRecord {
    Heightmap mask;
    float     strength, featureSize;
};
struct SmoothRecord {
    Heightmap             mask;
    TerrainSmoothSettings settings;
};
struct RidgeRecord {
    Heightmap            mask;
    TerrainRidgeSettings settings;
};
struct TerraceRecord {
    Heightmap              mask;
    TerrainTerraceSettings settings;
};
struct PowerRecord {
    Heightmap mask;
    float     power;
};
struct CurveRecord {
    Heightmap mask, curve;
    float     minimum, maximum;
};
struct MixRecord {
    Heightmap                local, global;
    TerrainHeightMixSettings settings;
};
Result<int> execute(Heightmap& h, const StampRecord& c) {
    return applyTerrainStamp(h, c.stamp, c.settings, c.local, c.global);
}
Result<int> execute(Heightmap& h, const ContrastRecord& c) {
    return applyTerrainContrast(h, c.mask, c.strength, c.featureSize);
}
Result<int> execute(Heightmap& h, const SmoothRecord& c) { return applyTerrainSmooth(h, c.mask, c.settings); }
Result<int> execute(Heightmap& h, const RidgeRecord& c) { return applyTerrainRidges(h, c.mask, c.settings); }
Result<int> execute(Heightmap& h, const TerraceRecord& c) { return applyTerrainTerrace(h, c.mask, c.settings); }
Result<int> execute(Heightmap& h, const PowerRecord& c) { return applyTerrainPower(h, c.mask, c.power); }
Result<int> execute(Heightmap& h, const CurveRecord& c) {
    return applyTerrainHeightCurve(h, c.mask, c.curve, c.minimum, c.maximum);
}
Result<int> execute(Heightmap& h, const MixRecord& c) {
    return applyTerrainHeightMix(h, c.local, c.global, c.settings);
}
template <class Record>
Result<int> execute(SessionState& state, const Record& record) {
    return execute(state.terrain, record);
}
}  // namespace
struct TerrainGenerationSession::Command {
    std::variant<StampRecord, ContrastRecord, SmoothRecord, RidgeRecord, TerraceRecord, PowerRecord, CurveRecord,
                 MixRecord, SimulationResetRecord, ThermalRecord, HydraulicRecord>
         operation;
    bool enabled = true;
};
namespace {
#define SESSION_STAMP_FIELDS(X) X(originX) X(originZ) X(spacingX) X(spacingZ) X(centerX) X(centerZ) X(width) X(depth) X(rotation) X(amplitude) X(baseHeight) X(blendStrength) X(operation)
#define SESSION_SMOOTH_FIELDS(X) X(radius) X(verticality) X(strength)
#define SESSION_RIDGE_FIELDS(X) X(mixStrength) X(exponent) X(strength) X(minimum) X(maximum) X(passes)
#define SESSION_TERRACE_FIELDS(X) X(count) X(bevel) X(strength)
#define SESSION_MIX_FIELDS(X) X(minimum) X(maximum) X(midpoint) X(strength) X(clipMinimum) X(clipMaximum)
#define SESSION_WATER_FIELDS(X) X(spacingX) X(spacingZ) X(heightScale) X(waterScale) X(dt) X(precipitation) X(evaporation) X(flowAcceleration)
#define SESSION_SEDIMENT_FIELDS(X) X(effect) X(depositRate) X(bankDeposit) X(bedDeposit)
#define SESSION_THERMAL_FIELDS(X) X(spacingX) X(spacingZ) X(heightScale) X(reposeSlope) X(dt) X(iterations)
#define SESSION_WRITE_FIELD(name) writer.scalar(value.name);
#define SESSION_READ_FIELD(name) if (!reader.scalar(value.name)) return false;
template <class T> void writeSessionValue(SessionWriter& writer, const T& value);
template <class T> bool readSessionValue(SessionReader& reader, T& value);
template <> void writeSessionValue(SessionWriter& writer, const TerrainStampSettings& value) { SESSION_STAMP_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainStampSettings& value) { SESSION_STAMP_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainSmoothSettings& value) { SESSION_SMOOTH_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainSmoothSettings& value) { SESSION_SMOOTH_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainRidgeSettings& value) { SESSION_RIDGE_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainRidgeSettings& value) { SESSION_RIDGE_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainTerraceSettings& value) { SESSION_TERRACE_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainTerraceSettings& value) { SESSION_TERRACE_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainHeightMixSettings& value) { SESSION_MIX_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainHeightMixSettings& value) { SESSION_MIX_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainWaterSettings& value) { SESSION_WATER_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainWaterSettings& value) { SESSION_WATER_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainSedimentSettings& value) { SESSION_SEDIMENT_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainSedimentSettings& value) { SESSION_SEDIMENT_FIELDS(SESSION_READ_FIELD) return true; }
template <> void writeSessionValue(SessionWriter& writer, const TerrainThermalSettings& value) { SESSION_THERMAL_FIELDS(SESSION_WRITE_FIELD) }
template <> bool readSessionValue(SessionReader& reader, TerrainThermalSettings& value) { SESSION_THERMAL_FIELDS(SESSION_READ_FIELD) return true; }
#undef SESSION_WRITE_FIELD
#undef SESSION_READ_FIELD
#undef SESSION_STAMP_FIELDS
#undef SESSION_SMOOTH_FIELDS
#undef SESSION_RIDGE_FIELDS
#undef SESSION_TERRACE_FIELDS
#undef SESSION_MIX_FIELDS
#undef SESSION_WATER_FIELDS
#undef SESSION_SEDIMENT_FIELDS
#undef SESSION_THERMAL_FIELDS
}  // namespace
struct TerrainGenerationSession::Impl {
    Heightmap                           baseline;
    std::shared_ptr<const SessionState> current;
    std::vector<Command>                commands;
    int                                 cursor = 0;
    TerrainSessionAccess                access = TerrainSessionAccess::Editable;
    TerrainSessionRunStatus             replayStatus = TerrainSessionRunStatus::Idle;
    std::optional<SessionState>         replayCandidate;
    int                                 replayNext = 0;
    Impl() = default;
    Impl(const Impl& other)
        : baseline(other.baseline), current(other.current), commands(other.commands), cursor(other.cursor),
          access(other.access) {}
    bool editable() const {
        return access == TerrainSessionAccess::Editable && validRaster(baseline) &&
               replayStatus != TerrainSessionRunStatus::Pending;
    }
    Result<int> rebuild() {
        auto next        = std::make_shared<SessionState>();
        next->terrain    = baseline;
        next->sediment   = Heightmap(baseline.getWidth(), baseline.getHeight());
        auto initialized = next->water.reset(baseline.getWidth(), baseline.getHeight());
        if (!initialized.ok()) return initialized;
        for (int i = 0; i < cursor; ++i) {
            const auto& c = commands[size_t(i)];
            if (!c.enabled) continue;
            auto result = std::visit([&](const auto& operation) { return execute(*next, operation); }, c.operation);
            if (!result.ok()) return result;
        }
        int changed = 0;
        for (size_t i = 0; i < baseline.data().size(); ++i)
            changed += next->terrain.data()[i] != (current ? current->terrain.data()[i] : baseline.data()[i]);
        current = std::move(next);
        return Result<int>::success(changed);
    }
};
TerrainGenerationSession::TerrainGenerationSession()                                               = default;
TerrainGenerationSession::~TerrainGenerationSession()                                              = default;
TerrainGenerationSession::TerrainGenerationSession(TerrainGenerationSession&&) noexcept            = default;
TerrainGenerationSession& TerrainGenerationSession::operator=(TerrainGenerationSession&&) noexcept = default;
TerrainSessionAccess      TerrainGenerationSession::getAccess() const noexcept {
    return impl_ ? impl_->access : TerrainSessionAccess::Editable;
}
TerrainSessionRunStatus TerrainGenerationSession::getReplayStatus() const noexcept {
    return impl_ ? impl_->replayStatus : TerrainSessionRunStatus::Idle;
}
int TerrainGenerationSession::getReplayCompletedOperations() const noexcept {
    return impl_ ? impl_->replayNext : 0;
}
int TerrainGenerationSession::getOperationCount() const noexcept { return impl_ ? int(impl_->commands.size()) : 0; }
int TerrainGenerationSession::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
Result<void> TerrainGenerationSession::setAccess(TerrainSessionAccess access) {
    if (access != TerrainSessionAccess::Editable && access != TerrainSessionAccess::Locked)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.session: unknown access policy"));
    if (impl_ && impl_->replayStatus == TerrainSessionRunStatus::Pending)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.session: cancel pending replay first"));
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->access = access;
    return Result<void>::success();
}
Result<int> TerrainGenerationSession::reset(const Heightmap& baseline) {
    if (getAccess() == TerrainSessionAccess::Locked || !validRaster(baseline))
        return invalid("terrain.session.reset: editable session and finite baseline required");
    auto candidate      = std::make_unique<Impl>();
    candidate->baseline = baseline;
    auto initialized    = candidate->rebuild();
    if (!initialized.ok()) return initialized;
    impl_.swap(candidate);
    return Result<int>::success(int(baseline.data().size()));
}
Result<int> TerrainGenerationSession::stamp(const Heightmap& stamp, const TerrainStampSettings& settings,
                                            const Heightmap& local, const Heightmap& global) {
    return append(Command{StampRecord{stamp, local, global, settings}});
}
Result<int> TerrainGenerationSession::append(Command command) {
    if (!impl_ || !impl_->editable() || impl_->cursor == INT_MAX)
        return invalid("terrain.session.append: initialized editable session required");
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->replayStatus = TerrainSessionRunStatus::Idle;
    candidate->replayCandidate.reset();
    candidate->replayNext = 0;
    candidate->commands.resize(size_t(candidate->cursor));
    candidate->commands.push_back(std::move(command));
    ++candidate->cursor;
    auto result = candidate->rebuild();
    if (!result.ok()) return result;
    impl_.swap(candidate);
    return result;
}

Result<int> TerrainGenerationSession::contrast(const Heightmap& mask, float strength, float featureSize) {
    return append(Command{ContrastRecord{mask, strength, featureSize}});
}
Result<int> TerrainGenerationSession::smooth(const Heightmap& mask, const TerrainSmoothSettings& settings) {
    return append(Command{SmoothRecord{mask, settings}});
}
Result<int> TerrainGenerationSession::ridges(const Heightmap& mask, const TerrainRidgeSettings& settings) {
    return append(Command{RidgeRecord{mask, settings}});
}
Result<int> TerrainGenerationSession::terrace(const Heightmap& mask, const TerrainTerraceSettings& settings) {
    return append(Command{TerraceRecord{mask, settings}});
}
Result<int> TerrainGenerationSession::power(const Heightmap& mask, float power) {
    return append(Command{PowerRecord{mask, power}});
}
Result<int> TerrainGenerationSession::heightCurve(const Heightmap& mask, const Heightmap& curve, float minimum,
                                                  float maximum) {
    return append(Command{CurveRecord{mask, curve, minimum, maximum}});
}
Result<int> TerrainGenerationSession::heightMix(const Heightmap& local, const Heightmap& global,
                                                const TerrainHeightMixSettings& settings) {
    return append(Command{MixRecord{local, global, settings}});
}
Result<int> TerrainGenerationSession::resetSimulation(float depth) {
    return append(Command{SimulationResetRecord{depth}});
}
Result<int> TerrainGenerationSession::thermal(const TerrainThermalSettings& settings) {
    return append(Command{ThermalRecord{settings}});
}
Result<int> TerrainGenerationSession::hydraulic(const TerrainWaterSettings&    water,
                                                const TerrainSedimentSettings& sediment,
                                                const TerrainThermalSettings& thermal, int iterations) {
    return append(Command{HydraulicRecord{water, sediment, thermal, iterations}});
}
Result<int> TerrainGenerationSession::copySediment(Heightmap& output) const {
    if (!impl_ || !impl_->current || !validRaster(output) || output.getWidth() != impl_->baseline.getWidth() ||
        output.getHeight() != impl_->baseline.getHeight())
        return invalid("terrain.session: initialized simulation and matching finite output required");
    return publish(output, impl_->current->sediment.data());
}
Result<int> TerrainGenerationSession::exportWater(Heightmap& output, TerrainWaterChannel channel) const {
    if (!impl_ || !impl_->current) return invalid("terrain.session: initialized simulation required");
    return impl_->current->water.exportChannel(output, channel);
}
Result<int> TerrainGenerationSession::moveCursor(int delta) {
    if (!impl_ || !impl_->editable() || (delta < 0 && impl_->cursor == 0) ||
        (delta > 0 && size_t(impl_->cursor) == impl_->commands.size()))
        return invalid("terrain.session: unavailable history or locked/empty session");
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->replayStatus = TerrainSessionRunStatus::Idle;
    candidate->replayCandidate.reset();
    candidate->replayNext = 0;
    candidate->cursor += delta;
    auto result = candidate->rebuild();
    if (!result.ok()) return result;
    impl_.swap(candidate);
    return result;
}
Result<int> TerrainGenerationSession::replay() { return moveCursor(0); }
Result<TerrainSessionRunStatus> TerrainGenerationSession::beginReplay() {
    if (!impl_ || !impl_->editable())
        return Result<TerrainSessionRunStatus>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "terrain.session.beginReplay: initialized editable session required"));
    SessionState candidate;
    candidate.terrain = impl_->baseline;
    candidate.sediment = Heightmap(impl_->baseline.getWidth(), impl_->baseline.getHeight());
    auto initialized = candidate.water.reset(impl_->baseline.getWidth(), impl_->baseline.getHeight());
    if (!initialized.ok()) return Result<TerrainSessionRunStatus>::failure(initialized.status());
    impl_->replayCandidate = std::move(candidate);
    impl_->replayNext = 0;
    impl_->replayStatus = TerrainSessionRunStatus::Pending;
    return Result<TerrainSessionRunStatus>::success(impl_->replayStatus);
}
Result<TerrainSessionRunStatus> TerrainGenerationSession::stepReplay(int maxOperations) {
    if (!impl_ || impl_->replayStatus != TerrainSessionRunStatus::Pending || !impl_->replayCandidate ||
        maxOperations <= 0)
        return Result<TerrainSessionRunStatus>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "terrain.session.stepReplay: pending replay and positive operation budget required"));
    int visited = 0;
    while (impl_->replayNext < impl_->cursor && visited < maxOperations) {
        const auto& command = impl_->commands[static_cast<std::size_t>(impl_->replayNext)];
        if (command.enabled) {
            auto executed = std::visit(
                [&](const auto& operation) { return execute(*impl_->replayCandidate, operation); }, command.operation);
            if (!executed.ok()) {
                impl_->replayCandidate.reset();
                impl_->replayStatus = TerrainSessionRunStatus::Failed;
                return Result<TerrainSessionRunStatus>::failure(executed.status());
            }
        }
        ++impl_->replayNext;
        ++visited;
    }
    if (impl_->replayNext == impl_->cursor) {
        impl_->current = std::make_shared<const SessionState>(std::move(*impl_->replayCandidate));
        impl_->replayCandidate.reset();
        impl_->replayStatus = TerrainSessionRunStatus::Completed;
    }
    return Result<TerrainSessionRunStatus>::success(impl_->replayStatus);
}
Result<TerrainSessionRunStatus> TerrainGenerationSession::cancelReplay() {
    if (!impl_ || impl_->replayStatus != TerrainSessionRunStatus::Pending)
        return Result<TerrainSessionRunStatus>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.session.cancelReplay: no pending replay"));
    impl_->replayCandidate.reset();
    impl_->replayStatus = TerrainSessionRunStatus::Cancelled;
    return Result<TerrainSessionRunStatus>::success(impl_->replayStatus);
}
Result<int> TerrainGenerationSession::undo() { return moveCursor(-1); }
Result<int> TerrainGenerationSession::redo() { return moveCursor(1); }
Result<int> TerrainGenerationSession::setOperationEnabled(int index, bool enabled) {
    if (!impl_ || !impl_->editable() || index < 0 || size_t(index) >= impl_->commands.size())
        return invalid("terrain.session: valid operation and editable session required");
    auto candidate                             = std::make_unique<Impl>(*impl_);
    candidate->replayStatus                    = TerrainSessionRunStatus::Idle;
    candidate->replayCandidate.reset();
    candidate->replayNext                      = 0;
    candidate->commands[size_t(index)].enabled = enabled;
    auto result                                = candidate->rebuild();
    if (!result.ok()) return result;
    impl_.swap(candidate);
    return result;
}
Result<bool> TerrainGenerationSession::operationEnabled(int index) const {
    if (!impl_ || index < 0 || size_t(index) >= impl_->commands.size())
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.session: invalid operation index"));
    return Result<bool>::success(impl_->commands[size_t(index)].enabled);
}
Result<int> TerrainGenerationSession::copyTerrain(Heightmap& output) const {
    if (!impl_ || !impl_->current || !validRaster(output) || output.getWidth() != impl_->current->terrain.getWidth() ||
        output.getHeight() != impl_->current->terrain.getHeight())
        return invalid("terrain.session: initialized terrain and matching finite output required");
    return publish(output, impl_->current->terrain.data());
}

Result<std::string> TerrainGenerationSession::snapshotJson() const {
    if (!impl_ || !impl_->current || impl_->replayStatus == TerrainSessionRunStatus::Pending)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.snapshot: initialized idle session required"));
    if (impl_->commands.size() > kMaximumSessionCommands)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.snapshot: command count exceeds limit"));
    SessionWriter writer;
    writeSessionHeightmap(writer, impl_->baseline);
    writer.scalar(impl_->cursor);
    writer.scalar(impl_->access);
    writer.scalar(static_cast<std::uint32_t>(impl_->commands.size()));
    for (const auto& command : impl_->commands) {
        writer.scalar(command.enabled);
        writer.scalar(static_cast<std::uint8_t>(command.operation.index()));
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, StampRecord>) {
                writeSessionHeightmap(writer, value.stamp); writeSessionHeightmap(writer, value.local);
                writeSessionHeightmap(writer, value.global); writeSessionValue(writer, value.settings);
            } else if constexpr (std::is_same_v<T, ContrastRecord>) {
                writeSessionHeightmap(writer, value.mask); writer.scalar(value.strength); writer.scalar(value.featureSize);
            } else if constexpr (std::is_same_v<T, SmoothRecord> || std::is_same_v<T, RidgeRecord> ||
                                 std::is_same_v<T, TerraceRecord>) {
                writeSessionHeightmap(writer, value.mask); writeSessionValue(writer, value.settings);
            } else if constexpr (std::is_same_v<T, PowerRecord>) {
                writeSessionHeightmap(writer, value.mask); writer.scalar(value.power);
            } else if constexpr (std::is_same_v<T, CurveRecord>) {
                writeSessionHeightmap(writer, value.mask); writeSessionHeightmap(writer, value.curve);
                writer.scalar(value.minimum); writer.scalar(value.maximum);
            } else if constexpr (std::is_same_v<T, MixRecord>) {
                writeSessionHeightmap(writer, value.local); writeSessionHeightmap(writer, value.global);
                writeSessionValue(writer, value.settings);
            } else if constexpr (std::is_same_v<T, SimulationResetRecord>) {
                writer.scalar(value.depth);
            } else if constexpr (std::is_same_v<T, ThermalRecord>) {
                writeSessionValue(writer, value.settings);
            } else {
                writeSessionValue(writer, value.water); writeSessionValue(writer, value.sediment);
                writeSessionValue(writer, value.thermal); writer.scalar(value.iterations);
            }
        }, command.operation);
    }
    if (writer.bytes.size() > kMaximumSessionSnapshotBytes)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.snapshot: payload exceeds size limit"));
    Value::Object root{{"payload", encodeSessionHex(writer.bytes)},
                       {"schema", "eve.procgen.terrain-generation-session"}, {"version", 1}};
    return Value(std::move(root)).toJson();
}

Result<void> TerrainGenerationSession::restoreJson(const std::string& json) {
    if ((impl_ && (impl_->access == TerrainSessionAccess::Locked ||
                   impl_->replayStatus == TerrainSessionRunStatus::Pending)) ||
        json.size() > kMaximumSessionSnapshotBytes * 2 + 1024)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: editable idle session and bounded JSON required"));
    auto decoded = Value::fromJson(json);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    const auto* root = decoded.value().getIf<Value::Object>();
    if (!root || root->size() != 3 || !root->contains("schema") || !root->contains("version") ||
        !root->contains("payload"))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: missing or unknown root fields"));
    const auto* schema = root->at("schema").getIf<std::string>();
    const auto* version = root->at("version").getIf<std::int64_t>();
    const auto* payload = root->at("payload").getIf<std::string>();
    if (!schema || *schema != "eve.procgen.terrain-generation-session" || !version || *version != 1 || !payload)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: unsupported schema or version"));
    auto bytes = decodeSessionHex(*payload);
    if (!bytes.ok()) return Result<void>::failure(bytes.status());
    SessionReader reader{bytes.value()};
    auto candidate = std::make_unique<Impl>();
    std::uint32_t count{};
    if (!readSessionHeightmap(reader, candidate->baseline) || !reader.scalar(candidate->cursor) ||
        !reader.scalar(candidate->access) || !reader.scalar(count) || count > kMaximumSessionCommands ||
        candidate->cursor < 0 || candidate->cursor > static_cast<int>(count) ||
        (candidate->access != TerrainSessionAccess::Editable && candidate->access != TerrainSessionAccess::Locked))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: invalid session header"));
    candidate->commands.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        bool enabled{}; std::uint8_t kind{};
        if (!reader.scalar(enabled) || !reader.scalar(kind) || kind > 10)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.session.restore: invalid command header"));
        Command command; command.enabled = enabled;
        bool valid = true;
        switch (kind) {
            case 0: { StampRecord v; valid = readSessionHeightmap(reader, v.stamp) && readSessionHeightmap(reader, v.local) && readSessionHeightmap(reader, v.global) && readSessionValue(reader, v.settings); command.operation = std::move(v); break; }
            case 1: { ContrastRecord v; valid = readSessionHeightmap(reader, v.mask) && reader.scalar(v.strength) && reader.scalar(v.featureSize); command.operation = std::move(v); break; }
            case 2: { SmoothRecord v; valid = readSessionHeightmap(reader, v.mask) && readSessionValue(reader, v.settings); command.operation = std::move(v); break; }
            case 3: { RidgeRecord v; valid = readSessionHeightmap(reader, v.mask) && readSessionValue(reader, v.settings); command.operation = std::move(v); break; }
            case 4: { TerraceRecord v; valid = readSessionHeightmap(reader, v.mask) && readSessionValue(reader, v.settings); command.operation = std::move(v); break; }
            case 5: { PowerRecord v; valid = readSessionHeightmap(reader, v.mask) && reader.scalar(v.power); command.operation = std::move(v); break; }
            case 6: { CurveRecord v; valid = readSessionHeightmap(reader, v.mask) && readSessionHeightmap(reader, v.curve) && reader.scalar(v.minimum) && reader.scalar(v.maximum); command.operation = std::move(v); break; }
            case 7: { MixRecord v; valid = readSessionHeightmap(reader, v.local) && readSessionHeightmap(reader, v.global) && readSessionValue(reader, v.settings); command.operation = std::move(v); break; }
            case 8: { SimulationResetRecord v; valid = reader.scalar(v.depth); command.operation = v; break; }
            case 9: { ThermalRecord v; valid = readSessionValue(reader, v.settings); command.operation = v; break; }
            default: { HydraulicRecord v; valid = readSessionValue(reader, v.water) && readSessionValue(reader, v.sediment) && readSessionValue(reader, v.thermal) && reader.scalar(v.iterations); command.operation = v; break; }
        }
        if (!valid) return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: invalid command payload"));
        candidate->commands.push_back(std::move(command));
    }
    if (reader.offset != reader.bytes.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.session.restore: trailing payload data"));
    auto validation = std::make_unique<Impl>(*candidate);
    validation->cursor = static_cast<int>(validation->commands.size());
    for (auto& command : validation->commands) command.enabled = true;
    auto validated = validation->rebuild();
    if (!validated.ok()) return Result<void>::failure(validated.status());
    auto rebuilt = candidate->rebuild();
    if (!rebuilt.ok()) return Result<void>::failure(rebuilt.status());
    impl_.swap(candidate);
    return Result<void>::success();
}
}  // namespace eve::procgen
