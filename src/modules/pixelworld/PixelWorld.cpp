#include "pixelworld/PixelWorld.h"

#include "pixelworld/PixelWorldControl.h"

#include "common/Status.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <chrono>
#include <deque>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::pixelworld {
namespace {

std::atomic<std::uint64_t> nextPixelWorldId{1};

int floorDiv(int value) noexcept {
    return value >= 0 ? value / kPixelChunkSize : -((-value + kPixelChunkSize - 1) / kPixelChunkSize);
}

int floorMod(int value) noexcept {
    const int result = value % kPixelChunkSize;
    return result < 0 ? result + kPixelChunkSize : result;
}

struct ChunkCoord {
    int x = 0;
    int y = 0;
    friend bool operator==(const ChunkCoord&, const ChunkCoord&) = default;
    friend bool operator<(const ChunkCoord& a, const ChunkCoord& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    }
};

struct ChunkCoordHash {
    std::size_t operator()(ChunkCoord coord) const noexcept {
        const auto x = std::uint64_t(std::uint32_t(coord.x));
        const auto y = std::uint64_t(std::uint32_t(coord.y));
        return std::size_t((x << 32U) ^ y);
    }
};

struct Chunk {
    std::array<PixelCell, kPixelChunkSize * kPixelChunkSize> cells{};
    std::array<std::uint64_t, kPixelChunkSize * kPixelChunkSize> updated{};
    std::array<std::uint16_t, 64> materialCounts{};
    std::uint32_t nonAir = 0;
    std::uint32_t mobile = 0;
    std::uint32_t thermalRemainderCells = 0;
    std::uint32_t materialOverflow = 0;
    std::int16_t minimumTemperature = 32767;
    std::int16_t maximumTemperature = -32768;
    bool temperatureBoundsDirty = false;
    std::uint64_t revision = 0;
    bool active = true;
    bool touched = true;
    std::uint8_t idleTicks = 0;

    static std::size_t index(int x, int y) noexcept {
        return std::size_t(y * kPixelChunkSize + x);
    }
};

eve::Status malformed(std::string message) {
    return eve::Status::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::SerializationError, std::move(message), "snapshot", {}, "pixelworld"));
}

template <class T>
void append(std::vector<std::byte>& out, T value) {
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), first, first + sizeof(T));
}

template <class T>
bool read(std::span<const std::byte> bytes, std::size_t& cursor, T& value) {
    if (cursor > bytes.size() || bytes.size() - cursor < sizeof(T)) return false;
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

}  // namespace

struct PixelWorld::Impl {
    explicit Impl(MaterialCatalog ownedCatalog = MaterialCatalog::builtIn())
        : catalog(std::move(ownedCatalog)) {
        rebuildMaterialRuntimeTables();
    }

    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks;
    std::unordered_map<ChunkCoord, std::uint64_t, ChunkCoordHash> removedChunks;
    MaterialCatalog catalog;
    std::vector<std::uint8_t> canDisplaceTable;
    std::vector<MaterialState> materialStates;
    std::vector<std::uint8_t> thermalConductivities;
    std::vector<std::uint16_t> heatCapacities;
    std::vector<std::vector<std::uint16_t>> reactionRulesByPair;
    std::size_t materialRuntimeCount = 0;
    std::uint64_t seed = 1;
    std::uint64_t revision = 0;
    std::uint64_t tick = 0;
    std::uint64_t lastEditSequence = 0;
    std::uint64_t worldId = nextPixelWorldId.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t epoch = 1;
    std::uint64_t nextFragmentId = 1;
    bool paused = false;

    const Chunk* findChunk(int x, int y) const noexcept {
        const auto found = chunks.find({floorDiv(x), floorDiv(y)});
        return found == chunks.end() ? nullptr : &found->second;
    }

    Chunk* findChunk(int x, int y) noexcept {
        const auto found = chunks.find({floorDiv(x), floorDiv(y)});
        return found == chunks.end() ? nullptr : &found->second;
    }

    Chunk& ensureChunk(int x, int y) { return chunks[{floorDiv(x), floorDiv(y)}]; }

    bool isMobile(MaterialId material) const noexcept {
        const std::size_t index = std::size_t(material);
        const auto state = index < materialStates.size() ? materialStates[index]
                                                         : MaterialState::Empty;
        return state == MaterialState::Powder || state == MaterialState::Liquid;
    }

    MaterialState materialState(MaterialId material) const noexcept {
        const std::size_t index = std::size_t(material);
        return index < materialStates.size() ? materialStates[index] : MaterialState::Empty;
    }

    void rebuildMaterialRuntimeTables() {
        materialRuntimeCount = catalog.definitions().size();
        canDisplaceTable.assign(materialRuntimeCount * materialRuntimeCount, 0);
        reactionRulesByPair.assign(materialRuntimeCount * materialRuntimeCount, {});
        materialStates.resize(materialRuntimeCount);
        thermalConductivities.resize(materialRuntimeCount);
        heatCapacities.resize(materialRuntimeCount);
        for (std::size_t source = 0; source < materialRuntimeCount; ++source) {
            const auto& sourceDef = catalog.definition(MaterialId(source));
            materialStates[source] = sourceDef.state;
            thermalConductivities[source] = sourceDef.thermalConductivity;
            heatCapacities[source] = sourceDef.heatCapacity;
            for (std::size_t target = 0; target < materialRuntimeCount; ++target) {
                const auto& targetDef = catalog.definition(MaterialId(target));
                const bool targetFluid = targetDef.state == MaterialState::Empty ||
                                         targetDef.state == MaterialState::Gas ||
                                         targetDef.state == MaterialState::Liquid;
                canDisplaceTable[source * materialRuntimeCount + target] =
                    std::uint8_t(targetFluid && targetDef.density < sourceDef.density);
            }
        }
        const auto reactions = catalog.reactions();
        for (std::size_t ruleIndex = 0; ruleIndex < reactions.size(); ++ruleIndex) {
            const auto& rule = reactions[ruleIndex];
            const std::size_t first = std::size_t(rule.first);
            const std::size_t second = std::size_t(rule.second);
            if (first >= materialRuntimeCount || second >= materialRuntimeCount ||
                ruleIndex > std::size_t(std::numeric_limits<std::uint16_t>::max()))
                continue;
            reactionRulesByPair[first * materialRuntimeCount + second].push_back(
                std::uint16_t(ruleIndex));
            if (first != second)
                reactionRulesByPair[second * materialRuntimeCount + first].push_back(
                    std::uint16_t(ruleIndex));
        }
    }

    bool canDisplace(MaterialId source, MaterialId target) const noexcept {
        const std::size_t sourceIndex = std::size_t(source);
        const std::size_t targetIndex = std::size_t(target);
        if (sourceIndex >= materialRuntimeCount || targetIndex >= materialRuntimeCount) return false;
        return canDisplaceTable[sourceIndex * materialRuntimeCount + targetIndex] != 0;
    }

    std::span<const std::uint16_t> reactionRules(MaterialId source,
                                                 MaterialId target) const noexcept {
        const std::size_t sourceIndex = std::size_t(source);
        const std::size_t targetIndex = std::size_t(target);
        if (sourceIndex >= materialRuntimeCount || targetIndex >= materialRuntimeCount) return {};
        return reactionRulesByPair[sourceIndex * materialRuntimeCount + targetIndex];
    }

    std::uint8_t thermalConductivity(MaterialId material) const noexcept {
        const std::size_t index = std::size_t(material);
        return index < thermalConductivities.size() ? thermalConductivities[index] : 0;
    }

    std::uint16_t heatCapacity(MaterialId material) const noexcept {
        const std::size_t index = std::size_t(material);
        return index < heatCapacities.size() ? heatCapacities[index] : 1;
    }

    void addActivity(Chunk& chunk, const PixelCell& cell) const noexcept {
        if (cell.material == MaterialId::Air) return;
        ++chunk.nonAir;
        if (isMobile(cell.material)) ++chunk.mobile;
        if (cell.thermalRemainder != 0) ++chunk.thermalRemainderCells;
        const auto material = std::size_t(cell.material);
        if (material < chunk.materialCounts.size())
            ++chunk.materialCounts[material];
        else
            ++chunk.materialOverflow;
        chunk.minimumTemperature = std::min(chunk.minimumTemperature, cell.temperature);
        chunk.maximumTemperature = std::max(chunk.maximumTemperature, cell.temperature);
    }

    void removeActivity(Chunk& chunk, const PixelCell& cell) const noexcept {
        if (cell.material == MaterialId::Air) return;
        --chunk.nonAir;
        if (isMobile(cell.material)) --chunk.mobile;
        if (cell.thermalRemainder != 0) --chunk.thermalRemainderCells;
        const auto material = std::size_t(cell.material);
        if (material < chunk.materialCounts.size())
            --chunk.materialCounts[material];
        else
            --chunk.materialOverflow;
        if (cell.temperature == chunk.minimumTemperature || cell.temperature == chunk.maximumTemperature)
            chunk.temperatureBoundsDirty = true;
    }

    void rebuildActivity(Chunk& chunk) const noexcept {
        chunk.materialCounts.fill(0);
        chunk.nonAir = 0;
        chunk.mobile = 0;
        chunk.thermalRemainderCells = 0;
        chunk.materialOverflow = 0;
        chunk.minimumTemperature = 32767;
        chunk.maximumTemperature = -32768;
        chunk.temperatureBoundsDirty = false;
        for (const PixelCell& cell : chunk.cells) addActivity(chunk, cell);
    }

    void refreshTemperatureBounds(Chunk& chunk) const noexcept {
        if (!chunk.temperatureBoundsDirty) return;
        chunk.minimumTemperature = 32767;
        chunk.maximumTemperature = -32768;
        for (const PixelCell& cell : chunk.cells) {
            if (cell.material == MaterialId::Air) continue;
            chunk.minimumTemperature = std::min(chunk.minimumTemperature, cell.temperature);
            chunk.maximumTemperature = std::max(chunk.maximumTemperature, cell.temperature);
        }
        chunk.temperatureBoundsDirty = false;
    }

    PixelCell get(int x, int y) const noexcept {
        const Chunk* chunk = findChunk(x, y);
        return chunk ? chunk->cells[Chunk::index(floorMod(x), floorMod(y))] : PixelCell{};
    }

    PixelCell normalizeCell(PixelCell cell) const noexcept {
        if (cell.material == MaterialId::Air) {
            cell.thermalRemainder = 0;
            return cell;
        }
        const std::int64_t capacity = catalog.definition(cell.material).heatCapacity;
        std::int64_t energy = std::int64_t(cell.temperature) * capacity + cell.thermalRemainder;
        std::int64_t temperature = energy / capacity;
        std::int64_t remainder = energy % capacity;
        if (remainder < 0) {
            --temperature;
            remainder += capacity;
        }
        if (temperature < std::numeric_limits<std::int16_t>::min()) {
            temperature = std::numeric_limits<std::int16_t>::min();
            remainder = 0;
        } else if (temperature > std::numeric_limits<std::int16_t>::max()) {
            temperature = std::numeric_limits<std::int16_t>::max();
            remainder = capacity - 1;
        }
        cell.temperature = std::int16_t(temperature);
        cell.thermalRemainder = std::uint16_t(remainder);
        return cell;
    }

    std::uint64_t updatedAt(int x, int y) const noexcept {
        const Chunk* chunk = findChunk(x, y);
        return chunk ? chunk->updated[Chunk::index(floorMod(x), floorMod(y))] : 0;
    }

    void wakeAround(int x, int y) {
        const int cx = floorDiv(x), cy = floorDiv(y);
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                const auto found = chunks.find({cx + ox, cy + oy});
                if (found != chunks.end()) found->second.active = true;
            }
    }

    void put(int x, int y, PixelCell cell, std::uint64_t updatedTick, bool markUpdated = true) {
        cell = normalizeCell(cell);
        removedChunks.erase({floorDiv(x), floorDiv(y)});
        Chunk& chunk = ensureChunk(x, y);
        const auto index = Chunk::index(floorMod(x), floorMod(y));
        const PixelCell oldCell = chunk.cells[index];
        if (oldCell != cell) {
            removeActivity(chunk, oldCell);
            addActivity(chunk, cell);
        }
        chunk.cells[index] = cell;
        if (markUpdated) chunk.updated[index] = updatedTick;
        chunk.revision = revision + 1;
        chunk.active = true;
        chunk.touched = true;
    }

    void putNormalizedWithinChunk(Chunk& chunk, std::size_t index, const PixelCell& cell,
                                  std::uint64_t updatedTick, bool markUpdated = true) {
        const PixelCell oldCell = chunk.cells[index];
        if (oldCell != cell) {
            removeActivity(chunk, oldCell);
            addActivity(chunk, cell);
        }
        chunk.cells[index] = cell;
        if (markUpdated) chunk.updated[index] = updatedTick;
        chunk.revision = revision + 1;
        chunk.active = true;
        chunk.touched = true;
    }

    void putWithinChunk(Chunk& chunk, std::size_t index, PixelCell cell,
                        std::uint64_t updatedTick, bool markUpdated = true) {
        putNormalizedWithinChunk(chunk, index, normalizeCell(cell), updatedTick, markUpdated);
    }

    bool moveOrSwap(int x, int y, int tx, int ty, std::uint64_t currentTick) {
        const PixelCell source = get(x, y);
        return moveOrSwapKnownSource(x, y, source, tx, ty, currentTick);
    }

    bool moveOrSwapKnownSource(int x, int y, PixelCell source, int tx, int ty,
                               std::uint64_t currentTick) {
        const PixelCell target = get(tx, ty);
        if (!canDisplace(source.material, target.material)) return false;
        put(tx, ty, source, currentTick);
        put(x, y, target, currentTick);
        wakeAround(tx, ty);
        return true;
    }

    bool moveOrSwapWithinChunk(ChunkCoord coord, Chunk& chunk, int sourceX, int sourceY,
                               PixelCell source, int targetX, int targetY,
                               std::uint64_t currentTick) {
        const auto sourceIndex = Chunk::index(sourceX, sourceY);
        const auto targetIndex = Chunk::index(targetX, targetY);
        const PixelCell target = chunk.cells[targetIndex];
        if (!canDisplace(source.material, target.material)) return false;
        chunk.cells[targetIndex] = source;
        chunk.cells[sourceIndex] = target;
        chunk.updated[targetIndex] = currentTick;
        chunk.updated[sourceIndex] = currentTick;
        chunk.revision = revision + 1;
        chunk.active = true;
        chunk.touched = true;
        const bool touchesBoundary = sourceX == 0 || sourceX == kPixelChunkSize - 1 ||
                                     sourceY == 0 || sourceY == kPixelChunkSize - 1 ||
                                     targetX == 0 || targetX == kPixelChunkSize - 1 ||
                                     targetY == 0 || targetY == kPixelChunkSize - 1;
        if (touchesBoundary)
            wakeAround(coord.x * kPixelChunkSize + targetX,
                       coord.y * kPixelChunkSize + targetY);
        return true;
    }

    bool pseudoBit(int x, int y, std::uint64_t currentTick) const noexcept {
        std::uint64_t value = seed ^ (std::uint64_t(std::uint32_t(x)) << 32U) ^
                              std::uint64_t(std::uint32_t(y)) ^ (currentTick * 0x9E3779B97F4A7C15ULL);
        value ^= value >> 30U;
        value *= 0xBF58476D1CE4E5B9ULL;
        value ^= value >> 27U;
        return (value & 1U) != 0;
    }
};

PixelWorld::PixelWorld(std::uint64_t seed) : impl_(std::make_unique<Impl>()) {
    impl_->seed = seed;
    pixelWorldControlService().registerWorld(*this);
}
PixelWorld::PixelWorld(std::uint64_t seed, MaterialCatalog catalog)
    : impl_(std::make_unique<Impl>(std::move(catalog))) {
    impl_->seed = seed;
    pixelWorldControlService().registerWorld(*this);
}
PixelWorld::~PixelWorld() {
    if (impl_) pixelWorldControlService().unregisterWorld(*this);
}
PixelWorld::PixelWorld(PixelWorld&& other) noexcept : impl_(std::move(other.impl_)) {
    if (impl_) pixelWorldControlService().rebindWorld(&other, *this);
}
PixelWorld& PixelWorld::operator=(PixelWorld&& other) noexcept {
    if (this == &other) return *this;
    if (impl_) pixelWorldControlService().unregisterWorld(*this);
    impl_ = std::move(other.impl_);
    if (impl_) pixelWorldControlService().rebindWorld(&other, *this);
    return *this;
}

PixelCell PixelWorld::getCell(int x, int y) const noexcept { return impl_->get(x, y); }
int PixelWorld::getMaterial(int x, int y) const noexcept { return int(impl_->get(x, y).material); }
bool PixelWorld::isSolidMaterial(MaterialId material) const noexcept {
    return impl_->catalog.definition(material).state == MaterialState::Solid;
}
std::uint32_t PixelWorld::materialDisplayRgba(MaterialId material) const noexcept {
    return impl_->catalog.definition(material).displayRgba;
}

eve::Result<PixelCatalogReloadReceipt> PixelWorld::reloadMaterialCatalog(
    MaterialCatalog catalog, std::uint64_t expectedFingerprint) {
    const auto reject = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<PixelCatalogReloadReceipt>::failure(eve::Diagnostic::error(
            code, std::move(message), std::move(path), {}, "pixelworld.catalog-reload"));
    };
    if (!impl_->paused)
        return reject(eve::DiagnosticCode::PreconditionViolation,
                      "material Catalog reload requires a paused world", "paused");
    if (expectedFingerprint != impl_->catalog.fingerprint())
        return reject(eve::DiagnosticCode::Conflict,
                      "material Catalog expected fingerprint is stale", "expectedFingerprint");
    const auto current = impl_->catalog.definitions();
    const auto replacement = catalog.definitions();
    if (current.size() != replacement.size())
        return reject(eve::DiagnosticCode::Conflict,
                      "live Catalog reload cannot add or remove material ids", "materials");
    for (std::size_t index = 0; index < current.size(); ++index)
        if (current[index].id != replacement[index].id || current[index].name != replacement[index].name)
            return reject(eve::DiagnosticCode::Conflict,
                          "live Catalog reload must preserve every material id and name",
                          "materials[" + std::to_string(index) + "]");

    PixelCatalogReloadReceipt receipt;
    receipt.fingerprintBefore = impl_->catalog.fingerprint();
    receipt.fingerprintAfter = catalog.fingerprint();
    receipt.revisionBefore = impl_->revision;
    if (receipt.fingerprintBefore == receipt.fingerprintAfter) {
        receipt.revisionAfter = impl_->revision;
        receipt.worldEpoch = impl_->epoch;
        return eve::Result<PixelCatalogReloadReceipt>::success(receipt);
    }
    impl_->catalog = std::move(catalog);
    impl_->rebuildMaterialRuntimeTables();
    ++impl_->revision;
    ++impl_->epoch;
    impl_->lastEditSequence = 0;
    for (auto& [coord, chunk] : impl_->chunks) {
        for (PixelCell& cell : chunk.cells) cell = impl_->normalizeCell(cell);
        impl_->rebuildActivity(chunk);
        impl_->refreshTemperatureBounds(chunk);
        chunk.revision = impl_->revision;
        chunk.active = true;
        chunk.touched = true;
        chunk.idleTicks = 0;
        ++receipt.chunksRebuilt;
    }
    receipt.revisionAfter = impl_->revision;
    receipt.worldEpoch = impl_->epoch;
    receipt.replayHistoryInvalidated = true;
    return eve::Result<PixelCatalogReloadReceipt>::success(receipt);
}

void PixelWorld::setCell(int x, int y, PixelCell cell) {
    if (cell.material == MaterialId::Air && impl_->findChunk(x, y) == nullptr) return;
    impl_->put(x, y, cell, impl_->tick);
    impl_->wakeAround(x, y);
    ++impl_->revision;
}

void PixelWorld::setMaterial(int x, int y, std::string_view material) {
    setMaterialChecked(x, y, material).expect("PixelWorld::setMaterial requires a registered material");
}

eve::Result<void> PixelWorld::setMaterialChecked(int x, int y, std::string_view material) {
    auto resolved = impl_->catalog.resolve(material);
    if (!resolved.ok()) return eve::Result<void>::failure(resolved.status());
    PixelCell cell;
    cell.material = resolved.value();
    cell.temperature = impl_->catalog.definition(cell.material).defaultTemperature;
    cell.lifetime = std::uint8_t(std::min<std::uint16_t>(impl_->catalog.definition(cell.material).defaultLifetime,
                                                        std::numeric_limits<std::uint8_t>::max()));
    setCell(x, y, cell);
    return eve::Result<void>::success();
}

std::size_t PixelWorld::paintCircle(int centerX, int centerY, int radius, std::string_view material) {
    return std::move(paintCircleChecked(centerX, centerY, radius, material))
        .expect("PixelWorld::paintCircle requires a registered material");
}

eve::Result<std::size_t> PixelWorld::paintCircleChecked(int centerX, int centerY, int radius,
                                                        std::string_view material) {
    if (radius < 0) return eve::Result<std::size_t>::success(0);
    std::size_t changed = 0;
    auto resolved = impl_->catalog.resolve(material);
    if (!resolved.ok()) return eve::Result<std::size_t>::failure(resolved.status());
    const MaterialId id = resolved.value();
    for (int y = centerY - radius; y <= centerY + radius; ++y)
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const int dx = x - centerX, dy = y - centerY;
            if (dx * dx + dy * dy > radius * radius || impl_->get(x, y).material == id) continue;
            PixelCell cell;
            cell.material = id;
            cell.temperature = impl_->catalog.definition(id).defaultTemperature;
            cell.lifetime = std::uint8_t(std::min<std::uint16_t>(impl_->catalog.definition(id).defaultLifetime,
                                                                std::numeric_limits<std::uint8_t>::max()));
            setCell(x, y, cell);
            ++changed;
        }
    return eve::Result<std::size_t>::success(changed);
}

eve::Result<PixelEditReceipt> PixelWorld::applyEdit(const PixelEditCommand& command) {
    const auto reject = [](std::string message, std::string path) {
        return eve::Result<PixelEditReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, std::move(message), std::move(path), {},
            "pixelworld.edit"));
    };
    if (command.sequence != impl_->lastEditSequence + 1)
        return reject("edit sequence must be exactly previous sequence + 1", "sequence");
    if (command.radius < 0 || command.radius > 4096)
        return reject("edit radius must be in [0, 4096]", "radius");
    if (command.centerX < -100'000'000 || command.centerX > 100'000'000 ||
        command.centerY < -100'000'000 || command.centerY > 100'000'000)
        return reject("edit center is outside the supported coordinate range", "center");
    if (command.kind == PixelEditKind::PaintCircle &&
        std::size_t(command.material) >= impl_->catalog.definitions().size())
        return reject("paint command references an unknown material id", "material");
    if (command.kind == PixelEditKind::Explosion && (command.strength < 0 || command.strength > 1'000'000))
        return reject("explosion strength must be in [0, 1000000]", "strength");

    struct Candidate {
        int x = 0;
        int y = 0;
        PixelCell cell;
        bool removed = false;
        bool heated = false;
    };
    std::vector<Candidate> candidates;
    const std::int64_t radiusSquared = std::int64_t(command.radius) * command.radius;
    for (int y = command.centerY - command.radius; y <= command.centerY + command.radius; ++y)
        for (int x = command.centerX - command.radius; x <= command.centerX + command.radius; ++x) {
            const std::int64_t dx = std::int64_t(x) - command.centerX;
            const std::int64_t dy = std::int64_t(y) - command.centerY;
            const std::int64_t distanceSquared = dx * dx + dy * dy;
            if (distanceSquared > radiusSquared) continue;
            const PixelCell oldCell = impl_->get(x, y);
            PixelCell newCell = oldCell;
            bool removed = false;
            bool heated = false;
            if (command.kind == PixelEditKind::PaintCircle) {
                newCell = {};
                newCell.material = command.material;
                const auto& definition = impl_->catalog.definition(command.material);
                newCell.temperature = definition.defaultTemperature;
                newCell.lifetime = std::uint8_t(std::min<std::uint16_t>(
                    definition.defaultLifetime, std::numeric_limits<std::uint8_t>::max()));
            } else if (command.kind == PixelEditKind::HeatCircle) {
                if (oldCell.material == MaterialId::Air) continue;
                newCell.temperature = std::int16_t(std::clamp<int>(
                    int(oldCell.temperature) + command.temperatureDelta, -32768, 32767));
                heated = newCell.temperature != oldCell.temperature;
            } else {
                if (oldCell.material == MaterialId::Air) continue;
                const std::int64_t denominator = std::max<std::int64_t>(1, radiusSquared);
                const int radialStrength = int(std::int64_t(command.strength) *
                                               (radiusSquared - distanceSquared + 1) / denominator);
                if (radialStrength >= impl_->catalog.definition(oldCell.material).blastResistance) {
                    newCell = {};
                    removed = true;
                } else if (command.temperatureDelta != 0) {
                    const int radialHeat = int(std::int64_t(command.temperatureDelta) *
                                               (radiusSquared - distanceSquared + 1) / denominator);
                    newCell.temperature = std::int16_t(std::clamp<int>(
                        int(oldCell.temperature) + radialHeat, -32768, 32767));
                    heated = newCell.temperature != oldCell.temperature;
                }
            }
            if (newCell == oldCell) continue;
            candidates.push_back({x, y, newCell, removed, heated});
        }

    PixelEditReceipt receipt;
    receipt.sequence = command.sequence;
    receipt.revisionBefore = impl_->revision;
    for (const Candidate& candidate : candidates) {
        impl_->put(candidate.x, candidate.y, candidate.cell, impl_->tick);
        impl_->wakeAround(candidate.x, candidate.y);
        ++receipt.cellsChanged;
        if (candidate.removed) ++receipt.cellsRemoved;
        if (candidate.heated) ++receipt.cellsHeated;
    }
    impl_->lastEditSequence = command.sequence;
    ++impl_->revision;
    receipt.revisionAfter = impl_->revision;
    return eve::Result<PixelEditReceipt>::success(receipt);
}

PixelEditReceipt PixelWorld::explode(int centerX, int centerY, int radius, int strength,
                                     std::int16_t temperatureDelta) {
    PixelEditCommand command;
    command.sequence = impl_->lastEditSequence + 1;
    command.kind = PixelEditKind::Explosion;
    command.centerX = centerX;
    command.centerY = centerY;
    command.radius = radius;
    command.strength = strength;
    command.temperatureDelta = temperatureDelta;
    return std::move(applyEdit(command)).expect("PixelWorld::explode generated an invalid edit command");
}

eve::Result<std::vector<PixelFragment>> PixelWorld::extractUnsupportedFragments(
    PixelRegion region, int supportY, std::uint32_t minimumCells) {
    const auto reject = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<std::vector<PixelFragment>>::failure(eve::Diagnostic::error(
            code, std::move(message), std::move(path), {}, "pixelworld.fragment.extract"));
    };
    const std::int64_t width = std::int64_t(region.maxX) - region.minX + 1;
    const std::int64_t height = std::int64_t(region.maxY) - region.minY + 1;
    if (region.minX < -100'000'000 || region.maxX > 100'000'000 ||
        region.minY < -100'000'000 || region.maxY > 100'000'000)
        return reject(eve::DiagnosticCode::InvalidArgument,
                      "fragment scan region is outside the supported coordinate range", "region");
    if (width <= 0 || height <= 0 || width * height > 16'777'216)
        return reject(eve::DiagnosticCode::InvalidArgument,
                      "fragment scan region must be non-empty and contain at most 16777216 cells", "region");
    if (minimumCells == 0)
        return reject(eve::DiagnosticCode::InvalidArgument, "minimumCells must be positive", "minimumCells");

    using Coord = std::pair<int, int>;
    std::set<Coord> visited;
    std::vector<PixelFragment> fragments;
    constexpr std::array<Coord, 4> neighbors{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}}};
    const auto isSolid = [this](int x, int y) {
        return impl_->catalog.definition(impl_->get(x, y).material).state == MaterialState::Solid;
    };

    for (int y = region.minY; y <= region.maxY; ++y)
        for (int x = region.minX; x <= region.maxX; ++x) {
            if (!isSolid(x, y) || visited.contains({x, y})) continue;
            std::deque<Coord> pending{{x, y}};
            std::vector<Coord> component;
            visited.emplace(x, y);
            bool supported = false;
            int minX = x, maxX = x, minY = y, maxY = y;
            while (!pending.empty()) {
                const auto [cx, cy] = pending.front();
                pending.pop_front();
                component.emplace_back(cx, cy);
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                if (cy >= supportY) supported = true;
                for (const auto& [dx, dy] : neighbors) {
                    const int nx = cx + dx, ny = cy + dy;
                    const bool inside = nx >= region.minX && nx <= region.maxX &&
                                        ny >= region.minY && ny <= region.maxY;
                    if (!inside) {
                        if (isSolid(nx, ny)) supported = true;
                        continue;
                    }
                    if (isSolid(nx, ny) && visited.emplace(nx, ny).second)
                        pending.emplace_back(nx, ny);
                }
            }
            if (supported || component.size() < minimumCells) continue;

            PixelFragment fragment;
            fragment.source = worldLink();
            fragment.id = impl_->nextFragmentId++;
            fragment.originX = minX;
            fragment.originY = minY;
            fragment.width = maxX - minX + 1;
            fragment.height = maxY - minY + 1;
            fragment.solidCellCount = std::uint32_t(component.size());
            fragment.cells.resize(std::size_t(fragment.width) * std::size_t(fragment.height));
            for (const auto& [cx, cy] : component)
                fragment.cells[std::size_t(cy - minY) * std::size_t(fragment.width) + std::size_t(cx - minX)] =
                    impl_->get(cx, cy);
            fragments.push_back(std::move(fragment));
        }

    if (!fragments.empty()) {
        for (const PixelFragment& fragment : fragments)
            for (int y = 0; y < fragment.height; ++y)
                for (int x = 0; x < fragment.width; ++x) {
                    const PixelCell& cell = fragment.cells[std::size_t(y) * std::size_t(fragment.width) + x];
                    if (cell.material == MaterialId::Air) continue;
                    impl_->put(fragment.originX + x, fragment.originY + y, {}, impl_->tick);
                    impl_->wakeAround(fragment.originX + x, fragment.originY + y);
                }
        ++impl_->revision;
    }
    return eve::Result<std::vector<PixelFragment>>::success(std::move(fragments));
}

eve::Result<PixelFragmentRasterReceipt> PixelWorld::rasterizeFragment(
    const PixelFragment& fragment, int originX, int originY) {
    const auto reject = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<PixelFragmentRasterReceipt>::failure(eve::Diagnostic::error(
            code, std::move(message), std::move(path), {}, "pixelworld.fragment.rasterize"));
    };
    if (fragment.source != worldLink())
        return reject(eve::DiagnosticCode::StaleHandle,
                      "fragment belongs to another world or an invalidated world epoch", "source");
    if (fragment.id == 0 || fragment.width <= 0 || fragment.height <= 0 ||
        std::uint64_t(fragment.width) * std::uint64_t(fragment.height) != fragment.cells.size())
        return reject(eve::DiagnosticCode::InvalidArgument, "fragment bitmap metadata is invalid", "fragment");
    std::uint32_t cellsPlaced = 0;
    for (int y = 0; y < fragment.height; ++y)
        for (int x = 0; x < fragment.width; ++x) {
            const PixelCell& cell = fragment.cells[std::size_t(y) * std::size_t(fragment.width) + x];
            if (cell.material == MaterialId::Air) continue;
            if (std::size_t(cell.material) >= impl_->catalog.definitions().size())
                return reject(eve::DiagnosticCode::InvalidArgument, "fragment contains an unknown material", "cells");
            const std::int64_t wx = std::int64_t(originX) + x, wy = std::int64_t(originY) + y;
            if (wx < std::numeric_limits<int>::min() || wx > std::numeric_limits<int>::max() ||
                wy < std::numeric_limits<int>::min() || wy > std::numeric_limits<int>::max())
                return reject(eve::DiagnosticCode::InvalidArgument, "fragment target coordinates overflow", "origin");
            if (impl_->get(int(wx), int(wy)).material != MaterialId::Air)
                return reject(eve::DiagnosticCode::Conflict, "fragment target overlaps authoritative terrain", "origin");
            ++cellsPlaced;
        }
    if (cellsPlaced != fragment.solidCellCount)
        return reject(eve::DiagnosticCode::InvalidArgument, "fragment solid cell count does not match bitmap", "solidCellCount");

    PixelFragmentRasterReceipt receipt;
    receipt.fragmentId = fragment.id;
    receipt.revisionBefore = impl_->revision;
    receipt.cellsPlaced = cellsPlaced;
    for (int y = 0; y < fragment.height; ++y)
        for (int x = 0; x < fragment.width; ++x) {
            const PixelCell& cell = fragment.cells[std::size_t(y) * std::size_t(fragment.width) + x];
            if (cell.material == MaterialId::Air) continue;
            impl_->put(originX + x, originY + y, cell, impl_->tick);
            impl_->wakeAround(originX + x, originY + y);
        }
    if (cellsPlaced != 0) ++impl_->revision;
    receipt.revisionAfter = impl_->revision;
    return eve::Result<PixelFragmentRasterReceipt>::success(receipt);
}

PixelWorldLink PixelWorld::worldLink() const noexcept { return {impl_->worldId, impl_->epoch}; }

void PixelWorld::clear() noexcept {
    impl_->chunks.clear();
    impl_->removedChunks.clear();
    impl_->revision = 0;
    impl_->tick = 0;
    impl_->lastEditSequence = 0;
    ++impl_->epoch;
}

eve::Result<StepStats> PixelWorld::advance(eve::SimulationTick tick) {
    if (impl_->paused) return eve::Result<StepStats>::success({eve::SimulationTick(impl_->tick)});
    return advanceImpl(tick, nullptr);
}

eve::Result<StepStats> PixelWorld::advanceScheduled(eve::SimulationTick tick,
                                                    PixelWorkScheduler& scheduler) {
    if (impl_->paused) return eve::Result<StepStats>::success({eve::SimulationTick(impl_->tick)});
    return advanceImpl(tick, &scheduler);
}

eve::Result<StepStats> PixelWorld::advanceImpl(eve::SimulationTick tick,
                                               PixelWorkScheduler* scheduler) {
    if (tick.value() <= impl_->tick)
        return eve::Result<StepStats>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "PixelWorld ticks must increase monotonically", "tick", {}, "pixelworld"));

    const auto stepStarted = std::chrono::steady_clock::now();
    impl_->tick = tick.value();
    StepStats stats;
    stats.tick = tick;
    std::vector<ChunkCoord> order;
    order.reserve(impl_->chunks.size());
    for (auto& [coord, chunk] : impl_->chunks) {
        if (chunk.active) order.push_back(coord);
        chunk.touched = false;
    }
    std::sort(order.begin(), order.end());

    struct ChunkPhaseSummary {
        std::uint32_t nonAir = 0;
        std::uint32_t mobile = 0;
        std::uint32_t thermalRemainderCells = 0;
        std::uint64_t materialMask = 0;
        std::int16_t minimumTemperature = 32767;
        std::int16_t maximumTemperature = -32768;
        bool materialMaskOverflow = false;
    };
    const auto summarize = [](const Chunk& chunk) {
        ChunkPhaseSummary summary;
        summary.nonAir = chunk.nonAir;
        summary.mobile = chunk.mobile;
        summary.thermalRemainderCells = chunk.thermalRemainderCells;
        summary.minimumTemperature = chunk.minimumTemperature;
        summary.maximumTemperature = chunk.maximumTemperature;
        summary.materialMaskOverflow = chunk.materialOverflow != 0;
        for (std::size_t material = 0; material < chunk.materialCounts.size(); ++material)
            if (chunk.materialCounts[material] != 0)
                summary.materialMask |= std::uint64_t(1) << material;
        return summary;
    };
    std::unordered_map<ChunkCoord, ChunkPhaseSummary, ChunkCoordHash> phaseSummaries;
    const auto rebuildSummaries = [&] {
        phaseSummaries.clear();
        phaseSummaries.reserve(impl_->chunks.size());
        for (auto& [coord, chunk] : impl_->chunks) {
            impl_->refreshTemperatureBounds(chunk);
            phaseSummaries.emplace(coord, summarize(chunk));
        }
    };
    rebuildSummaries();

    struct MovementRows {
        std::array<std::uint64_t, kPixelChunkSize> mobile{};
        std::array<std::uint64_t, kPixelChunkSize> rightFirst{};
    };
    std::vector<MovementRows> movementCandidates(order.size());
    std::vector<std::size_t> movementWork;
    movementWork.reserve(order.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        const auto summary = phaseSummaries.find(order[index]);
        if (summary != phaseSummaries.end() && summary->second.mobile != 0)
            movementWork.push_back(index);
    }
    const auto generateMovementCandidates = [&](std::size_t workIndex) {
        const std::size_t orderIndex = movementWork[workIndex];
        const ChunkCoord coord = order[orderIndex];
        auto& candidates = movementCandidates[orderIndex];
        candidates.mobile.fill(0);
        candidates.rightFirst.fill(0);
        const auto found = impl_->chunks.find(coord);
        if (found == impl_->chunks.end()) return;
        const Chunk& chunk = found->second;
        for (int ly = kPixelChunkSize - 1; ly >= 0; --ly) {
            for (int lx = 0; lx < kPixelChunkSize; ++lx) {
                const auto cellIndex = Chunk::index(lx, ly);
                if (chunk.updated[cellIndex] == tick.value()) continue;
                const PixelCell cell = chunk.cells[cellIndex];
                const auto state = impl_->materialState(cell.material);
                if (state != MaterialState::Powder && state != MaterialState::Liquid) continue;
                const int x = coord.x * kPixelChunkSize + lx;
                const int y = coord.y * kPixelChunkSize + ly;
                const int direction = impl_->pseudoBit(x, y, tick.value()) ? 1 : -1;
                const std::uint64_t bit = std::uint64_t(1) << lx;
                candidates.mobile[std::size_t(ly)] |= bit;
                if (direction > 0) candidates.rightFirst[std::size_t(ly)] |= bit;
            }
        }
    };
    if (scheduler && movementWork.size() > 1) {
        scheduler->parallelFor(movementWork.size(), generateMovementCandidates);
        stats.parallelTasks += std::uint32_t(movementWork.size());
    } else {
        for (std::size_t index = 0; index < movementWork.size(); ++index)
            generateMovementCandidates(index);
    }
    for (std::size_t orderIndex = 0; orderIndex < order.size(); ++orderIndex) {
        ++stats.chunksVisited;
        const ChunkCoord sourceCoord = order[orderIndex];
        auto sourceFound = impl_->chunks.find(sourceCoord);
        if (sourceFound == impl_->chunks.end()) continue;
        Chunk& sourceChunk = sourceFound->second;
        const MovementRows& candidates = movementCandidates[orderIndex];
        for (int sourceLocalY = kPixelChunkSize - 1; sourceLocalY >= 0; --sourceLocalY) {
            std::uint64_t remaining = candidates.mobile[std::size_t(sourceLocalY)];
            const bool leftFirst = ((tick.value() + std::uint64_t(sourceLocalY)) & 1U) == 0;
            while (remaining != 0) {
                const int sourceLocalX = leftFirst
                                             ? int(std::countr_zero(remaining))
                                             : 63 - int(std::countl_zero(remaining));
                const std::uint64_t candidateBit = std::uint64_t(1) << sourceLocalX;
                remaining &= ~candidateBit;
                const int direction =
                    (candidates.rightFirst[std::size_t(sourceLocalY)] & candidateBit) != 0 ? 1 : -1;
                const auto sourceIndex = Chunk::index(sourceLocalX, sourceLocalY);
                if (sourceChunk.updated[sourceIndex] == tick.value()) continue;
                const PixelCell cell = sourceChunk.cells[sourceIndex];
                const auto state = impl_->materialState(cell.material);
                if (state != MaterialState::Powder && state != MaterialState::Liquid) continue;
                ++stats.cellsVisited;
                const auto tryMove = [&](int targetLocalX, int targetLocalY) {
                    if (targetLocalX >= 0 && targetLocalX < kPixelChunkSize &&
                        targetLocalY >= 0 && targetLocalY < kPixelChunkSize)
                        return impl_->moveOrSwapWithinChunk(
                            sourceCoord, sourceChunk, sourceLocalX, sourceLocalY, cell,
                            targetLocalX, targetLocalY, tick.value());
                    const int sourceX = sourceCoord.x * kPixelChunkSize + sourceLocalX;
                    const int sourceY = sourceCoord.y * kPixelChunkSize + sourceLocalY;
                    return impl_->moveOrSwapKnownSource(
                        sourceX, sourceY, cell,
                        sourceCoord.x * kPixelChunkSize + targetLocalX,
                        sourceCoord.y * kPixelChunkSize + targetLocalY, tick.value());
                };
                bool moved = tryMove(sourceLocalX, sourceLocalY + 1);
                if (!moved)
                    moved = tryMove(sourceLocalX + direction, sourceLocalY + 1);
                if (!moved)
                    moved = tryMove(sourceLocalX - direction, sourceLocalY + 1);
                if (!moved && state == MaterialState::Liquid)
                    moved = tryMove(sourceLocalX + direction, sourceLocalY);
                if (!moved && state == MaterialState::Liquid)
                    moved = tryMove(sourceLocalX - direction, sourceLocalY);
                if (moved) {
                    ++stats.cellsMoved;
                    stats.cellsChanged += 2;
                }
            }
        }
    }

    if (stats.cellsMoved != 0) rebuildSummaries();

    // Thermal phase: calculate all pair transfers from the same pre-phase state,
    // then apply deltas in canonical coordinate order. Integer arithmetic makes
    // the reference backend bit-exact and independent of unordered-map order.
    struct ThermalChunkContributions {
        std::array<std::int64_t, kPixelChunkSize * kPixelChunkSize> local{};
        std::array<std::int64_t, kPixelChunkSize> rightHalo{};
        std::array<std::int64_t, kPixelChunkSize> bottomHalo{};
    };
    std::vector<ThermalChunkContributions> thermalCandidates(order.size());
    std::vector<std::uint32_t> transferCounts(order.size());
    std::vector<std::uint64_t> transferEnergy(order.size());
    const auto hasPossibleThermalGradient = [&](ChunkCoord coord) {
        const auto ownSummary = phaseSummaries.find(coord);
        if (ownSummary == phaseSummaries.end() || ownSummary->second.nonAir == 0) return false;
        const auto& own = ownSummary->second;
        if (own.thermalRemainderCells != 0) return true;
        if (own.minimumTemperature != own.maximumTemperature) return true;
        for (const auto& [dx, dy] :
             std::array<std::pair<int, int>, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}}) {
            const auto neighbor = phaseSummaries.find({coord.x + dx, coord.y + dy});
            if (neighbor == phaseSummaries.end() || neighbor->second.nonAir == 0) continue;
            if (neighbor->second.minimumTemperature != own.minimumTemperature ||
                neighbor->second.maximumTemperature != own.maximumTemperature)
                return true;
        }
        return false;
    };
    std::vector<std::size_t> thermalWork;
    thermalWork.reserve(order.size());
    for (std::size_t index = 0; index < order.size(); ++index)
        if (hasPossibleThermalGradient(order[index])) thermalWork.push_back(index);
    const auto& readOnlyChunks = impl_->chunks;
    const auto calculateThermalChunk = [&](std::size_t workIndex) {
        const std::size_t orderIndex = thermalWork[workIndex];
        const ChunkCoord coord = order[orderIndex];
        const auto ownFound = readOnlyChunks.find(coord);
        if (ownFound == readOnlyChunks.end()) return;
        const Chunk& ownChunk = ownFound->second;
        const auto rightFound = readOnlyChunks.find({coord.x + 1, coord.y});
        const auto bottomFound = readOnlyChunks.find({coord.x, coord.y + 1});
        const Chunk* rightChunk = rightFound == readOnlyChunks.end() ? nullptr : &rightFound->second;
        const Chunk* bottomChunk = bottomFound == readOnlyChunks.end() ? nullptr : &bottomFound->second;
        auto& candidates = thermalCandidates[orderIndex];
        transferCounts[orderIndex] = 0;
        transferEnergy[orderIndex] = 0;
        for (int ly = 0; ly < kPixelChunkSize; ++ly)
            for (int lx = 0; lx < kPixelChunkSize; ++lx) {
                const PixelCell source = ownChunk.cells[Chunk::index(lx, ly)];
                if (source.material == MaterialId::Air) continue;
                for (const auto& [ox, oy] : std::array<std::pair<int, int>, 2>{{{1, 0}, {0, 1}}}) {
                    PixelCell target;
                    if (ox != 0 && lx == kPixelChunkSize - 1) {
                        if (rightChunk == nullptr) continue;
                        target = rightChunk->cells[Chunk::index(0, ly)];
                    } else if (oy != 0 && ly == kPixelChunkSize - 1) {
                        if (bottomChunk == nullptr) continue;
                        target = bottomChunk->cells[Chunk::index(lx, 0)];
                    } else {
                        target = ownChunk.cells[Chunk::index(lx + ox, ly + oy)];
                    }
                    if (target.material == MaterialId::Air) continue;
                    const int conductivity = std::min<int>(
                        impl_->thermalConductivity(source.material),
                        impl_->thermalConductivity(target.material));
                    if (conductivity == 0) continue;
                    const std::int64_t sourceCapacity = impl_->heatCapacity(source.material);
                    const std::int64_t targetCapacity = impl_->heatCapacity(target.material);
                    const std::int64_t sourceEnergy =
                        std::int64_t(source.temperature) * sourceCapacity + source.thermalRemainder;
                    const std::int64_t targetEnergy =
                        std::int64_t(target.temperature) * targetCapacity + target.thermalRemainder;
                    const std::int64_t equilibriumTransfer =
                        (targetEnergy * sourceCapacity - sourceEnergy * targetCapacity) /
                        (sourceCapacity + targetCapacity);
                    if (equilibriumTransfer == 0) continue;
                    std::int64_t transfer = equilibriumTransfer * conductivity / 255;
                    if (transfer == 0) transfer = equilibriumTransfer < 0 ? -1 : 1;
                    const std::size_t sourceIndex = Chunk::index(lx, ly);
                    candidates.local[sourceIndex] += transfer;
                    if (ox != 0 && lx == kPixelChunkSize - 1)
                        candidates.rightHalo[std::size_t(ly)] -= transfer;
                    else if (oy != 0 && ly == kPixelChunkSize - 1)
                        candidates.bottomHalo[std::size_t(lx)] -= transfer;
                    else
                        candidates.local[Chunk::index(lx + ox, ly + oy)] -= transfer;
                    ++transferCounts[orderIndex];
                    transferEnergy[orderIndex] += std::uint64_t(transfer < 0 ? -transfer : transfer);
                }
            }
    };
    if (scheduler && thermalWork.size() > 1) {
        scheduler->parallelFor(thermalWork.size(), calculateThermalChunk);
        stats.parallelTasks += std::uint32_t(thermalWork.size());
    } else {
        for (std::size_t index = 0; index < thermalWork.size(); ++index) calculateThermalChunk(index);
    }
    using ThermalDeltaChunk = std::array<std::int64_t, kPixelChunkSize * kPixelChunkSize>;
    std::unordered_map<ChunkCoord, ThermalDeltaChunk, ChunkCoordHash> thermalEnergyDeltas;
    thermalEnergyDeltas.reserve(order.size());
    for (std::size_t index = 0; index < thermalCandidates.size(); ++index) {
        stats.temperatureTransfers += transferCounts[index];
        stats.thermalEnergyTransferred += transferEnergy[index];
        const ChunkCoord coord = order[index];
        const ThermalChunkContributions& contributions = thermalCandidates[index];
        auto& localDeltas = thermalEnergyDeltas[coord];
        for (std::size_t cellIndex = 0; cellIndex < contributions.local.size(); ++cellIndex)
            localDeltas[cellIndex] += contributions.local[cellIndex];
        auto& rightDeltas = thermalEnergyDeltas[{coord.x + 1, coord.y}];
        auto& bottomDeltas = thermalEnergyDeltas[{coord.x, coord.y + 1}];
        for (std::size_t offset = 0; offset < kPixelChunkSize; ++offset) {
            rightDeltas[Chunk::index(0, int(offset))] += contributions.rightHalo[offset];
            bottomDeltas[Chunk::index(int(offset), 0)] += contributions.bottomHalo[offset];
        }
    }
    std::vector<ChunkCoord> thermalChunkOrder;
    thermalChunkOrder.reserve(thermalEnergyDeltas.size());
    for (const auto& [coord, deltas] : thermalEnergyDeltas) {
        (void)deltas;
        thermalChunkOrder.push_back(coord);
    }
    std::sort(thermalChunkOrder.begin(), thermalChunkOrder.end());
    struct ThermalCommitStats {
        std::uint32_t cellsChanged = 0;
        std::uint64_t energyClamped = 0;
    };
    std::vector<Chunk*> thermalCommitChunks;
    std::vector<const ThermalDeltaChunk*> thermalCommitDeltas;
    thermalCommitChunks.reserve(thermalChunkOrder.size());
    thermalCommitDeltas.reserve(thermalChunkOrder.size());
    for (const ChunkCoord coord : thermalChunkOrder) {
        auto chunkFound = impl_->chunks.find(coord);
        if (chunkFound == impl_->chunks.end()) continue;
        thermalCommitChunks.push_back(&chunkFound->second);
        thermalCommitDeltas.push_back(&thermalEnergyDeltas.at(coord));
    }
    std::vector<ThermalCommitStats> thermalCommitStats(thermalCommitChunks.size());
    const auto commitThermalChunk = [&](std::size_t index) {
        Chunk& chunk = *thermalCommitChunks[index];
        const ThermalDeltaChunk& deltas = *thermalCommitDeltas[index];
        ThermalCommitStats& commitStats = thermalCommitStats[index];
        for (int localY = 0; localY < kPixelChunkSize; ++localY)
            for (int localX = 0; localX < kPixelChunkSize; ++localX) {
                const std::size_t cellIndex = Chunk::index(localX, localY);
                const std::int64_t energyDelta = deltas[cellIndex];
                if (energyDelta == 0) continue;
                PixelCell cell = chunk.cells[cellIndex];
                const std::int64_t capacity =
                    impl_->catalog.definition(cell.material).heatCapacity;
                const std::int64_t beforeEnergy =
                    std::int64_t(cell.temperature) * capacity + cell.thermalRemainder;
                const std::int64_t requestedEnergy = beforeEnergy + energyDelta;
                std::int64_t temperature = requestedEnergy / capacity;
                std::int64_t remainder = requestedEnergy % capacity;
                if (remainder < 0) {
                    --temperature;
                    remainder += capacity;
                }
                if (temperature < std::numeric_limits<std::int16_t>::min()) {
                    temperature = std::numeric_limits<std::int16_t>::min();
                    remainder = 0;
                } else if (temperature > std::numeric_limits<std::int16_t>::max()) {
                    temperature = std::numeric_limits<std::int16_t>::max();
                    remainder = capacity - 1;
                }
                cell.temperature = std::int16_t(temperature);
                cell.thermalRemainder = std::uint16_t(remainder);
                const std::int64_t appliedEnergy =
                    std::int64_t(cell.temperature) * capacity + cell.thermalRemainder;
                if (appliedEnergy != requestedEnergy)
                    commitStats.energyClamped += std::uint64_t(
                        appliedEnergy > requestedEnergy ? appliedEnergy - requestedEnergy
                                                        : requestedEnergy - appliedEnergy);
                if (appliedEnergy == beforeEnergy) continue;
                impl_->putNormalizedWithinChunk(chunk, cellIndex, cell, tick.value(), false);
                ++commitStats.cellsChanged;
            }
    };
    if (scheduler && thermalCommitChunks.size() > 1) {
        scheduler->parallelFor(thermalCommitChunks.size(), commitThermalChunk);
        stats.parallelTasks += std::uint32_t(thermalCommitChunks.size());
    } else {
        for (std::size_t index = 0; index < thermalCommitChunks.size(); ++index)
            commitThermalChunk(index);
    }
    for (const ThermalCommitStats& commitStats : thermalCommitStats) {
        stats.cellsChanged += commitStats.cellsChanged;
        stats.thermalEnergyClamped += commitStats.energyClamped;
    }

    // Phase transitions are table-driven and choose the first canonical rule.
    for (const ChunkCoord coord : order) {
        if (impl_->chunks.find(coord) == impl_->chunks.end()) continue;
        const auto summary = phaseSummaries.find(coord);
        if (summary != phaseSummaries.end() && !summary->second.materialMaskOverflow) {
            bool mayTransition = false;
            for (const auto& rule : impl_->catalog.phaseRules()) {
                const auto material = std::size_t(rule.source);
                if (material >= 64 ||
                    (summary->second.materialMask & (std::uint64_t(1) << material)) == 0)
                    continue;
                const bool temperatureMayMatch =
                    rule.direction == TemperatureDirection::AtOrAbove
                        ? summary->second.maximumTemperature >= rule.threshold
                        : summary->second.minimumTemperature <= rule.threshold;
                if (temperatureMayMatch) {
                    mayTransition = true;
                    break;
                }
            }
            if (!mayTransition) continue;
        }
        for (int ly = 0; ly < kPixelChunkSize; ++ly)
            for (int lx = 0; lx < kPixelChunkSize; ++lx) {
                const int x = coord.x * kPixelChunkSize + lx;
                const int y = coord.y * kPixelChunkSize + ly;
                PixelCell cell = impl_->get(x, y);
                if (cell.material == MaterialId::Air || !impl_->catalog.canPhaseTransition(cell.material))
                    continue;
                for (const auto& rule : impl_->catalog.phaseRules()) {
                    if (rule.source != cell.material) continue;
                    const bool matches = rule.direction == TemperatureDirection::AtOrAbove
                                             ? cell.temperature >= rule.threshold
                                             : cell.temperature <= rule.threshold;
                    if (!matches) continue;
                    cell.material = rule.result;
                    cell.temperature = std::int16_t(std::clamp<int>(
                        int(cell.temperature) + rule.temperatureDelta, -32768, 32767));
                    cell.lifetime = std::uint8_t(std::min<std::uint16_t>(
                        impl_->catalog.definition(cell.material).defaultLifetime,
                        std::numeric_limits<std::uint8_t>::max()));
                    impl_->put(x, y, cell, tick.value());
                    ++stats.phaseChanges;
                    ++stats.cellsChanged;
                    break;
                }
            }
    }

    if (stats.phaseChanges != 0) rebuildSummaries();

    std::uint64_t worldMaterialMask = 0;
    bool worldMaterialMaskOverflow = false;
    for (const auto& [coord, summary] : phaseSummaries) {
        (void)coord;
        worldMaterialMask |= summary.materialMask;
        worldMaterialMaskOverflow = worldMaterialMaskOverflow || summary.materialMaskOverflow;
    }
    bool worldCanReact = worldMaterialMaskOverflow;
    if (!worldCanReact)
        for (const auto& rule : impl_->catalog.reactions()) {
            const auto first = std::size_t(rule.first), second = std::size_t(rule.second);
            if (first >= 64 || second >= 64 ||
                ((worldMaterialMask & (std::uint64_t(1) << first)) != 0 &&
                 (worldMaterialMask & (std::uint64_t(1) << second)) != 0)) {
                worldCanReact = true;
                break;
            }
        }

    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const ChunkCoord coord = *it;
        auto sourceFound = impl_->chunks.find(coord);
        if (sourceFound == impl_->chunks.end()) continue;
        Chunk* sourceChunk = &sourceFound->second;
        const auto summary = phaseSummaries.find(coord);
        if (summary != phaseSummaries.end() && !summary->second.materialMaskOverflow) {
            bool requiresScan = worldCanReact;
            if (!requiresScan) {
                for (std::size_t material = 0; material < 64; ++material) {
                    if ((summary->second.materialMask & (std::uint64_t(1) << material)) == 0) continue;
                    const auto state = impl_->catalog.definition(MaterialId(material)).state;
                    if (state == MaterialState::Gas || state == MaterialState::Energy) {
                        requiresScan = true;
                        break;
                    }
                }
            }
            if (!requiresScan) continue;
        }
        for (int ly = 0; ly < kPixelChunkSize; ++ly)
            for (int lx = 0; lx < kPixelChunkSize; ++lx) {
                const int x = coord.x * kPixelChunkSize + lx;
                const int y = coord.y * kPixelChunkSize + ly;
                const std::size_t sourceIndex = Chunk::index(lx, ly);
                if (sourceChunk->updated[sourceIndex] == tick.value()) continue;
                PixelCell cell = sourceChunk->cells[sourceIndex];
                if (cell.material == MaterialId::Air) continue;
                ++stats.cellsVisited;
                bool reacted = false;
                if (impl_->catalog.canReact(cell.material))
                    for (const auto& [ox, oy] :
                         std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                        const int neighborLocalX = lx + ox;
                        const int neighborLocalY = ly + oy;
                        const bool sameChunk = neighborLocalX >= 0 &&
                                               neighborLocalX < kPixelChunkSize &&
                                               neighborLocalY >= 0 &&
                                               neighborLocalY < kPixelChunkSize;
                        const std::size_t neighborIndex =
                            sameChunk ? Chunk::index(neighborLocalX, neighborLocalY) : 0;
                        PixelCell neighbor = sameChunk ? sourceChunk->cells[neighborIndex]
                                                       : impl_->get(x + ox, y + oy);
                        for (const std::uint16_t ruleIndex :
                             impl_->reactionRules(cell.material, neighbor.material)) {
                            const auto& rule = impl_->catalog.reactions()[ruleIndex];
                            const bool direct = rule.first == cell.material && rule.second == neighbor.material;
                            const bool reverse = rule.second == cell.material && rule.first == neighbor.material;
                            if ((!direct && !reverse) ||
                                std::max(cell.temperature, neighbor.temperature) < rule.minimumTemperature)
                                continue;
                            cell.material = direct ? rule.firstResult : rule.secondResult;
                            neighbor.material = direct ? rule.secondResult : rule.firstResult;
                            cell.temperature = std::int16_t(std::clamp<int>(int(cell.temperature) + rule.heatDelta,
                                                                          -32768, 32767));
                            neighbor.temperature = std::int16_t(std::clamp<int>(
                                int(neighbor.temperature) + rule.heatDelta, -32768, 32767));
                            if (cell.lifetime == 0)
                                cell.lifetime = std::uint8_t(std::min<std::uint16_t>(
                                    impl_->catalog.definition(cell.material).defaultLifetime,
                                    std::numeric_limits<std::uint8_t>::max()));
                            if (neighbor.lifetime == 0)
                                neighbor.lifetime = std::uint8_t(std::min<std::uint16_t>(
                                    impl_->catalog.definition(neighbor.material).defaultLifetime,
                                    std::numeric_limits<std::uint8_t>::max()));
                            if (sameChunk) {
                                impl_->putWithinChunk(*sourceChunk, neighborIndex, neighbor,
                                                      tick.value());
                            } else {
                                impl_->put(x + ox, y + oy, neighbor, tick.value());
                                // A custom rule may materialize an absent cross-boundary
                                // Air cell and rehash the sparse Chunk map.
                                sourceFound = impl_->chunks.find(coord);
                                if (sourceFound == impl_->chunks.end()) break;
                                sourceChunk = &sourceFound->second;
                            }
                            reacted = true;
                            ++stats.cellsChanged;
                            break;
                        }
                    }
                if (reacted) ++stats.reactions;
                const auto state = impl_->catalog.definition(cell.material).state;
                if (reacted)
                    impl_->putWithinChunk(*sourceChunk, sourceIndex, cell, tick.value());
                if (state != MaterialState::Gas && state != MaterialState::Energy) continue;
                if (cell.lifetime > 0) {
                    --cell.lifetime;
                    ++stats.cellsChanged;
                    if (cell.lifetime == 0) cell.material = MaterialId::Air;
                    impl_->put(x, y, cell, tick.value());
                }
                if (cell.material != MaterialId::Air) {
                    const int direction = impl_->pseudoBit(x, y, tick.value()) ? 1 : -1;
                    bool moved = impl_->moveOrSwap(x, y, x, y - 1, tick.value());
                    if (!moved) moved = impl_->moveOrSwap(x, y, x + direction, y - 1, tick.value());
                    if (moved) {
                        ++stats.cellsMoved;
                        stats.cellsChanged += 2;
                    }
                    sourceFound = impl_->chunks.find(coord);
                    if (sourceFound == impl_->chunks.end()) break;
                    sourceChunk = &sourceFound->second;
                }
            }
    }

    std::vector<ChunkCoord> reclaimed;
    for (auto& [coord, chunk] : impl_->chunks) {
        if (chunk.touched) {
            chunk.idleTicks = 0;
            chunk.active = true;
        } else {
            chunk.idleTicks = std::uint8_t(std::min<int>(kPixelSleepHysteresisTicks, chunk.idleTicks + 1));
            chunk.active = chunk.idleTicks < kPixelSleepHysteresisTicks;
        }
        if (!chunk.active && chunk.nonAir == 0)
            reclaimed.push_back(coord);
    }
    for (const ChunkCoord coord : reclaimed) {
        impl_->chunks.erase(coord);
        impl_->removedChunks[coord] = impl_->revision + 1;
        ++stats.chunksReclaimed;
    }
    if (stats.cellsChanged != 0 || stats.chunksReclaimed != 0) ++impl_->revision;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - stepStarted)
                             .count();
    pixelWorldControlService().recordStep(impl_->worldId, stats, std::uint64_t(elapsed));
    return eve::Result<StepStats>::success(stats);
}

StepStats PixelWorld::step() {
    auto result = advance(eve::SimulationTick(impl_->tick + 1));
    return std::move(result).expect("PixelWorld::step generated an invalid tick");
}

void PixelWorld::setPaused(bool paused) noexcept { impl_->paused = paused; }
bool PixelWorld::isPaused() const noexcept { return impl_->paused; }

std::uint64_t PixelWorld::seed() const noexcept { return impl_->seed; }
std::uint64_t PixelWorld::revision() const noexcept { return impl_->revision; }
std::uint64_t PixelWorld::tickValue() const noexcept { return impl_->tick; }
std::uint64_t PixelWorld::lastEditSequence() const noexcept { return impl_->lastEditSequence; }
int PixelWorld::chunkCount() const noexcept { return int(impl_->chunks.size()); }
int PixelWorld::activeChunkCount() const noexcept {
    return int(std::count_if(impl_->chunks.begin(), impl_->chunks.end(), [](const auto& entry) {
        return entry.second.active;
    }));
}
std::uint64_t PixelWorld::materialCatalogFingerprint() const noexcept { return impl_->catalog.fingerprint(); }

std::vector<PixelChunkSnapshot> PixelWorld::snapshotChangedChunks(std::uint64_t sinceRevision) const {
    std::vector<ChunkCoord> order;
    order.reserve(impl_->chunks.size());
    for (const auto& [coord, chunk] : impl_->chunks)
        if (chunk.revision > sinceRevision) order.push_back(coord);
    for (const auto& [coord, revision] : impl_->removedChunks)
        if (revision > sinceRevision) order.push_back(coord);
    std::sort(order.begin(), order.end());

    std::vector<PixelChunkSnapshot> snapshots;
    snapshots.reserve(order.size());
    for (const ChunkCoord coord : order) {
        PixelChunkSnapshot snapshot;
        snapshot.x = coord.x;
        snapshot.y = coord.y;
        const auto chunk = impl_->chunks.find(coord);
        if (chunk != impl_->chunks.end()) {
            snapshot.revision = chunk->second.revision;
            snapshot.cells.assign(chunk->second.cells.begin(), chunk->second.cells.end());
        } else {
            snapshot.revision = impl_->removedChunks.at(coord);
            snapshot.removed = true;
        }
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

eve::Result<std::vector<PixelChunkSnapshot>> PixelWorld::snapshotChunksInRegion(
    PixelChunkRegion region, std::uint64_t sinceRevision) const {
    if (region.minX > region.maxX || region.minY > region.maxY)
        return eve::Result<std::vector<PixelChunkSnapshot>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Chunk region bounds are inverted", "region", {},
            "pixelworld.chunk-region"));
    const std::uint64_t width = std::uint64_t(std::int64_t(region.maxX) - region.minX) + 1;
    const std::uint64_t height = std::uint64_t(std::int64_t(region.maxY) - region.minY) + 1;
    if (width > 65'536 || height > 65'536 || width * height > 65'536)
        return eve::Result<std::vector<PixelChunkSnapshot>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Chunk region exceeds 65536-coordinate budget", "region", {},
            "pixelworld.chunk-region"));

    std::vector<PixelChunkSnapshot> result;
    for (int y = region.minY;; ++y) {
        for (int x = region.minX;; ++x) {
            const ChunkCoord coord{x, y};
            const auto present = impl_->chunks.find(coord);
            if (present != impl_->chunks.end() && present->second.revision > sinceRevision) {
                PixelChunkSnapshot snapshot;
                snapshot.x = x;
                snapshot.y = y;
                snapshot.revision = present->second.revision;
                snapshot.cells.assign(present->second.cells.begin(), present->second.cells.end());
                result.push_back(std::move(snapshot));
            } else {
                const auto removed = impl_->removedChunks.find(coord);
                if (removed != impl_->removedChunks.end() && removed->second > sinceRevision)
                    result.push_back({x, y, removed->second, true, {}});
            }
            if (x == region.maxX) break;
        }
        if (y == region.maxY) break;
    }
    return eve::Result<std::vector<PixelChunkSnapshot>>::success(std::move(result));
}

eve::Result<std::vector<PixelChunkDiagnostic>> PixelWorld::chunkDiagnostics(
    PixelChunkRegion region) const {
    if (region.minX > region.maxX || region.minY > region.maxY)
        return eve::Result<std::vector<PixelChunkDiagnostic>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Chunk region bounds are inverted", "region", {},
            "pixelworld.chunk-diagnostics"));
    const std::uint64_t width = std::uint64_t(std::int64_t(region.maxX) - region.minX) + 1;
    const std::uint64_t height = std::uint64_t(std::int64_t(region.maxY) - region.minY) + 1;
    if (width > 65'536 || height > 65'536 || width * height > 65'536)
        return eve::Result<std::vector<PixelChunkDiagnostic>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Chunk region exceeds 65536-coordinate budget", "region", {},
            "pixelworld.chunk-diagnostics"));
    std::vector<PixelChunkDiagnostic> result;
    for (int y = region.minY;; ++y) {
        for (int x = region.minX;; ++x) {
            const auto found = impl_->chunks.find({x, y});
            if (found != impl_->chunks.end()) {
                const Chunk& chunk = found->second;
                PixelChunkDiagnostic diagnostic{x, y, chunk.revision, chunk.nonAir, chunk.mobile,
                                                20, 20, chunk.idleTicks, chunk.active};
                if (chunk.nonAir != 0) {
                    diagnostic.minimumTemperature = 32767;
                    diagnostic.maximumTemperature = -32768;
                    for (const PixelCell cell : chunk.cells) {
                        if (cell.material == MaterialId::Air) continue;
                        diagnostic.minimumTemperature = std::min(diagnostic.minimumTemperature,
                                                                 cell.temperature);
                        diagnostic.maximumTemperature = std::max(diagnostic.maximumTemperature,
                                                                 cell.temperature);
                    }
                }
                result.push_back(diagnostic);
            }
            if (x == region.maxX) break;
        }
        if (y == region.maxY) break;
    }
    return eve::Result<std::vector<PixelChunkDiagnostic>>::success(std::move(result));
}

eve::Result<PixelChunkApplyReceipt> PixelWorld::applyChunkBatch(
    const PixelChunkBatch& batch, std::uint64_t expectedRevision) {
    const auto reject = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<PixelChunkApplyReceipt>::failure(eve::Diagnostic::error(
            code, std::move(message), std::move(path), {}, "pixelworld.chunk-batch"));
    };
    if (impl_->revision != expectedRevision)
        return reject(eve::DiagnosticCode::Conflict, "local world revision is stale", "expectedRevision");
    if (batch.catalogFingerprint != impl_->catalog.fingerprint())
        return reject(eve::DiagnosticCode::Conflict, "material Catalog fingerprint does not match",
                      "catalogFingerprint");
    if (batch.sourceSeed != impl_->seed)
        return reject(eve::DiagnosticCode::Conflict, "deterministic world seed does not match", "sourceSeed");
    if (!batch.fullResync &&
        (batch.sourceRevision < impl_->revision || batch.sourceTick.value() < impl_->tick ||
         batch.sourceLastEditSequence < impl_->lastEditSequence))
        return reject(eve::DiagnosticCode::Conflict, "authoritative metadata must not rewind local state",
                      "source");
    if (batch.chunks.size() > 1'000'000U)
        return reject(eve::DiagnosticCode::InvalidArgument, "Chunk batch exceeds admission budget", "chunks");

    std::optional<ChunkCoord> previous;
    for (std::size_t index = 0; index < batch.chunks.size(); ++index) {
        const auto& snapshot = batch.chunks[index];
        const ChunkCoord coord{snapshot.x, snapshot.y};
        if (previous && !(previous.value() < coord))
            return reject(eve::DiagnosticCode::InvalidArgument,
                          "Chunk batch must be unique canonical y/x order", "chunks");
        previous = coord;
        if (snapshot.revision == 0 || snapshot.revision > batch.sourceRevision)
            return reject(eve::DiagnosticCode::InvalidArgument,
                          "Chunk revision must be positive and no newer than source", "chunks.revision");
        if (snapshot.removed) {
            if (!snapshot.cells.empty())
                return reject(eve::DiagnosticCode::InvalidArgument,
                              "removed Chunk must not contain cells", "chunks.cells");
            continue;
        }
        if (snapshot.cells.size() != std::size_t(kPixelChunkSize * kPixelChunkSize))
            return reject(eve::DiagnosticCode::InvalidArgument,
                          "present Chunk must contain exactly 64x64 cells", "chunks.cells");
        for (const PixelCell cell : snapshot.cells) {
            if (std::size_t(cell.material) >= impl_->catalog.definitions().size())
                return reject(eve::DiagnosticCode::InvalidArgument,
                              "Chunk references an unknown material", "chunks.cells.material");
            const auto capacity = impl_->catalog.definition(cell.material).heatCapacity;
            if ((cell.material == MaterialId::Air && cell.thermalRemainder != 0) ||
                cell.thermalRemainder >= capacity)
                return reject(eve::DiagnosticCode::InvalidArgument,
                              "Chunk contains a non-canonical thermal remainder",
                              "chunks.cells.thermalRemainder");
        }
    }

    Impl candidate = batch.fullResync ? Impl(impl_->catalog) : *impl_;
    if (batch.fullResync) {
        candidate.worldId = impl_->worldId;
        candidate.epoch = impl_->epoch;
        candidate.nextFragmentId = impl_->nextFragmentId;
        candidate.paused = impl_->paused;
    }
    PixelChunkApplyReceipt receipt;
    receipt.revisionBefore = impl_->revision;
    for (const auto& snapshot : batch.chunks) {
        const ChunkCoord coord{snapshot.x, snapshot.y};
        if (snapshot.removed) {
            candidate.chunks.erase(coord);
            candidate.removedChunks[coord] = snapshot.revision;
            ++receipt.chunksRemoved;
            continue;
        }
        Chunk chunk;
        std::copy(snapshot.cells.begin(), snapshot.cells.end(), chunk.cells.begin());
        chunk.revision = snapshot.revision;
        chunk.active = true;
        chunk.touched = true;
        chunk.idleTicks = 0;
        candidate.rebuildActivity(chunk);
        candidate.chunks.insert_or_assign(coord, std::move(chunk));
        candidate.removedChunks.erase(coord);
        ++receipt.chunksReplaced;
    }
    candidate.revision = batch.sourceRevision;
    candidate.tick = batch.sourceTick.value();
    candidate.lastEditSequence = batch.sourceLastEditSequence;
    ++candidate.epoch;
    receipt.revisionAfter = candidate.revision;
    receipt.worldEpoch = candidate.epoch;
    *impl_ = std::move(candidate);
    return eve::Result<PixelChunkApplyReceipt>::success(receipt);
}

eve::Result<std::vector<std::byte>> PixelWorld::saveSnapshot() const {
    std::vector<ChunkCoord> order;
    order.reserve(impl_->chunks.size());
    for (const auto& [coord, chunk] : impl_->chunks) {
        (void)chunk;
        order.push_back(coord);
    }
    std::sort(order.begin(), order.end());
    std::vector<std::byte> out;
    out.insert(out.end(), {std::byte{0x45}, std::byte{0x56}, std::byte{0x50}, std::byte{0x57}});
    append<std::uint16_t>(out, 4);
    append(out, impl_->catalog.fingerprint());
    append(out, impl_->seed);
    append(out, impl_->revision);
    append(out, impl_->tick);
    append(out, impl_->lastEditSequence);
    append<std::uint32_t>(out, std::uint32_t(order.size()));
    for (const ChunkCoord coord : order) {
        append<std::int32_t>(out, coord.x);
        append<std::int32_t>(out, coord.y);
        const Chunk& chunk = impl_->chunks.at(coord);
        for (const PixelCell cell : chunk.cells) {
            append<std::uint16_t>(out, std::uint16_t(cell.material));
            append(out, cell.temperature);
            append(out, cell.lifetime);
            append(out, cell.thermalRemainder);
        }
    }
    return eve::Result<std::vector<std::byte>>::success(std::move(out));
}

eve::Result<void> PixelWorld::restoreSnapshot(std::span<const std::byte> bytes) {
    std::size_t cursor = 0;
    if (bytes.size() < 4 || bytes[0] != std::byte{0x45} || bytes[1] != std::byte{0x56} ||
        bytes[2] != std::byte{0x50} || bytes[3] != std::byte{0x57})
        return eve::Result<void>::failure(malformed("missing EVPW header"));
    cursor = 4;
    std::uint16_t version = 0;
    Impl candidate(impl_->catalog);
    std::uint32_t count = 0;
    if (!read(bytes, cursor, version) ||
        (version != 1 && version != 2 && version != 3 && version != 4))
        return eve::Result<void>::failure(malformed("unsupported pixelworld schema version"));
    if (version >= 2) {
        std::uint64_t fingerprint = 0;
        if (!read(bytes, cursor, fingerprint) || fingerprint != impl_->catalog.fingerprint())
            return eve::Result<void>::failure(malformed("material catalog fingerprint mismatch"));
    } else if (impl_->catalog.fingerprint() != MaterialCatalog::builtIn().fingerprint()) {
        return eve::Result<void>::failure(malformed("version-1 snapshots require the built-in material catalog"));
    }
    if (!read(bytes, cursor, candidate.seed) || !read(bytes, cursor, candidate.revision) ||
        !read(bytes, cursor, candidate.tick))
        return eve::Result<void>::failure(malformed("truncated or unreasonable snapshot header"));
    if (version >= 3 && !read(bytes, cursor, candidate.lastEditSequence))
        return eve::Result<void>::failure(malformed("truncated edit sequence"));
    if (!read(bytes, cursor, count) || count > 1'000'000U)
        return eve::Result<void>::failure(malformed("truncated or unreasonable chunk count"));
    for (std::uint32_t i = 0; i < count; ++i) {
        ChunkCoord coord;
        if (!read(bytes, cursor, coord.x) || !read(bytes, cursor, coord.y))
            return eve::Result<void>::failure(malformed("truncated chunk coordinate"));
        Chunk chunk;
        for (PixelCell& cell : chunk.cells) {
            std::uint16_t material = 0;
            if (!read(bytes, cursor, material) || !read(bytes, cursor, cell.temperature) ||
                !read(bytes, cursor, cell.lifetime) || material >= candidate.catalog.definitions().size())
                return eve::Result<void>::failure(malformed("invalid chunk cell payload"));
            cell.material = MaterialId(material);
            if (version >= 4 && !read(bytes, cursor, cell.thermalRemainder))
                return eve::Result<void>::failure(malformed("truncated thermal remainder"));
            const auto capacity = candidate.catalog.definition(cell.material).heatCapacity;
            if ((cell.material == MaterialId::Air && cell.thermalRemainder != 0) ||
                cell.thermalRemainder >= capacity)
                return eve::Result<void>::failure(malformed("non-canonical thermal remainder"));
        }
        const auto [ignored, inserted] = candidate.chunks.emplace(coord, std::move(chunk));
        (void)ignored;
        if (!inserted) return eve::Result<void>::failure(malformed("duplicate chunk coordinate"));
    }
    if (cursor != bytes.size()) return eve::Result<void>::failure(malformed("trailing snapshot bytes"));
    for (auto& [coord, chunk] : candidate.chunks) {
        (void)coord;
        candidate.rebuildActivity(chunk);
        chunk.revision = candidate.revision;
    }
    candidate.worldId = impl_->worldId;
    candidate.epoch = impl_->epoch + 1;
    candidate.nextFragmentId = impl_->nextFragmentId;
    candidate.paused = impl_->paused;
    *impl_ = std::move(candidate);
    return eve::Result<void>::success();
}

}  // namespace eve::pixelworld
