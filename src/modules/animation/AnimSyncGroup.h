#pragma once

#include <vector>

namespace eve::animation {

class AnimPlayer;

/** @brief Duration-normalized synchronization group for locomotion animation players. */
class AnimSyncGroup {
public:
    void addPlayer(AnimPlayer* player, float phaseOffset = 0.f);
    void clear() { entries_.clear(); leader_ = 0; }
    int getCount() const { return static_cast<int>(entries_.size()); }
    void setLeader(int index);
    int getLeader() const { return leader_; }
    /** @brief Advance the leader and align all followers to its normalized phase. */
    void update(float dt);
    float getPhase() const { return phase_; }

private:
    struct Entry { AnimPlayer* player = nullptr; float phaseOffset = 0.f; };
    std::vector<Entry> entries_;
    int leader_ = 0;
    float phase_ = 0.f;
};

}  // namespace eve::animation
