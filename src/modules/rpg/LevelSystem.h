#pragma once
#include "common/Export.h"


/**
 * @file LevelSystem.h
 * @brief 角色成长：等级 / 经验 / 升级。
 *
 * 进度数据存在 RPGActor::Progression 组件。gainXp 在跨过阈值时升级并发出
 * LevelUpEvent（推入当前 SimulationEventScope 的队列，RPG facade 在 update() 后轮询）。
 * 阈值按 xpGrowth 比例逐级增长。
 */

#include "common/Result.h"
#include "rpg/ActorEventRef.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief Move-only progression candidate bound to one actor incarnation and its saved progression facts.
 * @ownership Holds only a weak actor reference; it never retains the actor or Table.
 * @thread Prepare and commit on the same actor owner thread; no callbacks. */
class PreparedProgressionGain {
public:
    /** @brief Constructs a PreparedProgressionGain. */
    PreparedProgressionGain() = default;
    /** @brief Transfer the prepared candidate without allocation. */
    PreparedProgressionGain(PreparedProgressionGain &&) noexcept = default;
    /** @brief Transfer the prepared candidate without allocation. */
    PreparedProgressionGain &operator=(PreparedProgressionGain &&) noexcept = default;
    /** @brief Copying a prepared candidate is forbidden. */
    PreparedProgressionGain(const PreparedProgressionGain &) = delete;
    /** @brief Copying a prepared candidate is forbidden. */
    PreparedProgressionGain &operator=(const PreparedProgressionGain &) = delete;
    /** @brief Number of level transitions contained in this prepared candidate. */
    [[nodiscard]] int levelsGained() const noexcept { return nextLevel_ - previousLevel_; }

private:
    ActorEventRef actor_;
    int       previousLevel_ = 0;
    double    previousXp_ = 0.0;
    double    previousXpToNext_ = 0.0;
    int       nextLevel_ = 0;
    double    nextXp_ = 0.0;
    double    nextXpToNext_ = 0.0;
    friend class LevelSystem;
};

/** @brief 升级事件。 */
struct LevelUpEvent {
    ActorEventRef actor;
    int previousLevel = 0;
    int newLevel = 0;
};

/**
 * @brief 成长系统：读写 RPGActor::Progression 并驱动升级事件。
 * @thread 调用线程应与 RPGActor 的 ECS 线程一致。
 */
class EVENGINE_API_PLATFORM LevelSystem {
public:
    /** @brief 当前等级（无 progression 时返回 1）。 */
    static int getLevel(RPGActor *actor);
    /** @brief 当前经验。 */
    static double getXp(RPGActor *actor);
    /** @brief 升级所需经验。 */
    static double getXpToNext(RPGActor *actor);
    /** @brief 设置当前等级的升级阈值。 */
    static void setXpToNext(RPGActor *actor, double value);
    /** @brief 直接设定等级（不改经验；用于读档/初始化）。 */
    static void setLevel(RPGActor *actor, int level);

    /**
     * @brief Atomically restore one validated progression checkpoint.
     * @param actor Borrowed actor owned by the ECS world.
     * @param level Positive level.
     * @param xp Finite, non-negative progress below xpToNext.
     * @param xpToNext Finite positive threshold for the current level.
     * @return Applied state, or a structured validation failure with no mutation.
     * @thread Call on the actor's owning ECS simulation thread.
     * @reentrancy No callbacks or level-up events are emitted.
     */
    [[nodiscard]] static eve::Result<void> restoreProgression(RPGActor *actor, int level, double xp,
                                                              double xpToNext);

    /**
     * @brief 增加经验；每跨过阈值升一级并发出 LevelUpEvent（compatibility facade (脚本兼容门面)）。
     * @return 是否至少升了一级。
     * @param xpGrowth 每级阈值增长比例（默认 1.2）。
     */
    static bool gainXp(RPGActor *actor, double amount, double xpGrowth = 1.2);

    /**
     * @brief Validate an XP gain and calculate its complete progression candidate without mutation/events.
     * @return Prepared candidate, or a structured missing-component/finite/range failure.
     * @remarks Requires existing Progression; preparation never installs a missing component.
     * @cost One arithmetic step per crossed level, bounded to 10000; constant-size candidate storage.
     * @thread Actor owning ECS simulation thread.
     * @reentrancy No callbacks or events are emitted.
     */
    [[nodiscard]] static eve::Result<PreparedProgressionGain>
    prepareGainXp(RPGActor *actor, double amount, double xpGrowth = 1.2);
    /**
     * @brief Commit a prepared XP gain if actor progression is still unchanged.
     * @return Number of levels gained, or Conflict without mutation/events.
     * @remarks Queue capacity is admitted before progression changes; all transitions publish together.
     * A retired actor/world returns StaleHandle. Failed queue admission leaves progression/events unchanged.
     * The value candidate is consumed by the call; prepare again to retry a rejected transaction.
     * @cost Constant actor validation plus one event per gained level; queue growth is amortized per simulation.
     * @thread Same actor-owning thread used for preparation.
     * @reentrancy No callbacks are invoked; events are poll-only.
     */
    [[nodiscard]] static eve::Result<int> commitGainXp(PreparedProgressionGain prepared);

    /** @brief Replace out with this simulation's queued level transitions; self-output is a no-op.
     * @ownership Transfers event values without retaining actors/worlds; no callbacks or allocation. */
    static void pollLevelUps(std::vector<LevelUpEvent> &out);
};

}  // namespace eve::rpg
