#include "rpg/LevelSystem.h"

#include "rpg/RPGActor.h"

#include <cmath>

namespace eve::rpg {

namespace {
std::vector<LevelUpEvent> &levelUpEvents() {
    static std::vector<LevelUpEvent> events;
    return events;
}
}  // namespace

int LevelSystem::getLevel(RPGActor *actor) {
    return actor ? actor->progression()->level : 1;
}

double LevelSystem::getXp(RPGActor *actor) {
    return actor ? actor->progression()->xp : 0.0;
}

double LevelSystem::getXpToNext(RPGActor *actor) {
    return actor ? actor->progression()->xpToNext : 100.0;
}

void LevelSystem::setXpToNext(RPGActor *actor, double value) {
    if (!actor) return;
    actor->progression()->xpToNext = value > 0.0 ? value : 0.0;
}

void LevelSystem::setLevel(RPGActor *actor, int level) {
    if (!actor) return;
    actor->progression()->level = level > 0 ? level : 1;
}

eve::Result<void> LevelSystem::restoreProgression(RPGActor *actor, int level, double xp, double xpToNext) {
    if (!actor)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "progression restore requires an actor", "actor"));
    if (level < 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "progression level must be positive", "level"));
    if (!std::isfinite(xp) || xp < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "progression xp must be finite and non-negative", "xp"));
    if (!std::isfinite(xpToNext) || xpToNext <= 0.0 || xp >= xpToNext)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "progression threshold must be finite, positive and greater than current xp", "xpToNext"));
    auto candidate     = *actor->progression();
    candidate.level    = level;
    candidate.xp       = xp;
    candidate.xpToNext = xpToNext;
    *actor->progression() = candidate;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool LevelSystem::gainXp(RPGActor *actor, double amount, double xpGrowth) {
    auto prepared = prepareGainXp(actor, amount, xpGrowth);
    if (!prepared.ok()) return false;
    auto committed = commitGainXp(std::move(prepared).takeValue());
    return committed.ok() && std::move(committed).takeValue() > 0;
}

eve::Result<PreparedProgressionGain> LevelSystem::prepareGainXp(RPGActor *actor, double amount,
                                                                double xpGrowth) {
    if (!actor)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP gain requires an actor", "actor"));
    if (!std::isfinite(amount) || amount <= 0.0)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP gain must be finite and positive", "amount"));
    if (!std::isfinite(xpGrowth) || xpGrowth < 1.0)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP growth must be finite and at least one", "xpGrowth"));
    const auto &current = *actor->progression();
    if (current.level < 1 || !std::isfinite(current.xp) || current.xp < 0.0 ||
        !std::isfinite(current.xpToNext) || current.xpToNext <= 0.0 || current.xp >= current.xpToNext)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "actor progression is not valid for XP gain", "progression"));

    PreparedProgressionGain prepared;
    prepared.actor_ = actor;
    prepared.previousLevel_ = current.level;
    prepared.previousXp_ = current.xp;
    prepared.previousXpToNext_ = current.xpToNext;
    prepared.nextLevel_ = current.level;
    prepared.nextXp_ = current.xp + amount;
    prepared.nextXpToNext_ = current.xpToNext;
    if (!std::isfinite(prepared.nextXp_))
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP gain would overflow progression", "amount"));
    constexpr int maxLevelsPerGain = 10000;
    int levels = 0;
    while (prepared.nextXp_ >= prepared.nextXpToNext_) {
        if (++levels > maxLevelsPerGain)
            return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation,
                "XP gain exceeds the maximum levels allowed in one transaction", "amount"));
        prepared.nextXp_ -= prepared.nextXpToNext_;
        ++prepared.nextLevel_;
        prepared.nextXpToNext_ *= xpGrowth;
        if (!std::isfinite(prepared.nextXpToNext_) || prepared.nextXpToNext_ <= 0.0)
            return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "XP growth would overflow progression", "xpGrowth"));
    }
    return eve::Result<PreparedProgressionGain>::success(
        std::move(prepared), eve::Status::success(eve::StatusCode::Pending));
}

eve::Result<int> LevelSystem::commitGainXp(PreparedProgressionGain prepared) {
    if (!prepared.actor_)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "prepared XP gain is empty", "prepared"));
    auto &current = *prepared.actor_->progression();
    if (current.level != prepared.previousLevel_ || current.xp != prepared.previousXp_ ||
        current.xpToNext != prepared.previousXpToNext_)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "actor progression changed after XP preparation", "progression"));
    current.level = prepared.nextLevel_;
    current.xp = prepared.nextXp_;
    current.xpToNext = prepared.nextXpToNext_;
    const int levels = prepared.nextLevel_ - prepared.previousLevel_;
    for (int offset = 0; offset < levels; ++offset) {
        LevelUpEvent event;
        event.actor = prepared.actor_;
        event.previousLevel = prepared.previousLevel_ + offset;
        event.newLevel = event.previousLevel + 1;
        levelUpEvents().push_back(std::move(event));
    }
    return eve::Result<int>::success(levels, eve::Status::success(eve::StatusCode::Applied));
}

void LevelSystem::pollLevelUps(std::vector<LevelUpEvent> &out) {
    out.clear();
    out = std::move(levelUpEvents());
    levelUpEvents().clear();
}

}  // namespace eve::rpg
