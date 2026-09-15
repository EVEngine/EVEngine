#pragma once
#include <memory>
#include <string>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
struct TerrainStampSettings;
struct TerrainThermalSettings;
struct TerrainWaterSettings;
struct TerrainSedimentSettings;
enum class TerrainWaterChannel;
struct TerrainSmoothSettings;
struct TerrainRidgeSettings;
struct TerrainTerraceSettings;
struct TerrainHeightMixSettings;
/** @brief Edit policy for the owned operation history and terrain. */
enum class TerrainSessionAccess { Editable, Locked };
/** @brief Observable lifecycle of a caller-driven staged session replay. */
enum class TerrainSessionRunStatus { Idle, Pending, Completed, Cancelled, Failed };
/**
 * @brief Owned in-memory terrain operation session; records native stamps, scalar effects and erosion.
 * Owns baseline, current terrain, sediment, water and immutable copies of operation inputs. No borrowed
 * raster, scene object, script callback or GPU resource survives a call. Replays use the
 * same CPU kernels and inputs in order; determinism is same-build floating arithmetic.
 * All access is serialized by the caller on its owning thread. History mutations and
 * terrain, sediment and all water channels publish together after successful evaluation; allocation failure preserves
 * all state. Snapshots are copies, never a second writable view of the authoritative terrain.
 * Versioned snapshots retain the baseline, every owned command, enabled flags, cursor and access policy.
 * Asynchronous worker tickets and a generic gameplay object model are not introduced.
 */
class TerrainGenerationSession {
public:
    /** @brief Construct an empty editable session. */
    TerrainGenerationSession();
    /** @brief Destroy owned terrain and command snapshots. */
    ~TerrainGenerationSession();
    /** @brief Transfer state; moved-from session is empty and resettable. */
    TerrainGenerationSession(TerrainGenerationSession&&) noexcept;
    /** @brief Transfer state and release previously owned snapshots. */
    TerrainGenerationSession& operator=(TerrainGenerationSession&&) noexcept;
    /** @brief One mutable owner; implicit copies are prohibited. */
    TerrainGenerationSession(const TerrainGenerationSession&) = delete;
    /** @brief One mutable owner; implicit copies are prohibited. */
    TerrainGenerationSession& operator=(const TerrainGenerationSession&) = delete;
    /** @brief Copy a finite baseline, zero sediment/water and clear history atomically; locked sessions reject reset.
     * @return Baseline sample count or InvalidArgument. */
    [[nodiscard]] Result<int> reset(const Heightmap& baseline);
    /** @brief Append an owned stamp command, truncating redo only on success; locked sessions reject it.
     * @param stamp Borrowed stamp raster, copied into history.
     * @param settings Value settings captured for replay.
     * @param localMask Borrowed local mask, copied into history.
     * @param globalMask Borrowed global mask, copied into history.
     * @return Changed terrain sample count or InvalidArgument; failed commands are never recorded. */
    [[nodiscard]] Result<int> stamp(const Heightmap& stamp, const TerrainStampSettings& settings,
                                    const Heightmap& localMask, const Heightmap& globalMask);
    /** @brief Record nine-tap contrast; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> contrast(const Heightmap& mask, float strength, float featureSize);
    /** @brief Record two-pass smoothing; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> smooth(const Heightmap& mask, const TerrainSmoothSettings& settings);
    /** @brief Record ridge iteration; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> ridges(const Heightmap& mask, const TerrainRidgeSettings& settings);
    /** @brief Record terracing; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> terrace(const Heightmap& mask, const TerrainTerraceSettings& settings);
    /** @brief Record power transform; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> power(const Heightmap& mask, float power);
    /** @brief Record height-curve transform; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> heightCurve(const Heightmap& mask, const Heightmap& curve, float minimum, float maximum);
    /** @brief Record height mixing; copies all borrowed inputs and values.
     * @return Changed terrain sample count; invalid inputs/locked session leave terrain and history unchanged. */
    [[nodiscard]] Result<int> heightMix(const Heightmap& local, const Heightmap& global,
                                        const TerrainHeightMixSettings& settings);
    /** @brief Record a simulation reset: zero sediment, flux and velocity; set uniform nonnegative water depth.
     * Terrain is unchanged. Use before hydraulic to start a fresh erosion operation; otherwise erosion continues
     * the preceding simulation state. Reset is undoable and rejects nonfinite/negative depth atomically.
     * @return Changed terrain sample count (zero), or InvalidArgument; locked sessions reject it. */
    [[nodiscard]] Result<int> resetSimulation(float depth = 0);
    /** @brief Record thermal erosion using accumulated sediment; water is preserved. Settings are copied.
     * @return Changed terrain sample count or InvalidArgument; all generated state and history publish atomically. */
    [[nodiscard]] Result<int> thermal(const TerrainThermalSettings& settings);
    /** @brief Record complete water/sediment/thermal iterations using preceding simulation state.
     * Settings and explicit iteration count are copied; no implicit reset, clock or UI unit conversion.
     * @return Changed terrain sample count or InvalidArgument; failure preserves every channel and history. */
    [[nodiscard]] Result<int> hydraulic(const TerrainWaterSettings& water, const TerrainSedimentSettings& sediment,
                                        const TerrainThermalSettings& thermal, int iterations);
    /** @brief Copy signed current sediment into a matching finite output, including while locked.
     * @return Changed output sample count or InvalidArgument; failure preserves output. */
    [[nodiscard]] Result<int> copySediment(Heightmap& output) const;
    /** @brief Copy a raw water channel into a matching finite output, including while locked.
     * @return Changed output sample count or InvalidArgument; no mutable simulation view is exposed. */
    [[nodiscard]] Result<int> exportWater(Heightmap& output, TerrainWaterChannel channel) const;
    /** @brief Re-evaluate the applied history prefix from baseline atomically; locked sessions reject it.
     * @return Changed terrain sample count or InvalidArgument. */
    [[nodiscard]] Result<int> replay();
    /** @brief Start replaying the applied history into a private candidate without changing current terrain. */
    [[nodiscard]] Result<TerrainSessionRunStatus> beginReplay();
    /** @brief Execute at most maxOperations commands and publish once when the candidate completes. */
    [[nodiscard]] Result<TerrainSessionRunStatus> stepReplay(int maxOperations);
    /** @brief Cancel a pending replay and discard its private candidate without changing current terrain. */
    [[nodiscard]] Result<TerrainSessionRunStatus> cancelReplay();
    /** @brief Return the staged replay lifecycle status. */
    [[nodiscard]] TerrainSessionRunStatus getReplayStatus() const noexcept;
    /** @brief Return commands visited by the current or most recent staged replay. */
    [[nodiscard]] int getReplayCompletedOperations() const noexcept;
    /** @brief Move the applied prefix back one command and replay; no history returns InvalidArgument.
     * @return Changed terrain sample count, atomically; locked sessions reject it. */
    [[nodiscard]] Result<int> undo();
    /** @brief Move the applied prefix forward one command and replay; no redo returns InvalidArgument.
     * @return Changed terrain sample count, atomically; locked sessions reject it. */
    [[nodiscard]] Result<int> redo();
    /** @brief Toggle a command and replay the applied prefix atomically; index is history-local, not a stable ID.
     * @return Changed terrain sample count or InvalidArgument; locked sessions reject it. */
    [[nodiscard]] Result<int> setOperationEnabled(int index, bool enabled);
    /** @brief Read a command's enabled flag, or InvalidArgument for an out-of-range history index. */
    [[nodiscard]] Result<bool> operationEnabled(int index) const;
    /** @brief Copy current terrain into an existing matching finite output; allowed while locked.
     * @return Changed output sample count or InvalidArgument; failure leaves output unchanged. */
    [[nodiscard]] Result<int> copyTerrain(Heightmap& output) const;
    /** @brief Change only the edit policy; unlocking is always permitted, invalid enum values are rejected. */
    [[nodiscard]] Result<void> setAccess(TerrainSessionAccess access);
    /** @brief Read current edit policy; moved-from sessions are editable. */
    TerrainSessionAccess getAccess() const noexcept;
    /** @brief Count all recorded commands, including the redo suffix. */
    int getOperationCount() const noexcept;
    /** @brief Count commands in the applied prefix, including disabled commands. */
    int getAppliedCount() const noexcept;
    /** @brief Serialize schema version 1 with the complete replayable operation history. */
    [[nodiscard]] Result<std::string> snapshotJson() const;
    /** @brief Atomically restore schema version 1 and rebuild the applied prefix before publication. */
    [[nodiscard]] Result<void> restoreJson(const std::string& json);

private:
    struct Command;
    struct Impl;
    [[nodiscard]] Result<int> append(Command command);
    std::unique_ptr<Impl>     impl_;
    [[nodiscard]] Result<int> moveCursor(int delta);
};
}  // namespace eve::procgen
