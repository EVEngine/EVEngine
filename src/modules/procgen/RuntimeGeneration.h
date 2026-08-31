#pragma once

#include "procgen/PointDelta.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** @brief One immutable runtime-generation cell request returned to script. */
class ProcgenCellRequest {
public:
    int      getLevel() const;
    int      getX() const;
    int      getZ() const;
    uint32_t getSeed() const;
    /** @brief Unique scheduler ticket used to reject stale asynchronous completions. */
    uint64_t getTicket() const;
    float    getMinX() const;
    float    getMinZ() const;
    float    getMaxX() const;
    float    getMaxZ() const;

private:
    friend class RuntimeGeneration;
    int      level_    = 0;
    int      x_        = 0;
    int      z_        = 0;
    uint32_t seed_     = 1;
    uint64_t ticket_   = 0;
    float    cellSize_ = 1.f;
};

/**
 * @brief View-driven partition and hierarchical procedural generation scheduler.
 *
 * updateSource computes desired cells for every configured level. nextGenerate
 * and nextCleanup provide bounded work queues; completeGeneration atomically
 * publishes a cell output. The scheduler owns no scene or graphics objects.
 */
class RuntimeGeneration {
public:
    /** @brief Create a scheduler with a stable world seed. */
    explicit RuntimeGeneration(uint32_t worldSeed = 1);

    /** @brief Remove levels, cells, queues and outputs while retaining the world seed. */
    void clear();
    /**
     * @brief Add a hierarchical grid level.
     * @param cellSize World-space XZ cell size.
     * @param generationRadius Distance around the source that requests generation.
     * @param cleanupMultiplier Cleanup radius divided by generation radius; must be at least 1.
     * @return Level index, or -1 for invalid settings.
     */
    int addLevel(float cellSize, float generationRadius, float cleanupMultiplier);
    int getLevelCount() const;
    float getLevelCellSize(int level) const;
    float getLevelGenerationRadius(int level) const;
    float getLevelCleanupRadius(int level) const;

    /** @brief Weight favoring cells in front of the source, clamped to [0,1]. */
    void  setDirectionWeight(float weight);
    float getDirectionWeight() const;
    /** @brief Maximum simultaneously issued generation requests. */
    void setMaxGenerating(int count);
    int  getMaxGenerating() const;
    /** @brief Maximum active plus in-flight cells; zero disables the resident-cell limit. */
    void setMaxActiveCells(int count);
    /** @brief Return the resident-cell limit, or zero when unlimited. */
    int getMaxActiveCells() const;
    /** @brief Set the maximum points allowed in one generated cell; zero disables the limit. */
    void setMaxPointsPerCell(int count);
    /** @brief Return the per-cell point limit, or zero when unlimited. */
    int getMaxPointsPerCell() const;
    /** @brief Set the total point limit across active cell outputs; zero disables the limit. */
    void setMaxResidentPoints(int count);
    /** @brief Return the total resident-point limit, or zero when unlimited. */
    int getMaxResidentPoints() const;
    /** @brief Return points retained by active or not-yet-acknowledged cleanup cells. */
    int getResidentPointCount() const;
    /** @brief Return generation or restore outputs rejected by point budgets. */
    int getRejectedOutputCount() const;
    /**
     * @brief Schedule lowest-priority active cells for cleanup until at most target points remain.
     * @return Number of cells newly scheduled for cleanup.
     */
    int trimToResidentPoints(int target);
    /** @brief Set retries after the initial generation attempt; zero fails immediately. */
    void setMaxGenerationRetries(int count);
    /** @brief Return retries allowed after the initial generation attempt. */
    int getMaxGenerationRetries() const;
    /** @brief CPU issue budget in milliseconds for one frame; zero disables the limit. */
    void  setFrameTimeBudget(float milliseconds);
    float getFrameTimeBudget() const;
    /** @brief Start a new budget window before consuming generation requests. */
    void beginFrame();

    /**
     * @brief Recompute desired cells for a generation source.
     * @param x,z World position.
     * @param directionX,directionZ Horizontal view direction; zero disables direction priority.
     */
    void updateSource(float x, float z, float directionX, float directionZ);
    /** @brief Add or update a named generation source, then rebuild desired cells. */
    bool setGenerationSource(const std::string& id, float x, float z, float directionX,
                             float directionZ, float radiusScale = 1.f);
    bool removeGenerationSource(const std::string& id);
    void clearGenerationSources();
    int  getGenerationSourceCount() const;
    std::string getGenerationSourceId(int index) const;
    /** @brief Re-evaluate generation/cleanup queues from every registered source. */
    void refreshGenerationSources();

    /**
     * @brief Enable view-cone scheduling.
     * @param halfAngleDegrees Horizontal cone half angle in [1,180].
     * @param behindRadius Cells nearer than this distance generate even outside the cone.
     */
    void setFrustumCulling(bool enabled, float halfAngleDegrees, float behindRadius);
    bool  isFrustumCullingEnabled() const;
    float getFrustumHalfAngle() const;
    float getFrustumBehindRadius() const;

    int                 getPendingGenerateCount() const;
    int                 getGeneratingCount() const;
    int                 getActiveCellCount() const;
    int                 getPendingCleanupCount() const;
    /** @brief Return issued generation requests invalidated before completion. */
    int                 getCancelledGenerationCount() const;
    /** @brief Return cells stopped after exhausting the generation retry policy. */
    int                 getFailedCellCount() const;
    /** @brief Reset and requeue all terminally failed cells still tracked by the scheduler. */
    int                 retryFailedCells();
    ProcgenCellRequest* nextGenerate();
    ProcgenCellRequest* nextCleanup();
    /**
     * @brief Test whether an issued generation or cleanup request still owns its scheduler ticket.
     * @param request Borrowed immutable request; null is never current.
     * @return True while the matching cell, state and ticket still accept work for this request.
     * @thread Call on the scheduler-owning thread between cooperative generation stages.
     */
    bool isRequestCurrent(const ProcgenCellRequest* request) const;
    /** @brief Publish generated points for an issued request. */
    bool completeGeneration(ProcgenCellRequest* request, PointSet* output);
    /** @brief Return an issued request to the pending queue after failure or cancellation. */
    bool failGeneration(ProcgenCellRequest* request);
    /** @brief Acknowledge cleanup after consumers have removed spawned content. */
    bool completeCleanup(ProcgenCellRequest* request);
    /**
     * @brief Atomically acknowledge several cleanup tickets owned by this scheduler.
     * @param requests
     * Non-empty unique cleanup requests.
     * @return Number of erased cells, or a structured failure with no cell
     * changed.
     * @ownership Request references are borrowed only for this call and are not retained.
     *
     * @thread Call synchronously on the scheduler-owning thread.
     * @reentrant Not reentrant for this
     * RuntimeGeneration.
     */
    [[nodiscard]] Result<uint64_t> completeCleanupsAtomic(const std::vector<const ProcgenCellRequest*>& requests);

    bool      hasCell(int level, int x, int z) const;
    PointSet* getCellOutput(int level, int x, int z) const;
    uint64_t  getCellRevision(int level, int x, int z) const;
    /**
     * @brief Atomically replace an active cell through an identity-based delta.
     * @param expectedRevision Exact active revision observed by the caller.
     * @param output Complete desired snapshot whose points have unique non-zero ids.
     * @return The committed revision, or a structured stale/schema/budget failure.
     */
    [[nodiscard]] Result<uint64_t> applyCellUpdate(int level, int x, int z, uint64_t expectedRevision,
                                                   const PointSet& output);
    /**
     * @brief Assign deterministic identities to a legacy active cell atomically.
     * @return The committed revision; no revision is consumed when validation fails.
     */
    [[nodiscard]] Result<uint64_t> migrateCellPointIds(int level, int x, int z, uint64_t expectedRevision);
    /**
     * @brief Return a caller-owned copy of the latest committed delta, or null when unavailable.
     * @ownership Owned; the caller must delete the returned delta. Script bindings transfer ownership to the VM.
     * @return A heap object owned by the caller; script bindings transfer ownership to the VM.
     */
    PointDelta* getCellDelta(int level, int x, int z) const;
    /** @brief Serialize one active cell cache entry in a deterministic versioned format. */
    std::string serializeCell(int level, int x, int z) const;
    /** @brief Atomically restore one cell produced by serializeCell for this world seed. */
    bool deserializeCell(const std::string& definition);
    std::string debugReport() const;

private:
    friend class Procgen;

    struct Level {
        float cellSize        = 1.f;
        float generationRadius = 1.f;
        float cleanupRadius    = 1.f;
    };
    enum class State { Pending, Generating, Active, Cleanup, Failed };
    struct CellKey {
        int level = 0;
        int x     = 0;
        int z     = 0;
        bool operator==(const CellKey& other) const {
            return level == other.level && x == other.x && z == other.z;
        }
    };
    struct CellKeyHash {
        size_t operator()(const CellKey& key) const;
    };
    struct Cell {
        State    state    = State::Pending;
        float    priority = 0.f;
        uint64_t revision = 0;
        uint64_t ticket   = 0;
        int      failures = 0;
        bool     trimmed  = false;
        PointSet output;
        PointDelta delta;
        bool       hasDelta = false;
    };
    struct Source {
        float x           = 0.f;
        float z           = 0.f;
        float directionX  = 0.f;
        float directionZ  = 0.f;
        float radiusScale = 1.f;
    };

    ProcgenCellRequest* makeRequest(const CellKey& key) const;
    [[nodiscard]] Result<void> validateCleanups(const std::vector<const ProcgenCellRequest*>& requests) const;
    void                       eraseValidatedCleanups(const std::vector<const ProcgenCellRequest*>& requests);
    uint32_t            cellSeed(const CellKey& key) const;
    void                sortQueues();

    uint32_t worldSeed_       = 1;
    float    directionWeight_ = 0.25f;
    int      maxGenerating_   = 4;
    int      maxActiveCells_  = 0;
    int      maxPointsPerCell_      = 0;
    int      maxResidentPoints_     = 0;
    int      rejectedOutputCount_   = 0;
    int      cancelledGenerationCount_ = 0;
    int      maxGenerationRetries_ = 3;
    float    frameTimeBudgetMs_ = 0.f;
    uint64_t frameStartedNs_    = 0;
    bool     frustumCulling_    = false;
    float    frustumHalfAngle_  = 60.f;
    float    frustumBehindRadius_ = 0.f;
    std::vector<Level> levels_;
    std::unordered_map<std::string, Source> sources_;
    std::vector<std::string> sourceOrder_;
    std::unordered_map<CellKey, Cell, CellKeyHash> cells_;
    std::vector<CellKey> generateQueue_;
    std::vector<CellKey> cleanupQueue_;
    uint64_t             nextTicket_ = 0;
};

}  // namespace eve::procgen
