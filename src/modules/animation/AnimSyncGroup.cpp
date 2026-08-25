#include "animation/AnimSyncGroup.h"

#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "common/Exception.h"

#include <cmath>

namespace eve::animation {

void AnimSyncGroup::addPlayer(AnimPlayer* player, float phaseOffset) {
    if (!player) throw Exception("AnimSyncGroup.addPlayer: player is null");
    entries_.push_back({player, phaseOffset});
}

void AnimSyncGroup::setLeader(int index) {
    if (index < 0 || index >= getCount()) throw Exception("AnimSyncGroup.setLeader: invalid index %d", index);
    leader_ = index;
}

void AnimSyncGroup::update(float dt) {
    if (dt < 0.f) throw Exception("AnimSyncGroup.update: dt must be >= 0");
    usedMarkerSync_ = false;
    if (entries_.empty()) return;
    Entry& leader = entries_[static_cast<size_t>(leader_)];
    leader.player->update(dt);
    AnimClip* leaderClip = leader.player->getClip();
    if (!leaderClip || leaderClip->getDuration() <= 1e-8f) return;
    phase_ = leaderClip->wrapTime(leader.player->getTime()) / leaderClip->getDuration();
    for (int i = 0; i < getCount(); ++i) {
        if (i == leader_) continue;
        AnimPlayer* player = entries_[static_cast<size_t>(i)].player;
        AnimClip* clip = player->getClip();
        if (!clip || clip->getDuration() <= 1e-8f) continue;
        float mappedTime;
        if (leaderClip->hasCompatibleSyncMarkers(clip)) {
            mappedTime = leaderClip->mapSyncTimeTo(leader.player->getTime(), clip);
            usedMarkerSync_ = true;
        } else {
            mappedTime = phase_ * clip->getDuration();
        }
        mappedTime += entries_[static_cast<size_t>(i)].phaseOffset * clip->getDuration();
        player->setTime(clip->wrapTime(mappedTime));
    }
}

}  // namespace eve::animation
