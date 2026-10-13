#include "rpg/BattleControl.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "common/Capability.h"
#include "common/Json.h"
#include "rpg/AttributeSystem.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/BattleTactics.h"
#include "rpg/BattleVictory.h"
#include "rpg/ClassSystem.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
#include "rpg/TraitSystem.h"
#include "rpg/WorldState.h"

// BattleControl.cpp
namespace eve::rpg {
namespace {

LogicalId gameplayId(std::string_view value) { return LogicalId::parse(value).value(); }

Result<std::string> stringParameter(const Value &parameters, std::string_view name, bool optional = false) {
    const auto *object = parameters.getIf<Value::Object>();
    if (!object)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RPG gameplay parameters must be an object", "parameters"));
    const auto found = object->find(std::string(name));
    if (found == object->end() && optional) return Result<std::string>::success({});
    if (found == object->end() || !found->second.isString())
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "RPG gameplay parameter must be a string",
                                                              "parameters." + std::string(name)));
    return Result<std::string>::success(found->second.asString());
}

}  // namespace

BattleControl::BattleControl(Battle &battle, SubjectRef instance) : battle_(battle), instance_(instance) {
    cap::addListener<IGameplayControlProvider>(this);
    cap::addListener<IGameplayInstanceCatalog>(this);
}

BattleControl::~BattleControl() {
    cap::removeListener<IGameplayInstanceCatalog>(this);
    cap::removeListener<IGameplayControlProvider>(this);
}

Result<void> BattleControl::bindParticipant(SubjectRef subject, RPGActor *actor) {
    if (!instance_.isValid() || !subject.isValid() || !actor)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "battle, subject, and actor identities must be valid", "participant"));
    bool participant = false;
    for (int index = 0; index < battle_.getActorCount(); ++index)
        if (battle_.getActor(index) == actor) participant = true;
    if (!participant)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "actor is not a participant in the adapted battle", "actor"));
    const auto existing = actors_.find(subject);
    if (existing != actors_.end() && existing->second != actor)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "subject is already bound to another participant", "subject"));
    if (const auto reverse = subjectOf(actor); reverse.isValid() && reverse != subject)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "actor is already bound to another subject", "actor"));
    actors_[subject] = actor;
    ++revision_;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::string_view BattleControl::gameplayDomain() const noexcept { return "rpg.battle"; }

std::vector<SubjectRef> BattleControl::gameplayInstances() const {
    // This adapter serves exactly one instance: the identity it was constructed
    // with is the only one it can answer for.
    if (!instance_.isValid()) return {};
    return {instance_};
}

bool BattleControl::controls(const GameplaySession &session, SubjectRef subject) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), subject) !=
               session.controlledSubjects.end();
}

RPGActor *BattleControl::resolve(SubjectRef subject) const {
    const auto found = actors_.find(subject);
    return found == actors_.end() ? nullptr : found->second;
}

SubjectRef BattleControl::subjectOf(RPGActor *actor) const {
    for (const auto &[subject, candidate] : actors_)
        if (candidate == actor) return subject;
    return SubjectRef::nil();
}

Result<GameplayObservation> BattleControl::observeGameplay(const GameplaySession &session, SubjectRef instance) const {
    if (instance != instance_ || !instance_.isValid())
        return Result<GameplayObservation>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG battle gameplay instance was not found", "instance"));
    bool         ownsParticipant = false;
    Value::Array participants;
    for (int index = 0; index < battle_.getActorCount(); ++index) {
        RPGActor  *actor   = battle_.getActor(index);
        const auto subject = subjectOf(actor);
        if (!subject.isValid()) continue;
        const bool controlled = controls(session, subject);
        ownsParticipant       = ownsParticipant || controlled;
        participants.emplace_back(Value::Object{
            {"alive", Value(battle_.isActorAlive(actor))},
            {"controlled", Value(controlled)},
            {"hp", Value(actor->getCurrent("hp"))},
            {"maxHp", Value(actor->getMax("hp"))},
            {"side", Value(battle_.getSide(index))},
            {"subject", Value(subject.format())},
        });
    }
    if (session.access == GameplayAccess::PlayerEquivalent && !ownsParticipant)
        return Result<GameplayObservation>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "session controls no participant in this RPG battle", "session.controlledSubjects"));
    GameplayObservation observation;
    observation.domain   = gameplayId("gameplay:rpg-battle");
    observation.instance = instance_;
    observation.tick     = tick_;
    observation.revision = revision_;
    observation.state    = Value(Value::Object{
           {"defeat", Value(battle_.isDefeat())},
           {"finished", Value(battle_.isFinished())},
           {"participants", Value(std::move(participants))},
           {"turn", Value(battle_.getTurn())},
           {"victory", Value(battle_.isVictory())},
    });
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> BattleControl::availableGameplayActions(const GameplaySession &session,
                                                                                      SubjectRef             instance,
                                                                                      SubjectRef subject) const {
    if (instance != instance_)
        return Result<std::vector<GameplayActionDescriptor>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG battle gameplay instance was not found", "instance"));
    RPGActor *actor = resolve(subject);
    if (!actor)
        return Result<std::vector<GameplayActionDescriptor>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG battle participant was not found", "subject"));
    if (!controls(session, subject))
        return Result<std::vector<GameplayActionDescriptor>>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "session does not control this RPG participant", "subject"));
    if (!battle_.isActorAlive(actor) || battle_.isFinished())
        return Result<std::vector<GameplayActionDescriptor>>::success({});
    const Value targetSchema(Value::Object{{"type", Value("subject")}});
    const Value skillSchema(
        Value::Object{{"skillId", Value(Value::Object{{"type", Value("string")}})}, {"target", targetSchema}});
    return Result<std::vector<GameplayActionDescriptor>>::success({
        {gameplayId("rpg:attack"), Value(Value::Object{{"target", targetSchema}})},
        {gameplayId("rpg:skill"), skillSchema},
    });
}

Result<GameplayCommandReceipt> BattleControl::submitGameplay(const GameplaySession &session, SubjectRef instance,
                                                             const GameplayCommand &command) {
    if (instance != instance_)
        return Result<GameplayCommandReceipt>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG battle gameplay instance was not found", "instance"));
    RPGActor *actor = resolve(command.subject);
    if (!actor)
        return Result<GameplayCommandReceipt>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG battle participant was not found", "command.subject"));
    if (!controls(session, command.subject))
        return Result<GameplayCommandReceipt>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "session does not control this RPG participant", "command.subject"));
    if (command.id.empty())
        return Result<GameplayCommandReceipt>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "command id must not be empty", "command.id"));
    if (command.observedTick != tick_ || command.expectedRevision != revision_)
        return Result<GameplayCommandReceipt>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "RPG command was based on a stale observation", "command.expectedRevision"));
    auto targetText = stringParameter(command.parameters, "target");
    if (!targetText) return Result<GameplayCommandReceipt>::failure(targetText.status());
    const auto targetId = PersistentId::parse(targetText.value());
    RPGActor  *target   = targetId ? resolve(SubjectRef::fromPersistentId(*targetId)) : nullptr;
    if (!target)
        return Result<GameplayCommandReceipt>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG command target was not found", "parameters.target"));
    std::string skillId;
    if (command.action == gameplayId("rpg:skill")) {
        auto skill = stringParameter(command.parameters, "skillId");
        if (!skill) return Result<GameplayCommandReceipt>::failure(skill.status());
        skillId = std::move(skill).takeValue();
    } else if (command.action != gameplayId("rpg:attack")) {
        return Result<GameplayCommandReceipt>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "unsupported RPG gameplay action", "command.action"));
    }
    auto queued = battle_.setActionChecked(actor, skillId, target);
    if (!queued) return Result<GameplayCommandReceipt>::failure(queued.status());
    ++revision_;
    lastCommandByActor_[actor] = command.id;
    GameplayEvent accepted;
    accepted.sequence           = nextEventSequence_++;
    accepted.tick               = tick_;
    accepted.type               = "rpg.command.accepted";
    accepted.subject            = command.subject;
    accepted.causationCommandId = command.id;
    accepted.correlationId      = command.id;
    accepted.payload            = Value(Value::Object{{"action", Value(command.action.format())}});
    events_.push_back(std::move(accepted));

    GameplayCommandReceipt receipt;
    receipt.commandId         = command.id;
    receipt.executionId       = "rpg-round-" + std::to_string(battle_.getTurn() + 1);
    receipt.acceptedTick      = tick_;
    receipt.resultingRevision = revision_;
    receipt.details = Value(Value::Object{{"skillId", Value(skillId)}, {"target", Value(targetText.value())}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<GameplayObservation> BattleControl::advanceGameplay(const GameplaySession &session, SubjectRef instance,
                                                           const SimulationStep &step) {
    if (instance != instance_)
        return Result<GameplayObservation>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RPG battle gameplay instance was not found", "instance"));
    if (step.tick <= tick_)
        return Result<GameplayObservation>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RPG simulation tick must increase", "step.tick"));
    auto authorized = observeGameplay(session, instance);
    if (!authorized) return Result<GameplayObservation>::failure(authorized.status());
    std::move(authorized).takeValue();
    battle_.autoEnemyActions();
    battle_.startRound();
    while (battle_.executeNextAction() && !battle_.isFinished()) {
    }
    battle_.pollEvents();
    tick_ = step.tick;
    ++revision_;
    for (int index = 0; index < battle_.getEventCount(); ++index) {
        RPGActor     *caster = battle_.getEventCaster(index);
        GameplayEvent event;
        event.sequence = nextEventSequence_++;
        event.tick     = tick_;
        event.type     = "rpg.battle." + battle_.getEventAction(index);
        event.subject  = subjectOf(caster ? caster : battle_.getEventTarget(index));
        if (const auto found = lastCommandByActor_.find(caster); found != lastCommandByActor_.end()) {
            event.causationCommandId = found->second;
            event.correlationId      = found->second;
        }
        event.payload = Value(Value::Object{
            {"amount", Value(battle_.getEventAmount(index))},
            {"critical", Value(battle_.getEventCrit(index))},
            {"skillId", Value(battle_.getEventSkillId(index))},
            {"target", Value(subjectOf(battle_.getEventTarget(index)).format())},
        });
        events_.push_back(std::move(event));
    }
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> BattleControl::gameplayEvents(const GameplaySession &session, SubjectRef instance,
                                                                 std::uint64_t afterSequence) const {
    auto authorized = observeGameplay(session, instance);
    if (!authorized) return Result<std::vector<GameplayEvent>>::failure(authorized.status());
    std::move(authorized).takeValue();
    std::vector<GameplayEvent> result;
    for (const auto &event : events_)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(std::move(result),
                                                       Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rpg

// BattleSystem.cpp
namespace eve::rpg {

namespace {

std::unordered_map<std::string, SkillDamageSpec> &skillDamageTable() {
    static std::unordered_map<std::string, SkillDamageSpec> t;
    return t;
}

eve::Result<double> parseFormula(const std::string &formula, RPGActor *attacker, RPGActor *target, bool validateOnly);

}  // namespace

eve::Result<void> BattleSystem::registerSkillDamageChecked(const std::string &skillId, const SkillDamageSpec &spec) {
    if (skillId.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "skill damage id must not be empty", "skillId"));
    if (!spec.formula.empty()) {
        auto formula = parseFormula(spec.formula, nullptr, nullptr, true);
        if (!formula) return eve::Result<void>::failure(formula.status());
    }
    skillDamageTable()[skillId] = spec;
    return eve::Result<void>::success();
}

void BattleSystem::registerSkillDamage(const std::string &skillId, const SkillDamageSpec &spec) {
    registerSkillDamageChecked(skillId, spec).ignore("legacy RPG damage registration facade");
}

const SkillDamageSpec *BattleSystem::findSkillDamage(const std::string &skillId) {
    auto &t  = skillDamageTable();
    auto  it = t.find(skillId);
    return it == t.end() ? nullptr : &it->second;
}

void BattleSystem::clearSkillDamage() { skillDamageTable().clear(); }

namespace {

// ---- 极简表达式求值器：支持数字、a.<param>、b.<param>、+ - * / ( ) ----
struct FormulaParser {
    const std::string             &s;
    size_t                         pos = 0;
    RPGActor                      *a;
    RPGActor                      *b;
    bool                           validateOnly;
    std::optional<eve::Diagnostic> error;

    FormulaParser(const std::string &str, RPGActor *atk, RPGActor *tgt, bool syntaxOnly)
        : s(str), a(atk), b(tgt), validateOnly(syntaxOnly) {}

    void skipSpace() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }

    double parseExpression() {
        double lhs = parseTerm();
        while (true) {
            skipSpace();
            if (pos >= s.size()) break;
            const char c = s[pos];
            if (c == '+') {
                ++pos;
                lhs += parseTerm();
            } else if (c == '-') {
                ++pos;
                lhs -= parseTerm();
            } else {
                break;
            }
        }
        return lhs;
    }

    double parseTerm() {
        double lhs = parseFactor();
        while (true) {
            skipSpace();
            if (pos >= s.size()) break;
            const char c = s[pos];
            if (c == '*') {
                ++pos;
                lhs *= parseFactor();
            } else if (c == '/') {
                ++pos;
                const double rhs = parseFactor();
                if (validateOnly) {
                    lhs = 1.0;
                } else if (rhs == 0.0) {
                    if (!error)
                        error = eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                       "damage formula cannot divide by zero",
                                                       "formula[" + std::to_string(pos) + "]");
                    lhs = 0.0;
                } else {
                    lhs /= rhs;
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    double parseFactor() {
        skipSpace();
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
            const bool   negate = s[pos++] == '-';
            const double value  = parseFactor();
            return negate ? -value : value;
        }
        if (pos < s.size() && s[pos] == '(') {
            ++pos;
            double v = parseExpression();
            skipSpace();
            if (pos >= s.size() || s[pos] != ')') {
                fail("damage formula is missing a closing parenthesis");
                return 0.0;
            }
            ++pos;
            return v;
        }
        if (pos < s.size() && (s[pos] == 'a' || s[pos] == 'b') && pos + 1 < s.size() && s[pos + 1] == '.') {
            const bool isA = s[pos] == 'a';
            pos += 2;
            std::string name;
            while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
                name.push_back(s[pos++]);
            }
            if (name.empty()) {
                fail("damage formula attribute name must not be empty");
                return 0.0;
            }
            RPGActor *actor = isA ? a : b;
            return actor ? AttributeSystem::getFinal(actor, name) : 0.0;
        }
        if (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
            std::string num;
            while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
                num.push_back(s[pos++]);
            }
            char        *end   = nullptr;
            const double value = std::strtod(num.c_str(), &end);
            if (end == num.c_str() || *end != '\0' || !std::isfinite(value)) {
                fail("damage formula contains an invalid number");
                return 0.0;
            }
            return value;
        }
        fail("damage formula expected a number, attribute, unary sign, or parenthesized expression");
        return 0.0;
    }

    void fail(std::string message) {
        if (error) return;
        error =
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, message, "formula[" + std::to_string(pos) + "]");
    }
};

eve::Result<double> parseFormula(const std::string &formula, RPGActor *attacker, RPGActor *target, bool validateOnly) {
    if (formula.empty())
        return eve::Result<double>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "damage formula must not be empty", "formula"));
    FormulaParser parser(formula, attacker, target, validateOnly);
    const double  value = parser.parseExpression();
    parser.skipSpace();
    if (parser.error) return eve::Result<double>::failure(std::move(*parser.error));
    if (parser.pos != formula.size())
        return eve::Result<double>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                   "damage formula contains trailing input",
                                                                   "formula[" + std::to_string(parser.pos) + "]"));
    if (!std::isfinite(value))
        return eve::Result<double>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                   "damage formula result must be finite", "formula"));
    return eve::Result<double>::success(value);
}

}  // namespace

double BattleSystem::evaluateFormula(const std::string &formula, RPGActor *attacker, RPGActor *target) {
    return std::move(evaluateFormulaChecked(formula, attacker, target)).valueOr(0.0);
}

eve::Result<double> BattleSystem::evaluateFormulaChecked(const std::string &formula, RPGActor *attacker,
                                                         RPGActor *target) {
    return parseFormula(formula, attacker, target, false);
}

DamageResult BattleSystem::resolveHit(RPGActor *attacker, RPGActor *target, const SkillDamageSpec &spec,
                                      unsigned seed) {
    DamageResult result;
    if (!attacker || !target) return result;
    std::mt19937                           rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    // 命中判定：攻击方 hit + (100 - 目标 evasion)
    double attackerHit   = 100.0 + TraitSystem::getExParam(attacker, "hit");
    double targetEvasion = TraitSystem::getExParam(target, "evasion");
    double hitChance     = spec.hitChance + attackerHit - targetEvasion;
    if (dist(rng) * 100.0 >= hitChance) {
        result.hit = false;
        return result;
    }

    // 公式（默认物理：a.atk - b.def，至少 1）
    double amount;
    if (spec.formula.empty()) {
        amount = AttributeSystem::getFinal(attacker, "attack") - AttributeSystem::getFinal(target, "defense");
    } else {
        amount = evaluateFormula(spec.formula, attacker, target);
    }
    if (amount <= 0.0) amount = 1.0;

    // 元素耐性
    if (!spec.element.empty()) {
        result.elementRate = TraitSystem::getElementRate(target, spec.element);
        amount *= result.elementRate;
    }

    // 暴击（RPG Maker 默认 3x）
    double critChance = spec.critChance + TraitSystem::getExParam(attacker, "critRate");
    if (critChance > 0.0 && dist(rng) < critChance) {
        result.crit = true;
        amount *= 3.0;
    }

    result.amount = amount;
    return result;
}

}  // namespace eve::rpg

// BattleTactics.cpp
namespace eve::rpg {
namespace {

std::unordered_map<std::string, BattleTacticsDefinition> &definitions() {
    static std::unordered_map<std::string, BattleTacticsDefinition> value;
    return value;
}

bool validId(const std::string &value, bool allowEmpty = false) {
    if (value.empty()) return allowEmpty;
    if (value.size() > 256) return false;
    for (unsigned char ch : value)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

bool parsePolicy(const std::string &value, BattleTargetPolicy &policy) {
    if (value == "auto")
        policy = BattleTargetPolicy::Auto;
    else if (value == "self")
        policy = BattleTargetPolicy::Self;
    else if (value == "lowestHealthAlly")
        policy = BattleTargetPolicy::LowestHealthAlly;
    else if (value == "lowestHealthEnemy")
        policy = BattleTargetPolicy::LowestHealthEnemy;
    else
        return false;
    return true;
}

bool policyMatchesTargetType(BattleTargetPolicy policy, const std::string &targetType) {
    if (policy == BattleTargetPolicy::Auto) return true;
    if (targetType == "self") return policy == BattleTargetPolicy::Self;
    if (targetType == "allySingle") return policy == BattleTargetPolicy::LowestHealthAlly;
    return policy == BattleTargetPolicy::LowestHealthEnemy;
}

}  // namespace

eve::Result<int> BattleTacticsCatalogue::replaceFromJsonStrict(const std::string &json) {
    std::string parseError;
    const auto  document = eve::json::Document::parse(json, &parseError);
    if (!document.valid())
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                parseError.empty() ? "invalid JSON" : parseError, "$",
                                                                {}, "rpg.battle-tactics"));
    const auto root = document.root();
    if (!root.isArray() || root.size() == 0)
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                "battle tactics catalogue must be a non-empty array",
                                                                "$", {}, "rpg.battle-tactics"));
    const std::unordered_set<std::string> definitionFields = {"id", "rules"};
    const std::unordered_set<std::string> ruleFields = {"skillId", "targetPolicy", "conditionResource", "belowRatio"};
    std::unordered_map<std::string, BattleTacticsDefinition> proposed;
    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto        object = root.at(index);
        const std::string path   = "$[" + std::to_string(index) + "]";
        if (!object.isObject())
            return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                    "battle tactics definition must be an object", path,
                                                                    {}, "rpg.battle-tactics"));
        for (const auto &key : object.keys())
            if (!definitionFields.contains(key))
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "battle tactics definition contains an unknown field",
                    path + "." + key, {}, "rpg.battle-tactics"));
        const auto id    = object.get("id");
        const auto rules = object.get("rules");
        if (!id.isString() || !validId(id.asString()))
            return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                    "battle tactics id must be a stable non-empty id",
                                                                    path + ".id", {}, "rpg.battle-tactics"));
        if (proposed.contains(id.asString()))
            return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists,
                                                                    "duplicate battle tactics id", path + ".id", {},
                                                                    "rpg.battle-tactics"));
        if (!rules.isArray() || rules.size() == 0 || rules.size() > 16)
            return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                    "rules must contain between 1 and 16 entries",
                                                                    path + ".rules", {}, "rpg.battle-tactics"));
        BattleTacticsDefinition definition;
        definition.id = id.asString();
        for (std::size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
            const auto        rule     = rules.at(ruleIndex);
            const std::string rulePath = path + ".rules[" + std::to_string(ruleIndex) + "]";
            if (!rule.isObject())
                return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "battle tactic rule must be an object",
                                                                        rulePath, {}, "rpg.battle-tactics"));
            for (const auto &key : rule.keys())
                if (!ruleFields.contains(key))
                    return eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "battle tactic rule contains an unknown field",
                        rulePath + "." + key, {}, "rpg.battle-tactics"));
            const auto skillId           = rule.get("skillId");
            const auto targetPolicy      = rule.get("targetPolicy");
            const auto conditionResource = rule.get("conditionResource");
            const auto belowRatio        = rule.get("belowRatio");
            if (!skillId.isString() || !validId(skillId.asString(), true) ||
                (!skillId.asString().empty() && !SkillRegistry::find(skillId.asString())))
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::NotFound, "skillId must be empty or reference a registered skill",
                    rulePath + ".skillId", {}, "rpg.battle-tactics"));
            BattleTargetPolicy parsedPolicy = BattleTargetPolicy::Auto;
            if (!targetPolicy.isString() || !parsePolicy(targetPolicy.asString(), parsedPolicy))
                return eve::Result<int>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "targetPolicy is invalid",
                                           rulePath + ".targetPolicy", {}, "rpg.battle-tactics"));
            const std::string targetType =
                skillId.asString().empty() ? "enemySingle" : SkillRegistry::find(skillId.asString())->targetType;
            if (!policyMatchesTargetType(parsedPolicy, targetType))
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "targetPolicy does not match the skill target type",
                    rulePath + ".targetPolicy", {}, "rpg.battle-tactics"));
            if (!conditionResource.isString() || !validId(conditionResource.asString(), true) ||
                !belowRatio.isNumber() || !std::isfinite(belowRatio.asDouble()) || belowRatio.asDouble() < 0.0 ||
                belowRatio.asDouble() > 1.0)
                return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "conditionResource and belowRatio are invalid",
                                                                        rulePath, {}, "rpg.battle-tactics"));
            if (parsedPolicy == BattleTargetPolicy::Auto && !conditionResource.asString().empty())
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "conditional rules require an explicit target policy",
                    rulePath, {}, "rpg.battle-tactics"));
            BattleTacticRule parsed;
            parsed.skillId           = skillId.asString();
            parsed.targetPolicy      = parsedPolicy;
            parsed.conditionResource = conditionResource.asString();
            parsed.belowRatio        = belowRatio.asDouble();
            definition.rules.push_back(std::move(parsed));
        }
        if (!definition.rules.back().conditionResource.empty())
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "the final battle tactic rule must be unconditional",
                path + ".rules", {}, "rpg.battle-tactics"));
        proposed.emplace(definition.id, std::move(definition));
    }
    definitions() = std::move(proposed);
    return eve::Result<int>::success(static_cast<int>(definitions().size()));
}

void BattleTacticsCatalogue::clear() { definitions().clear(); }
int  BattleTacticsCatalogue::count() { return static_cast<int>(definitions().size()); }

eve::Result<std::string> BattleTacticsCatalogue::queueAction(Battle *battle, RPGActor *actor,
                                                             const std::string &tacticsId) {
    if (!battle || !actor)
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "battle and actor must not be null", "battle",
                                                                        {}, "rpg.battle-tactics"));
    const auto found = definitions().find(tacticsId);
    if (found == definitions().end())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                        "battle tactics id is not registered",
                                                                        "tacticsId", {}, "rpg.battle-tactics"));
    for (const auto &rule : found->second.rules) {
        if (!rule.conditionResource.empty()) {
            const int side = battle->sideOf(actor);
            RPGActor *target =
                rule.targetPolicy == BattleTargetPolicy::Self
                    ? actor
                    : battle->lowestHealthTarget(side, rule.targetPolicy == BattleTargetPolicy::LowestHealthAlly);
            if (!target) continue;
            const double maximum = target->getMax(rule.conditionResource);
            if (maximum <= 0.0 || target->getCurrent(rule.conditionResource) / maximum >= rule.belowRatio) continue;
        }
        auto queued = battle->setActionByPolicyChecked(actor, rule.skillId, rule.targetPolicy);
        if (!queued.ok()) return eve::Result<std::string>::failure(queued.status());
        return eve::Result<std::string>::success(rule.skillId);
    }
    return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                    "battle tactics has no applicable default rule",
                                                                    "rules", {}, "rpg.battle-tactics"));
}

}  // namespace eve::rpg

// BattleVictory.cpp
namespace eve::rpg {
namespace {

eve::Result<BattleVictoryReceipt> victoryFailure(eve::DiagnosticCode code, std::string message, std::string path,
                                                 const BattleVictoryRequest &request) {
    return eve::Result<BattleVictoryReceipt>::failure(eve::Diagnostic::error(
        code, message, path, {{"mapId", request.mapId}, {"objectId", request.objectId}}, "rpg.battle-victory"));
}

struct TrackerRollback {
    std::unordered_map<std::string, QuestRuntime> entries;
    std::vector<std::string>                      order;
    std::vector<QuestEvent>                       pending;
    std::vector<QuestEvent>                       polled;
};

void restoreTracker(Tracker &tracker, TrackerRollback rollback) {
    tracker.entries = std::move(rollback.entries);
    tracker.order   = std::move(rollback.order);
    tracker.pending = std::move(rollback.pending);
    tracker.polled  = std::move(rollback.polled);
}

bool finiteAddition(const GameState &state, const std::string &id, double amount) {
    return id.empty() ? amount == 0.0 : std::isfinite(amount) && std::isfinite(state.getVariable(id) + amount);
}

}  // namespace

eve::Result<BattleVictoryReceipt> BattleVictory::settle(RPGActor &actor, GameState &gameState, Tracker &tracker,
                                                        const BattleVictoryRequest &request) {
    if (request.requiredQuestId.empty() || tracker.getState(request.requiredQuestId) != "active")
        return victoryFailure(eve::DiagnosticCode::PreconditionViolation,
                              "required quest must be active before victory settlement", "requiredQuestId", request);
    if (request.notifyTopic.empty() || request.notifyTarget.empty() || request.notifyAmount <= 0)
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "victory quest notification must be complete and positive", "notification", request);
    if (request.attributeId.empty() || !std::isfinite(request.attributeAmount) || request.attributeAmount < 0.0 ||
        !finiteAddition(gameState, request.attributeId, request.attributeAmount))
        return victoryFailure(eve::DiagnosticCode::InvalidArgument, "victory attribute reward is invalid or non-finite",
                              "attribute", request);
    if (request.defeatCounterId.empty() || request.defeatCounterAmount <= 0 ||
        !finiteAddition(gameState, request.defeatCounterId, static_cast<double>(request.defeatCounterAmount)))
        return victoryFailure(eve::DiagnosticCode::InvalidArgument, "victory counter increment is invalid",
                              "defeatCounter", request);
    if (request.levelPointAttributeId.empty() || request.pointsPerLevel < 0)
        return victoryFailure(eve::DiagnosticCode::InvalidArgument, "level-point reward configuration is invalid",
                              "levelPoints", request);

    auto preparedXp = LevelSystem::prepareGainXp(&actor, request.xpAmount, request.xpGrowth);
    if (!preparedXp.ok()) return eve::Result<BattleVictoryReceipt>::failure(preparedXp.status());
    PreparedProgressionGain xp           = std::move(preparedXp).takeValue();
    const int               levelsGained = xp.levelsGained();
    const double            levelPoints  = static_cast<double>(levelsGained) * request.pointsPerLevel;
    if (!finiteAddition(gameState, request.levelPointAttributeId, levelPoints))
        return victoryFailure(eve::DiagnosticCode::InvalidArgument,
                              "level-point reward would produce a non-finite value", "levelPoints", request);

    auto gameStateBefore = gameState.snapshotJson();
    if (!gameStateBefore.ok()) return eve::Result<BattleVictoryReceipt>::failure(gameStateBefore.status());
    std::string gameStateSnapshot = std::move(gameStateBefore).takeValue();
    GameState   candidateState;
    auto        candidateRestore = candidateState.restoreSnapshotJson(gameStateSnapshot);
    if (!candidateRestore.ok()) return eve::Result<BattleVictoryReceipt>::failure(candidateRestore.status());
    WorldState candidateWorld(candidateState);
    if (candidateWorld.isObjectConsumed(request.mapId, request.objectId))
        return victoryFailure(eve::DiagnosticCode::Conflict, "encounter was already consumed", "objectId", request);
    auto candidateConsume = candidateWorld.consumeObject(request.mapId, request.objectId);
    if (!candidateConsume.ok()) return eve::Result<BattleVictoryReceipt>::failure(candidateConsume.status());

    TrackerRollback trackerBefore{tracker.entries, tracker.order, tracker.pending, tracker.polled};
    gameState.addVariable(request.attributeId, request.attributeAmount);
    gameState.addVariable(request.defeatCounterId, request.defeatCounterAmount);
    if (levelPoints > 0.0) gameState.addVariable(request.levelPointAttributeId, levelPoints);
    tracker.notify(request.notifyTopic, request.notifyTarget, request.notifyAmount);
    WorldState world(gameState);
    auto       consumed = world.consumeObject(request.mapId, request.objectId);
    if (!consumed.ok() || consumed.code() != eve::StatusCode::Applied) {
        auto       restored   = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        restoreTracker(tracker, std::move(trackerBefore));
        if (!restoredOk)
            return victoryFailure(eve::DiagnosticCode::InvariantViolation,
                                  "victory rollback could not restore GameState", "rollback", request);
        return victoryFailure(eve::DiagnosticCode::Conflict, "encounter changed before victory commit", "objectId",
                              request);
    }

    auto committedXp = LevelSystem::commitGainXp(std::move(xp));
    if (!committedXp.ok()) {
        auto       restored   = gameState.restoreSnapshotJson(gameStateSnapshot);
        const bool restoredOk = restored.ok();
        restoreTracker(tracker, std::move(trackerBefore));
        if (!restoredOk)
            return victoryFailure(eve::DiagnosticCode::InvariantViolation,
                                  "victory rollback could not restore GameState", "rollback", request);
        return eve::Result<BattleVictoryReceipt>::failure(committedXp.status());
    }

    BattleVictoryReceipt receipt;
    receipt.levelsGained  = std::move(committedXp).takeValue();
    receipt.skillsLearned = ClassSystem::checkLevelSkills(&actor);
    return eve::Result<BattleVictoryReceipt>::success(std::move(receipt),
                                                      eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
