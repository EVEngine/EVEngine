#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::towerdefense {

/** @brief One spawn group inside a wave. */
struct WaveEntry {
    std::string archetypeId;
    int         count           = 1;
    double      intervalSeconds = 0.5;
    double      delaySeconds    = 0.0; /**< Delay before the first unit of this entry. */
};

/** @brief Data-driven wave definition. */
struct WaveDefinition {
    std::string            id;
    double                 prepSeconds = 3.0;
    int                    rewardGold  = 0;
    std::vector<WaveEntry> entries;
};

/** @brief Lifecycle phase of a WaveDirector. */
enum class WavePhase : std::uint8_t {
    Idle,
    Preparing,
    Spawning,
    BetweenWaves,
    Completed,
    Failed,
};

/** @brief One deterministic spawn request emitted by advance(). */
struct WaveSpawnEvent {
    std::uint64_t sequence = 0;
    std::string   waveId;
    int           waveIndex = 0;
    std::string   archetypeId;
    int           entryIndex = 0;
    int           spawnIndex = 0;
};

/** @brief One phase/reward event emitted by advance(). */
struct WaveLifecycleEvent {
    std::uint64_t sequence = 0;
    std::string   kind; /**< waveStarted | waveCompleted | scheduleCompleted | failed */
    std::string   waveId;
    int           waveIndex  = 0;
    int           rewardGold = 0;
    std::string   reason;
};

/** @brief Owning result of one advance step. */
struct WaveAdvanceResult {
    WavePhase                       phase = WavePhase::Idle;
    std::vector<WaveSpawnEvent>     spawns;
    std::vector<WaveLifecycleEvent> events;
};

/**
 * @brief Deterministic wave / creep spawn schedule.
 *
 * Owns wave definitions and schedule progress only. Games place creeps, move them,
 * and apply lives/leak accounting from the returned spawn events.
 */
class EVENGINE_API_FOUNDATION WaveDirector {
public:
    /** @brief Clears definitions and resets schedule state. */
    void clear();
    /** @brief Registers or replaces one wave definition. */
    [[nodiscard]] Result<void> registerWave(WaveDefinition definition);
    /** @brief Loads an array of wave objects from JSON text. */
    [[nodiscard]] Result<int> loadScheduleJson(std::string_view json);
    /** @brief Number of registered waves in schedule order. */
    [[nodiscard]] int waveCount() const noexcept { return static_cast<int>(waves_.size()); }
    /** @brief Current phase name for scripts/HUD. */
    [[nodiscard]] std::string phaseName() const;
    /** @brief Current phase. */
    [[nodiscard]] WavePhase phase() const noexcept { return phase_; }
    /** @brief Index of the active/next wave, or waveCount when completed. */
    [[nodiscard]] int waveIndex() const noexcept { return waveIndex_; }
    /** @brief Remaining preparation/between-wave seconds. */
    [[nodiscard]] double remainingPrepSeconds() const noexcept { return remainingPrep_; }
    /** @brief Starts the schedule from the first wave (Preparing). */
    [[nodiscard]] Result<void> start();
    /** @brief Resets progress without clearing definitions. */
    [[nodiscard]] Result<void> reset();
    /**
     * @brief Advances the schedule by one injected simulation step.
     * @param tick Strictly increasing simulation tick.
     * @param dt Non-negative seconds; wall clock is forbidden.
     */
    [[nodiscard]] Result<WaveAdvanceResult> advance(std::uint64_t tick, double dt);

private:
    [[nodiscard]] Result<void> beginWave(int index, WaveAdvanceResult& out);
    void emitLifecycle(WaveAdvanceResult& out, std::string kind, std::string waveId, int waveIndex, int rewardGold,
                       std::string reason);

    std::vector<WaveDefinition> waves_;
    WavePhase                   phase_          = WavePhase::Idle;
    int                         waveIndex_      = 0;
    int                         entryIndex_     = 0;
    int                         spawnedInEntry_ = 0;
    double                      remainingPrep_  = 0.0;
    double                      spawnCooldown_  = 0.0;
    std::uint64_t               lastTick_       = 0;
    bool                        hasTick_        = false;
    std::uint64_t               eventSequence_  = 0;
};

/** @brief Lives / leak counter for a defended base. */
class EVENGINE_API_FOUNDATION DefenseBase {
public:
    /** @brief Constructs with a positive life budget. */
    explicit DefenseBase(int lives = 20);
    [[nodiscard]] int  lives() const noexcept { return lives_; }
    [[nodiscard]] int  leaks() const noexcept { return leaks_; }
    [[nodiscard]] bool isDefeated() const noexcept { return lives_ <= 0; }
    /** @brief Applies one or more leaks; lives clamp at zero. */
    [[nodiscard]] Result<int> leak(int count = 1);
    /** @brief Restores the life budget. */
    [[nodiscard]] Result<void> reset(int lives);

private:
    int lives_ = 20;
    int leaks_ = 0;
};

/** @brief Script module factory for tower-defense schedule helpers. */
class TowerDefense : public Module {
public:
    Module_REG(TowerDefense);
    TowerDefense()           = default;
    ~TowerDefense() override = default;
};

}  // namespace eve::towerdefense
