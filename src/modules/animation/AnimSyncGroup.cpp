#include "animation/AnimSyncGroup.h"

#include "animation/AnimationTime.h"
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

eve::Result<void> AnimSyncGroup::advance(const eve::SimulationStep& step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "AnimSyncGroup");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    (void)std::move(seconds).takeValue();
    usedMarkerSync_ = false;
    if (entries_.empty()) {
        lastTick_ = step.tick;
        hasLastTick_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }
    for (const Entry& entry : entries_) {
        if (entry.player->hasCurrentTick() && step.tick <= entry.player->currentTick())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "animation sync-group player already consumed this tick"));
    }
    Entry& leader = entries_[static_cast<size_t>(leader_)];
    auto leaderResult = leader.player->advance(step);
    if (!leaderResult) return eve::Result<void>::failure(leaderResult.status());
    AnimClip* leaderClip = leader.player->getClip();
    if (!leaderClip || leaderClip->getDuration() <= 1e-8f) {
        lastTick_ = step.tick;
        hasLastTick_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }
    phase_ = leaderClip->wrapTime(leader.player->getTime()) / leaderClip->getDuration();
    for (int i = 0; i < getCount(); ++i) {
        if (i == leader_) continue;
        AnimPlayer* player = entries_[static_cast<size_t>(i)].player;
        auto playerResult = player->advance(step);
        if (!playerResult) return eve::Result<void>::failure(playerResult.status());
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
    lastTick_ = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void AnimSyncGroup::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "AnimSyncGroup");
    if (!step) {
        step.ignore("legacy AnimSyncGroup update");
        return;
    }
    advance(std::move(step).takeValue()).ignore("legacy AnimSyncGroup update");
}

}  // namespace eve::animation
