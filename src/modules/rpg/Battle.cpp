#include "rpg/Battle.h"

#include "rpg/AttributeSystem.h"
#include "rpg/BattleSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusSystem.h"
#include "rpg/SkillSystem.h"
#include "rpg/TraitSystem.h"
#include "rpg/VitalsSystem.h"

#include <algorithm>

namespace eve::rpg {

void Battle::addActor(RPGActor *actor, int side) {
    if (!actor) return;
    participants_.push_back(Participant{actor, side});
}

bool Battle::isDead(const Participant &p) const {
    return !p.actor || VitalsSystem::isDead(p.actor, "hp");
}

bool Battle::isActorAlive(RPGActor *actor) const {
    return actor && !VitalsSystem::isDead(actor, "hp");
}

std::vector<RPGActor *> Battle::livingOnSide(int side) const {
    std::vector<RPGActor *> out;
    for (const auto &p : participants_) {
        if (p.side == side && !isDead(p)) out.push_back(p.actor);
    }
    return out;
}

RPGActor *Battle::randomOpponent(int mySide) {
    std::vector<RPGActor *> opps;
    for (const auto &p : participants_) {
        if (p.side != mySide && !isDead(p)) opps.push_back(p.actor);
    }
    if (opps.empty()) return nullptr;
    return opps[size_t((seedCounter_++) % opps.size())];
}

int Battle::sideOf(RPGActor *actor) const {
    for (const auto &p : participants_) {
        if (p.actor == actor) return p.side;
    }
    return playerSide_;
}

int Battle::computeWinnerSide() const {
    int aliveSide = -1;
    bool anyAlive = false;
    for (const auto &p : participants_) {
        if (isDead(p)) continue;
        anyAlive = true;
        if (aliveSide == -1) {
            aliveSide = p.side;
        } else if (aliveSide != p.side) {
            return -1;  // 多侧尚存活 → 未定
        }
    }
    return anyAlive ? aliveSide : -1;  // 全灭 → -1（平局）
}

void Battle::setPlayerSide(int side) { playerSide_ = side; }
int Battle::getPlayerSide() const { return playerSide_; }

bool Battle::isFinished() const { return finished_; }
bool Battle::isVictory() const { return finished_ && winner_ == playerSide_; }
bool Battle::isDefeat() const { return finished_ && winner_ != -1 && winner_ != playerSide_; }
int Battle::getWinnerSide() const { return winner_; }
int Battle::getTurn() const { return turn_; }

void Battle::setAction(RPGActor *actor, const std::string &skillId, RPGActor *target) {
    setActionChecked(actor, skillId, target).ignore();
}

RPGActor *Battle::lowestHealthTarget(int side, bool sameSide) const {
    RPGActor *selected = nullptr;
    double selectedRatio = 0.0;
    for (const auto &participant : participants_) {
        if (isDead(participant) || ((participant.side == side) != sameSide)) continue;
        const double maximum = VitalsSystem::getMax(participant.actor, "hp");
        if (maximum <= 0.0) continue;
        const double ratio = VitalsSystem::getCurrent(participant.actor, "hp") / maximum;
        if (!selected || ratio < selectedRatio) {
            selected = participant.actor;
            selectedRatio = ratio;
        }
    }
    return selected;
}

eve::Result<void> Battle::setActionChecked(RPGActor *actor, const std::string &skillId,
                                           RPGActor *target) {
    const auto reject = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "rpg.battle.action"));
    };
    if (!actor) return reject(eve::DiagnosticCode::InvalidArgument, "actor must not be null", "actor");
    if (finished_ || started_)
        return reject(eve::DiagnosticCode::Conflict,
                      "actions can only be queued before a non-finished round", "battle");
    const auto participant = std::find_if(participants_.begin(), participants_.end(),
                                          [actor](const Participant &value) {
                                              return value.actor == actor;
                                          });
    if (participant == participants_.end())
        return reject(eve::DiagnosticCode::NotFound, "actor is not a battle participant", "actor");
    if (isDead(*participant))
        return reject(eve::DiagnosticCode::Conflict, "dead actors cannot queue actions", "actor");
    if (std::any_of(roundActions_.begin(), roundActions_.end(), [actor](const PendingAction &value) {
            return value.actor == actor;
        }))
        return reject(eve::DiagnosticCode::Conflict,
                      "actor already has an action queued for this round", "actor");
    if (!skillId.empty() && !actor->knowsSkill(skillId))
        return reject(eve::DiagnosticCode::NotFound, "actor does not know the requested skill", "skillId");
    if (target) {
        const auto targetParticipant = std::find_if(
            participants_.begin(), participants_.end(), [target](const Participant &value) {
                return value.actor == target;
            });
        if (targetParticipant == participants_.end())
            return reject(eve::DiagnosticCode::NotFound,
                          "target is not a battle participant", "target");
        if (isDead(*targetParticipant))
            return reject(eve::DiagnosticCode::Conflict, "target is not alive", "target");
        const std::string targetType = skillId.empty() ? "enemySingle"
                                                       : SkillSystem::getTargetType(actor, skillId);
        if (targetType == "self" && target != actor)
            return reject(eve::DiagnosticCode::InvalidArgument,
                          "self-target skill must target its caster", "target");
        if (targetType == "allySingle" && targetParticipant->side != participant->side)
            return reject(eve::DiagnosticCode::InvalidArgument,
                          "ally-target action must target the caster side", "target");
        if (targetType != "self" && targetType != "allySingle" &&
            targetParticipant->side == participant->side)
            return reject(eve::DiagnosticCode::InvalidArgument,
                          "enemy-target action must target another side", "target");
    }
    roundActions_.push_back(PendingAction{actor, skillId, target, 0.0});
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Battle::setActionByPolicyChecked(RPGActor *actor,
                                                    const std::string &skillId,
                                                    BattleTargetPolicy policy) {
    if (policy == BattleTargetPolicy::Auto) return setActionChecked(actor, skillId, nullptr);
    const int side = sideOf(actor);
    RPGActor *target = nullptr;
    if (policy == BattleTargetPolicy::Self) target = actor;
    else if (policy == BattleTargetPolicy::LowestHealthAlly)
        target = lowestHealthTarget(side, true);
    else if (policy == BattleTargetPolicy::LowestHealthEnemy)
        target = lowestHealthTarget(side, false);
    if (!target)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "target policy found no living participant", "policy", {},
            "rpg.battle.action-policy"));
    return setActionChecked(actor, skillId, target);
}

void Battle::autoEnemyActions() {
    if (finished_) return;
    for (const auto &p : participants_) {
        if (p.side == playerSide_ || isDead(p)) continue;
        bool already = false;
        for (const auto &ra : roundActions_) {
            if (ra.actor == p.actor) {
                already = true;
                break;
            }
        }
        if (already) continue;
        // 随机已学技能，否则普攻（skillId 空 = 普攻）
        std::vector<std::string> known;
        for (const auto &[id, unused] : p.actor->skills()->known) {
            (void)unused;
            known.push_back(id);
        }
        std::string skillId;
        if (!known.empty()) {
            skillId = known[size_t((seedCounter_++) % known.size())];
        }
        roundActions_.push_back(PendingAction{p.actor, skillId, randomOpponent(p.side), 0.0});
    }
}

void Battle::startRound() {
    if (finished_) return;
    ++turn_;
    // 计算先攻：speed 属性 + 技能攻击速度修正。
    for (auto &pa : roundActions_) {
        double speed = AttributeSystem::getFinal(pa.actor, "speed");
        if (speed <= 0.0) speed = AttributeSystem::getFinal(pa.actor, "agi");
        if (speed <= 0.0) speed = 0.0;
        pa.initiative = speed + TraitSystem::getAttackSpeed(pa.actor);
    }
    std::stable_sort(roundActions_.begin(), roundActions_.end(),
                     [](const PendingAction &x, const PendingAction &y) {
                         return x.initiative > y.initiative;
                     });
    queue_ = std::move(roundActions_);
    roundActions_.clear();
    started_ = true;
    events_.push_back(BattleEvent{"roundStart", "", nullptr, nullptr, 0.0, false});
}

bool Battle::executeNextAction() {
    if (!started_ || finished_) return false;
    if (queue_.empty()) {
        started_ = false;
        return false;
    }
    PendingAction pa = queue_.front();
    queue_.erase(queue_.begin());

    // 已死则跳过
    if (VitalsSystem::isDead(pa.actor, "hp")) {
        return true;
    }

    // 解析目标：未指定则按 targetType / 默认对手。
    RPGActor *target = pa.target;
    if (!target) {
        if (pa.skillId.empty()) {
            target = randomOpponent(sideOf(pa.actor));
        } else if (SkillSystem::getTargetType(pa.actor, pa.skillId) == "self") {
            target = pa.actor;
        } else if (SkillSystem::getTargetType(pa.actor, pa.skillId) == "allySingle") {
            target = lowestHealthTarget(sideOf(pa.actor), true);
        } else {
            target = randomOpponent(sideOf(pa.actor));
        }
    }
    if (!target) return true;

    execute(pa, seedCounter_);
    // 每步检查结束：仅剩一方 → 胜；双方全灭 → 平；否则继续。
    const int w = computeWinnerSide();
    if (w != -1) {
        finished_ = true;
        winner_ = w;
        events_.push_back(BattleEvent{winner_ == playerSide_ ? "victory" : "defeat", "", nullptr,
                                      nullptr, 0.0, false});
    } else {
        bool anyAlive = false;
        for (const auto &p : participants_) {
            if (!isDead(p)) {
                anyAlive = true;
                break;
            }
        }
        if (!anyAlive) {
            finished_ = true;
            winner_ = -1;
            events_.push_back(BattleEvent{"defeat", "", nullptr, nullptr, 0.0, false});
        }
    }
    return true;
}

void Battle::execute(PendingAction &pa, unsigned &seedCounter) {
    const std::string skillId = pa.skillId;
    const std::string targetType = skillId.empty() ? std::string("enemySingle")
                                                   : SkillSystem::getTargetType(pa.actor, skillId);
    RPGActor *target = pa.target;
    if (!target) {
        if (targetType == "self") {
            target = pa.actor;
        } else if (targetType == "allySingle") {
            target = lowestHealthTarget(sideOf(pa.actor), true);
        } else {
            target = randomOpponent(sideOf(pa.actor));
        }
    }
    if (!target) return;

    events_.push_back(BattleEvent{"actionStart", skillId, pa.actor, target, 0.0, false});

    // 目标已死：换一个存活对手
    if (targetType != "self" && VitalsSystem::isDead(target, "hp")) {
        target = targetType == "allySingle" ? lowestHealthTarget(sideOf(pa.actor), true)
                                             : randomOpponent(sideOf(pa.actor));
        if (!target) return;
    }

    const SkillDamageSpec *spec = skillId.empty() ? nullptr : BattleSystem::findSkillDamage(skillId);
    if (!spec && !skillId.empty()) {
        // 无伤害规格：走技能授予效果（治疗/状态等，无公式伤害）。
        SkillSystem::beginCast(pa.actor, skillId, target);
        return;
    }
    if (!spec) {
        // 普攻默认规格
        static const SkillDamageSpec kDefault{};
        spec = &kDefault;
    }

    DamageResult result = BattleSystem::resolveHit(pa.actor, target, *spec, seedCounter++);
    if (!result.hit) {
        events_.push_back(BattleEvent{"miss", skillId, pa.actor, target, 0.0, false});
        return;
    }

    // 目标资源名任意：damageType 即资源名，"XHeal" 后缀表示治疗。
    std::string resource = spec->damageType;
    bool heal = false;
    if (resource.size() >= 4 && resource.compare(resource.size() - 4, 4, "Heal") == 0) {
        resource = resource.substr(0, resource.size() - 4);
        heal = true;
    }
    if (heal) {
        double healed = VitalsSystem::heal(target, resource, result.amount);
        events_.push_back(BattleEvent{"heal", skillId, pa.actor, target, healed, false});
    } else {
        double dealt = VitalsSystem::takeDamage(target, resource, result.amount,
                                                skillId.empty() ? "attack" : skillId);
        events_.push_back(BattleEvent{"damage", skillId, pa.actor, target, dealt, result.crit});
    }
}

int Battle::getActorCount() const { return int(participants_.size()); }
RPGActor *Battle::getActor(int index) const {
    if (index < 0 || size_t(index) >= participants_.size()) return nullptr;
    return participants_[size_t(index)].actor;
}
int Battle::getSide(int index) const {
    if (index < 0 || size_t(index) >= participants_.size()) return 0;
    return participants_[size_t(index)].side;
}

int Battle::getEventCount() const { return int(polled_.size()); }
BattleEvent Battle::getEvent(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return BattleEvent{};
    return polled_[size_t(index)];
}
void Battle::pollEvents() {
    polled_ = std::move(events_);
    events_.clear();
}

std::string Battle::getEventAction(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return {};
    return polled_[size_t(index)].action;
}
std::string Battle::getEventSkillId(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return {};
    return polled_[size_t(index)].skillId;
}
RPGActor *Battle::getEventCaster(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return nullptr;
    return polled_[size_t(index)].caster;
}
RPGActor *Battle::getEventTarget(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return nullptr;
    return polled_[size_t(index)].target;
}
double Battle::getEventAmount(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return 0.0;
    return polled_[size_t(index)].amount;
}
bool Battle::getEventCrit(int index) const {
    if (index < 0 || size_t(index) >= polled_.size()) return false;
    return polled_[size_t(index)].crit;
}

}  // namespace eve::rpg
