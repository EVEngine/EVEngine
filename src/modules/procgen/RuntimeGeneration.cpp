#include "procgen/RuntimeGeneration.h"

#include "procgen/PointSet.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace eve::procgen {
namespace {

float distanceToCell(float x, float z, int cellX, int cellZ, float size) {
    const float centerX = (float(cellX) + 0.5f) * size;
    const float centerZ = (float(cellZ) + 0.5f) * size;
    const float dx      = centerX - x;
    const float dz      = centerZ - z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

int      ProcgenCellRequest::getLevel() const { return level_; }
int      ProcgenCellRequest::getX() const { return x_; }
int      ProcgenCellRequest::getZ() const { return z_; }
uint32_t ProcgenCellRequest::getSeed() const { return seed_; }
uint64_t ProcgenCellRequest::getTicket() const { return ticket_; }
float    ProcgenCellRequest::getMinX() const { return float(x_) * cellSize_; }
float    ProcgenCellRequest::getMinZ() const { return float(z_) * cellSize_; }
float    ProcgenCellRequest::getMaxX() const { return float(x_ + 1) * cellSize_; }
float    ProcgenCellRequest::getMaxZ() const { return float(z_ + 1) * cellSize_; }

RuntimeGeneration::RuntimeGeneration(uint32_t worldSeed) : worldSeed_(worldSeed ? worldSeed : 1u) {}

void RuntimeGeneration::clear() {
    levels_.clear();
    cells_.clear();
    generateQueue_.clear();
    cleanupQueue_.clear();
    sources_.clear();
    sourceOrder_.clear();
    rejectedOutputCount_ = 0;
}

int RuntimeGeneration::addLevel(float cellSize, float generationRadius, float cleanupMultiplier) {
    if (cellSize <= 0.f || generationRadius < 0.f || cleanupMultiplier < 1.f) return -1;
    levels_.push_back({cellSize, generationRadius, generationRadius * cleanupMultiplier});
    return int(levels_.size()) - 1;
}

int RuntimeGeneration::getLevelCount() const { return int(levels_.size()); }
float RuntimeGeneration::getLevelCellSize(int level) const {
    return level >= 0 && level < int(levels_.size()) ? levels_[size_t(level)].cellSize : 0.f;
}
float RuntimeGeneration::getLevelGenerationRadius(int level) const {
    return level >= 0 && level < int(levels_.size())
               ? levels_[size_t(level)].generationRadius
               : 0.f;
}
float RuntimeGeneration::getLevelCleanupRadius(int level) const {
    return level >= 0 && level < int(levels_.size()) ? levels_[size_t(level)].cleanupRadius : 0.f;
}

void RuntimeGeneration::setDirectionWeight(float weight) {
    directionWeight_ = std::clamp(weight, 0.f, 1.f);
}
float RuntimeGeneration::getDirectionWeight() const { return directionWeight_; }
void  RuntimeGeneration::setMaxGenerating(int count) { maxGenerating_ = std::max(1, count); }
int   RuntimeGeneration::getMaxGenerating() const { return maxGenerating_; }
void RuntimeGeneration::setMaxActiveCells(int count) { maxActiveCells_ = std::max(0, count); }
int  RuntimeGeneration::getMaxActiveCells() const { return maxActiveCells_; }
void RuntimeGeneration::setMaxPointsPerCell(int count) { maxPointsPerCell_ = std::max(0, count); }
int  RuntimeGeneration::getMaxPointsPerCell() const { return maxPointsPerCell_; }
void RuntimeGeneration::setMaxResidentPoints(int count) {
    maxResidentPoints_ = std::max(0, count);
}
int RuntimeGeneration::getMaxResidentPoints() const { return maxResidentPoints_; }
int RuntimeGeneration::getResidentPointCount() const {
    uint64_t count = 0;
    for (const auto& [key, cell] : cells_)
        count += uint64_t(cell.output.getCount());
    return int(std::min(count, uint64_t(std::numeric_limits<int>::max())));
}
int RuntimeGeneration::getRejectedOutputCount() const { return rejectedOutputCount_; }
int RuntimeGeneration::trimToResidentPoints(int target) {
    target = std::max(0, target);
    uint64_t projectedResident = 0;
    for (const auto& [key, cell] : cells_)
        if (cell.state != State::Cleanup)
            projectedResident += uint64_t(cell.output.getCount());
    if (projectedResident <= uint64_t(target)) return 0;
    std::vector<CellKey> candidates;
    for (const auto& [key, cell] : cells_)
        if (cell.state == State::Active) candidates.push_back(key);
    std::sort(candidates.begin(), candidates.end(), [this](const CellKey& a, const CellKey& b) {
        const float aPriority = cells_.at(a).priority;
        const float bPriority = cells_.at(b).priority;
        if (aPriority != bPriority) return aPriority > bPriority;
        if (a.level != b.level) return a.level > b.level;
        if (a.z != b.z) return a.z > b.z;
        return a.x > b.x;
    });
    int scheduled = 0;
    for (const auto& key : candidates) {
        if (projectedResident <= uint64_t(target)) break;
        auto& cell = cells_.at(key);
        projectedResident -= uint64_t(cell.output.getCount());
        cell.state = State::Cleanup;
        cell.trimmed = true;
        cell.ticket = ++nextTicket_;
        cleanupQueue_.push_back(key);
        ++scheduled;
    }
    sortQueues();
    return scheduled;
}
void RuntimeGeneration::setMaxGenerationRetries(int count) {
    maxGenerationRetries_ = std::max(0, count);
}
int RuntimeGeneration::getMaxGenerationRetries() const { return maxGenerationRetries_; }
void RuntimeGeneration::setFrameTimeBudget(float milliseconds) {
    frameTimeBudgetMs_ = std::max(0.f, milliseconds);
}
float RuntimeGeneration::getFrameTimeBudget() const { return frameTimeBudgetMs_; }
void RuntimeGeneration::beginFrame() {
    frameStartedNs_ = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

void RuntimeGeneration::updateSource(float x, float z, float directionX, float directionZ) {
    setGenerationSource("primary", x, z, directionX, directionZ, 1.f);
}

bool RuntimeGeneration::setGenerationSource(const std::string& id, float x, float z,
                                             float directionX, float directionZ,
                                             float radiusScale) {
    if (id.empty() || radiusScale <= 0.f) return false;
    const float directionLength = std::sqrt(directionX * directionX + directionZ * directionZ);
    Source source;
    source.x           = x;
    source.z           = z;
    source.directionX  = directionLength > 0.f ? directionX / directionLength : 0.f;
    source.directionZ  = directionLength > 0.f ? directionZ / directionLength : 0.f;
    source.radiusScale = radiusScale;
    if (sources_.find(id) == sources_.end()) sourceOrder_.push_back(id);
    sources_[id] = source;
    refreshGenerationSources();
    return true;
}

bool RuntimeGeneration::removeGenerationSource(const std::string& id) {
    if (sources_.erase(id) == 0) return false;
    sourceOrder_.erase(std::remove(sourceOrder_.begin(), sourceOrder_.end(), id),
                       sourceOrder_.end());
    refreshGenerationSources();
    return true;
}

void RuntimeGeneration::clearGenerationSources() {
    sources_.clear();
    sourceOrder_.clear();
    refreshGenerationSources();
}

int RuntimeGeneration::getGenerationSourceCount() const { return int(sourceOrder_.size()); }
std::string RuntimeGeneration::getGenerationSourceId(int index) const {
    return index >= 0 && index < int(sourceOrder_.size()) ? sourceOrder_[size_t(index)]
                                                          : std::string();
}

void RuntimeGeneration::setFrustumCulling(bool enabled, float halfAngleDegrees,
                                          float behindRadius) {
    frustumCulling_      = enabled;
    frustumHalfAngle_    = std::clamp(halfAngleDegrees, 1.f, 180.f);
    frustumBehindRadius_ = std::max(0.f, behindRadius);
    refreshGenerationSources();
}
bool  RuntimeGeneration::isFrustumCullingEnabled() const { return frustumCulling_; }
float RuntimeGeneration::getFrustumHalfAngle() const { return frustumHalfAngle_; }
float RuntimeGeneration::getFrustumBehindRadius() const { return frustumBehindRadius_; }

void RuntimeGeneration::refreshGenerationSources() {
    constexpr float degreesToRadians = 0.017453292519943295f;
    const float     coneCosine = std::cos(frustumHalfAngle_ * degreesToRadians);
    std::unordered_set<CellKey, CellKeyHash> desired;
    for (auto& [key, cell] : cells_)
        if (cell.state != State::Cleanup) cell.priority = std::numeric_limits<float>::max();

    for (const auto& sourceId : sourceOrder_) {
        const auto& source = sources_.at(sourceId);
        for (size_t levelIndex = 0; levelIndex < levels_.size(); ++levelIndex) {
            const auto& level = levels_[levelIndex];
            const float generationRadius = level.generationRadius * source.radiusScale;
            const int minX = int(std::floor((source.x - generationRadius) / level.cellSize));
            const int maxX = int(std::floor((source.x + generationRadius) / level.cellSize));
            const int minZ = int(std::floor((source.z - generationRadius) / level.cellSize));
            const int maxZ = int(std::floor((source.z + generationRadius) / level.cellSize));
            for (int cellZ = minZ; cellZ <= maxZ; ++cellZ) {
                for (int cellX = minX; cellX <= maxX; ++cellX) {
                    const float centerX  = (float(cellX) + 0.5f) * level.cellSize;
                    const float centerZ  = (float(cellZ) + 0.5f) * level.cellSize;
                    const float dx       = centerX - source.x;
                    const float dz       = centerZ - source.z;
                    const float distance = std::sqrt(dx * dx + dz * dz);
                    if (distance > generationRadius) continue;
                    const bool hasDirection =
                        source.directionX != 0.f || source.directionZ != 0.f;
                    const float forward = distance > 0.f && hasDirection
                                              ? (dx * source.directionX + dz * source.directionZ) /
                                                    distance
                                              : 1.f;
                    if (frustumCulling_ && hasDirection && distance > frustumBehindRadius_ &&
                        forward < coneCosine)
                        continue;
                    const CellKey key{int(levelIndex), cellX, cellZ};
                    desired.insert(key);
                    const float priority = distance / std::max(generationRadius, 0.0001f) -
                                           directionWeight_ * forward;
                    const auto existing = cells_.find(key);
                    if (existing != cells_.end()) {
                        if (existing->second.state == State::Cleanup) {
                            if (existing->second.trimmed) continue;
                            existing->second.ticket = ++nextTicket_;
                            existing->second.state = existing->second.revision > 0 ? State::Active
                                                                                  : State::Pending;
                            existing->second.trimmed = false;
                            cleanupQueue_.erase(
                                std::remove(cleanupQueue_.begin(), cleanupQueue_.end(), key),
                                cleanupQueue_.end());
                            if (existing->second.state == State::Pending)
                                generateQueue_.push_back(key);
                        }
                        existing->second.priority =
                            std::min(existing->second.priority, priority);
                        continue;
                    }
                    Cell cell;
                    cell.priority = priority;
                    cells_.emplace(key, cell);
                    generateQueue_.push_back(key);
                }
            }
        }
    }

    for (auto& [key, cell] : cells_) {
        if (cell.state == State::Cleanup) continue;
        if ((cell.state == State::Pending || cell.state == State::Generating) &&
            desired.find(key) == desired.end()) {
            cell.state = State::Cleanup;
            cell.trimmed = false;
            cell.ticket = ++nextTicket_;
            cleanupQueue_.push_back(key);
            continue;
        }
        const auto& level = levels_[size_t(key.level)];
        bool retained = false;
        for (const auto& sourceId : sourceOrder_) {
            const auto& source = sources_.at(sourceId);
            if (distanceToCell(source.x, source.z, key.x, key.z, level.cellSize) <=
                level.cleanupRadius * source.radiusScale) {
                retained = true;
                break;
            }
        }
        if (retained) continue;
        cell.state = State::Cleanup;
        cell.trimmed = false;
        cell.ticket = ++nextTicket_;
        cleanupQueue_.push_back(key);
    }
    generateQueue_.erase(
        std::remove_if(generateQueue_.begin(), generateQueue_.end(), [this](const CellKey& key) {
            const auto found = cells_.find(key);
            return found == cells_.end() || found->second.state != State::Pending;
        }),
        generateQueue_.end());
    sortQueues();
}

void RuntimeGeneration::sortQueues() {
    const auto priorityLess = [this](const CellKey& a, const CellKey& b) {
        const float aPriority = cells_.at(a).priority;
        const float bPriority = cells_.at(b).priority;
        if (aPriority != bPriority) return aPriority < bPriority;
        if (a.level != b.level) return a.level < b.level;
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    };
    std::sort(generateQueue_.begin(), generateQueue_.end(), priorityLess);
    std::sort(cleanupQueue_.begin(), cleanupQueue_.end(), [](const CellKey& a, const CellKey& b) {
        if (a.level != b.level) return a.level < b.level;
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    });
}

int RuntimeGeneration::getPendingGenerateCount() const { return int(generateQueue_.size()); }
int RuntimeGeneration::getGeneratingCount() const {
    return int(std::count_if(cells_.begin(), cells_.end(), [](const auto& entry) {
        return entry.second.state == State::Generating;
    }));
}
int RuntimeGeneration::getActiveCellCount() const {
    return int(std::count_if(cells_.begin(), cells_.end(), [](const auto& entry) {
        return entry.second.state == State::Active;
    }));
}
int RuntimeGeneration::getPendingCleanupCount() const { return int(cleanupQueue_.size()); }
int RuntimeGeneration::getFailedCellCount() const {
    return int(std::count_if(cells_.begin(), cells_.end(), [](const auto& entry) {
        return entry.second.state == State::Failed;
    }));
}

int RuntimeGeneration::retryFailedCells() {
    int count = 0;
    for (auto& [key, cell] : cells_) {
        if (cell.state != State::Failed) continue;
        cell.state = State::Pending;
        cell.failures = 0;
        cell.ticket = ++nextTicket_;
        generateQueue_.push_back(key);
        ++count;
    }
    sortQueues();
    return count;
}

ProcgenCellRequest* RuntimeGeneration::nextGenerate() {
    if (generateQueue_.empty() || getGeneratingCount() >= maxGenerating_) return nullptr;
    if (maxActiveCells_ > 0 && getActiveCellCount() + getGeneratingCount() >= maxActiveCells_)
        return nullptr;
    if (frameTimeBudgetMs_ > 0.f && frameStartedNs_ != 0) {
        const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
        if (float(now - frameStartedNs_) * 0.000001f >= frameTimeBudgetMs_) return nullptr;
    }
    const CellKey key = generateQueue_.front();
    generateQueue_.erase(generateQueue_.begin());
    const auto found = cells_.find(key);
    if (found == cells_.end() || found->second.state != State::Pending) return nextGenerate();
    found->second.state = State::Generating;
    found->second.ticket = ++nextTicket_;
    return makeRequest(key);
}

ProcgenCellRequest* RuntimeGeneration::nextCleanup() {
    if (cleanupQueue_.empty()) return nullptr;
    const CellKey key = cleanupQueue_.front();
    cleanupQueue_.erase(cleanupQueue_.begin());
    const auto found = cells_.find(key);
    if (found == cells_.end() || found->second.state != State::Cleanup) return nextCleanup();
    found->second.ticket = ++nextTicket_;
    return makeRequest(key);
}

bool RuntimeGeneration::completeGeneration(ProcgenCellRequest* request, PointSet* output) {
    if (!request || !output) return false;
    const CellKey key{request->level_, request->x_, request->z_};
    const auto    found = cells_.find(key);
    if (found == cells_.end() || found->second.state != State::Generating ||
        request->seed_ != cellSeed(key) || request->ticket_ != found->second.ticket)
        return false;
    const int outputPoints = output->getCount();
    if ((maxPointsPerCell_ > 0 && outputPoints > maxPointsPerCell_) ||
        (maxResidentPoints_ > 0 && getResidentPointCount() > maxResidentPoints_ - outputPoints)) {
        ++rejectedOutputCount_;
        if (maxResidentPoints_ > 0 && outputPoints <= maxResidentPoints_)
            trimToResidentPoints(maxResidentPoints_ - outputPoints);
        return false;
    }
    found->second.output = *output;
    found->second.hasDelta = false;
    found->second.failures = 0;
    ++found->second.revision;
    found->second.state = State::Active;
    return true;
}

bool RuntimeGeneration::failGeneration(ProcgenCellRequest* request) {
    if (!request) return false;
    const CellKey key{request->level_, request->x_, request->z_};
    const auto    found = cells_.find(key);
    if (found == cells_.end() || found->second.state != State::Generating ||
        request->ticket_ != found->second.ticket)
        return false;
    ++found->second.failures;
    found->second.ticket = ++nextTicket_;
    if (found->second.failures <= maxGenerationRetries_) {
        found->second.state = State::Pending;
        generateQueue_.push_back(key);
        sortQueues();
    } else {
        found->second.state = State::Failed;
    }
    return true;
}

bool RuntimeGeneration::completeCleanup(ProcgenCellRequest* request) {
    if (!request) return false;
    const CellKey key{request->level_, request->x_, request->z_};
    const auto    found = cells_.find(key);
    if (found == cells_.end() || found->second.state != State::Cleanup ||
        request->ticket_ != found->second.ticket)
        return false;
    cells_.erase(found);
    return true;
}

bool RuntimeGeneration::hasCell(int level, int x, int z) const {
    const auto found = cells_.find({level, x, z});
    return found != cells_.end() && found->second.state == State::Active;
}

PointSet* RuntimeGeneration::getCellOutput(int level, int x, int z) const {
    const auto found = cells_.find({level, x, z});
    return found != cells_.end() && found->second.state == State::Active
               ? new PointSet(found->second.output)
               : nullptr;
}

uint64_t RuntimeGeneration::getCellRevision(int level, int x, int z) const {
    const auto found = cells_.find({level, x, z});
    return found == cells_.end() ? 0 : found->second.revision;
}

Result<uint64_t> RuntimeGeneration::applyCellUpdate(int level, int x, int z, uint64_t expectedRevision,
                                                    const PointSet& output) {
    const auto found = cells_.find({level, x, z});
    if (found == cells_.end() || found->second.state != State::Active)
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "active runtime-generation cell was not found", "cell"));
    if (expectedRevision == 0 || found->second.revision != expectedRevision)
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "runtime-generation cell revision is stale", "revision"));

    auto delta = diffPointSets(found->second.output, output);
    if (!delta.ok()) return Result<uint64_t>::failure(delta.status());
    auto staged = applyPointDelta(found->second.output, delta.value());
    if (!staged.ok()) return Result<uint64_t>::failure(staged.status());

    const int outputPoints = staged.value().getCount();
    const std::int64_t projectedResident = std::int64_t(getResidentPointCount()) - found->second.output.getCount() +
                                           outputPoints;
    if ((maxPointsPerCell_ > 0 && outputPoints > maxPointsPerCell_) ||
        (maxResidentPoints_ > 0 && projectedResident > maxResidentPoints_)) {
        ++rejectedOutputCount_;
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "runtime-generation cell update exceeds the configured point budget", "output"));
    }

    found->second.output   = std::move(staged).takeValue();
    found->second.delta    = std::move(delta).takeValue();
    found->second.hasDelta = true;
    ++found->second.revision;
    return Result<uint64_t>::success(found->second.revision);
}

Result<uint64_t> RuntimeGeneration::migrateCellPointIds(int level, int x, int z, uint64_t expectedRevision) {
    const CellKey key{level, x, z};
    const auto    found = cells_.find(key);
    if (found == cells_.end() || found->second.state != State::Active)
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "active runtime-generation cell was not found", "cell"));
    if (expectedRevision == 0 || found->second.revision != expectedRevision)
        return Result<uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "runtime-generation cell revision is stale", "revision"));

    PointSet staged = found->second.output;
    const std::uint64_t identityNamespace =
        derivePointId((std::uint64_t(worldSeed_) << 32u) | cellSeed(key), found->second.revision);
    auto assigned = staged.assignPointIds(identityNamespace);
    if (!assigned.ok()) return Result<uint64_t>::failure(assigned.status());
    found->second.output   = std::move(staged);
    found->second.hasDelta = false;
    ++found->second.revision;
    return Result<uint64_t>::success(found->second.revision);
}

PointDelta* RuntimeGeneration::getCellDelta(int level, int x, int z) const {
    const auto found = cells_.find({level, x, z});
    return found != cells_.end() && found->second.state == State::Active && found->second.hasDelta
               ? new PointDelta(found->second.delta)
               : nullptr;
}

std::string RuntimeGeneration::debugReport() const {
    std::ostringstream out;
    out << "levels=" << levels_.size() << " cells=" << cells_.size()
        << " pending=" << generateQueue_.size() << " generating=" << getGeneratingCount()
        << " active=" << getActiveCellCount() << " cleanup=" << cleanupQueue_.size()
        << " failed=" << getFailedCellCount() << " maxActive=" << maxActiveCells_
        << " residentPoints=" << getResidentPointCount()
        << " maxResidentPoints=" << maxResidentPoints_
        << " maxPointsPerCell=" << maxPointsPerCell_
        << " rejectedOutputs=" << rejectedOutputCount_;
    return out.str();
}

size_t RuntimeGeneration::CellKeyHash::operator()(const CellKey& key) const {
    size_t hash = size_t(uint32_t(key.level));
    hash ^= size_t(uint32_t(key.x)) * 0x9e3779b1u + (hash << 6u) + (hash >> 2u);
    hash ^= size_t(uint32_t(key.z)) * 0x85ebca77u + (hash << 6u) + (hash >> 2u);
    return hash;
}

ProcgenCellRequest* RuntimeGeneration::makeRequest(const CellKey& key) const {
    auto* request     = new ProcgenCellRequest();
    request->level_   = key.level;
    request->x_       = key.x;
    request->z_       = key.z;
    request->seed_    = cellSeed(key);
    const auto found  = cells_.find(key);
    request->ticket_  = found == cells_.end() ? 0 : found->second.ticket;
    request->cellSize_ = levels_[size_t(key.level)].cellSize;
    return request;
}

uint32_t RuntimeGeneration::cellSeed(const CellKey& key) const {
    return deriveSeed(worldSeed_, "cell:" + std::to_string(key.level) + ":" +
                                      std::to_string(key.x) + ":" + std::to_string(key.z));
}

}  // namespace eve::procgen
