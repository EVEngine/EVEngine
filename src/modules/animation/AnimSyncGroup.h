#pragma once

#include "common/Time.h"

#include <vector>

namespace eve::animation {

class AnimPlayer;

/** @brief Marker-aware synchronization group for locomotion animation players. */
class AnimSyncGroup {
public:
    void addPlayer(AnimPlayer* player, float phaseOffset = 0.f);
    void clear() { entries_.clear(); leader_ = 0; phase_ = 0.f; usedMarkerSync_ = false; }
    int getCount() const { return static_cast<int>(entries_.size()); }
    void setLeader(int index);
    int getLeader() const { return leader_; }
    /** @brief Advance and synchronize all players using one scheduler step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);
    float getPhase() const { return phase_; }
    /** @brief Whether the latest update used compatible sync markers for at least one follower. */
    bool getUsedMarkerSync() const { return usedMarkerSync_; }

private:
    struct Entry { AnimPlayer* player = nullptr; float phaseOffset = 0.f; };
    std::vector<Entry> entries_;
    int leader_ = 0;
    float phase_ = 0.f;
    bool usedMarkerSync_ = false;
    eve::SimulationTick lastTick_ = eve::SimulationTick::zero();
    bool hasLastTick_ = false;
};

}  // namespace eve::animation
