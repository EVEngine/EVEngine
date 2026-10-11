#include "rpg/RpgDialect.h"

#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "dnut_interpreter/StepKindRegistry.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace eve::rpg {

namespace {

using eve::dnut::SequenceConditionOutcome;
using eve::dnut::SequenceNode;
using eve::dnut::StepContext;
using eve::dnut::StepField;
using eve::dnut::StepFieldType;
using eve::dnut::StepHandler;
using eve::dnut::StepKindDescriptor;
using eve::dnut::StepKindRegistry;
using eve::dnut::StepOutcome;
using eve::dnut::StepShape;
using eve::dnut::StepStatus;

constexpr double kNumberEpsilon = 1e-9;

StepOutcome completed() { return StepOutcome{}; }

StepOutcome failed(std::string message) {
    StepOutcome outcome;
    outcome.status = StepStatus::Failed;
    outcome.error  = std::move(message);
    return outcome;
}

const eve::Value* field(const SequenceNode& node, const char* name) { return node.payload.find(name); }

std::string stringField(const SequenceNode& node, const char* name) {
    const eve::Value* value = node.payload.find(name);
    return value && value->isString() ? value->asString() : std::string{};
}

double numberField(const SequenceNode& node, const char* name, double fallback) {
    const eve::Value* value = node.payload.find(name);
    if (!value || !value->isNumeric()) return fallback;
    return value->isInt64() ? static_cast<double>(value->asInt()) : value->asDouble();
}

std::int64_t integerField(const SequenceNode& node, const char* name, std::int64_t fallback) {
    const eve::Value* value = node.payload.find(name);
    if (!value || !value->isInt64()) return fallback;
    return value->asInt();
}

bool boolField(const SequenceNode& node, const char* name, bool fallback) {
    const eve::Value* value = node.payload.find(name);
    if (!value || !value->isBool()) return fallback;
    return value->asBool();
}

eve::Result<void> descriptorFailure(std::string message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "rpg.dnut-story"));
}

/** @brief Build a validator requiring exactly one of `names` to be set. */
std::function<eve::Result<void>(const SequenceNode&)> exactlyOneOf(std::vector<std::string> names,
                                                                  std::string                stepType) {
    return [names = std::move(names), stepType = std::move(stepType)](const SequenceNode& node) -> eve::Result<void> {
        int present = 0;
        for (const auto& name : names) {
            if (const eve::Value* value = node.payload.find(name); value && !value->isNull()) ++present;
        }
        if (present == 1) return eve::Result<void>::success();
        std::string expected;
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (index != 0) expected += " or ";
            expected += "'" + names[index] + "'";
        }
        return descriptorFailure("step '" + stepType + "' requires exactly one of " + expected);
    };
}

/** @brief Build a validator restricting a string field to a fixed vocabulary. */
std::function<eve::Result<void>(const SequenceNode&)> oneOfValues(std::string              fieldName,
                                                                  std::vector<std::string> allowed,
                                                                  std::string              stepType) {
    return [fieldName = std::move(fieldName), allowed = std::move(allowed),
            stepType = std::move(stepType)](const SequenceNode& node) -> eve::Result<void> {
        const eve::Value* value = node.payload.find(fieldName);
        if (!value || !value->isString()) return eve::Result<void>::success();
        for (const auto& candidate : allowed) {
            if (value->asString() == candidate) return eve::Result<void>::success();
        }
        std::string accepted;
        for (std::size_t index = 0; index < allowed.size(); ++index) {
            if (index != 0) accepted += ", ";
            accepted += allowed[index];
        }
        return descriptorFailure("step '" + stepType + "' field '" + fieldName + "' must be one of " + accepted);
    };
}

/** @brief Resolve `actorId` through the binding's custom resolver or its party. */
RPGActor* resolveActor(const RpgStoryBinding* binding, const std::string& actorId) {
    if (!binding) return nullptr;
    if (binding->resolveActor) return binding->resolveActor(actorId);
    if (!binding->party || actorId.empty()) return nullptr;
    return binding->party->findMemberActor(actorId);
}

// --- domain step handlers --------------------------------------------------------------------

StepOutcome applySkill(const SequenceNode& node, const StepContext& context) {
    auto*             binding = static_cast<RpgStoryBinding*>(context.host);
    const std::string actorId = stringField(node, "actor");
    RPGActor*         actor   = resolveActor(binding, actorId);
    if (!actor) return failed("skill step: actor '" + actorId + "' cannot be resolved");
    if (const eve::Value* learn = field(node, "learn"); learn && learn->isString()) {
        actor->learnSkill(learn->asString());
        return completed();
    }
    const eve::Value* forget = field(node, "forget");
    if (!forget || !forget->isString()) return failed("skill step: neither learn nor forget is set");
    if (!actor->forgetSkill(forget->asString()))
        return failed("skill step: actor '" + actorId + "' does not know skill '" + forget->asString() + "'");
    return completed();
}

StepOutcome applyAttribute(const SequenceNode& node, const StepContext& context) {
    auto*             binding = static_cast<RpgStoryBinding*>(context.host);
    const std::string actorId = stringField(node, "actor");
    RPGActor*         actor   = resolveActor(binding, actorId);
    if (!actor) return failed("attribute step: actor '" + actorId + "' cannot be resolved");
    const std::string name  = stringField(node, "name");
    const std::string op    = stringField(node, "op");
    const double      value = numberField(node, "value", 0.0);
    if (op == "set") {
        actor->setBaseAttribute(name, value);
    } else if (op == "add") {
        actor->modifyBaseAttribute(name, value);
    } else if (op == "sub") {
        actor->modifyBaseAttribute(name, -value);
    } else if (op == "mul") {
        actor->setBaseAttribute(name, actor->getBaseAttribute(name) * value);
    } else {
        return failed("attribute step: unsupported op '" + op + "'");
    }
    return completed();
}

StepOutcome applyItem(const SequenceNode& node, const StepContext& context) {
    auto* binding = static_cast<RpgStoryBinding*>(context.host);
    if (!binding || !binding->bag) return failed("item step: no bag is bound");
    const std::int64_t requested = integerField(node, "count", 1);
    if (requested < 1) return failed("item step: count must be at least 1");
    const auto quantity = static_cast<int>(requested);

    if (const eve::Value* add = field(node, "add"); add && add->isString()) {
        if (!binding->bag->canAddItem(add->asString(), quantity)) {
            const std::string reason = binding->bag->canAddItemReason(add->asString(), quantity);
            return failed("item step: bag refuses '" + add->asString() + "' (" +
                          (reason.empty() ? std::string("no capacity") : reason) + ")");
        }
        if (binding->bag->addItem(add->asString(), quantity) != quantity)
            return failed("item step: bag did not accept the whole stack of '" + add->asString() + "'");
        return completed();
    }
    const eve::Value* remove = field(node, "remove");
    if (!remove || !remove->isString()) return failed("item step: neither add nor remove is set");
    if (binding->bag->countItem(remove->asString()) < quantity)
        return failed("item step: bag does not hold " + std::to_string(quantity) + " of '" + remove->asString() +
                      "'");
    if (binding->bag->removeItem(remove->asString(), quantity) != quantity)
        return failed("item step: bag did not remove the whole stack of '" + remove->asString() + "'");
    return completed();
}

StepOutcome applyEquipment(const SequenceNode& node, const StepContext& context) {
    auto* binding = static_cast<RpgStoryBinding*>(context.host);
    if (!binding || !binding->equipment) return failed("equipment step: no equipment set is bound");
    if (const eve::Value* equip = field(node, "equip"); equip && equip->isString()) {
        if (!binding->bag) return failed("equipment step: equipping requires a bound bag");
        const std::string slotName = stringField(node, "slot");
        const int         bagSlot  = binding->bag->findItem(equip->asString());
        if (bagSlot < 0) return failed("equipment step: bag does not hold '" + equip->asString() + "'");
        if (!binding->equipment->equipFromBag(slotName, binding->bag, bagSlot))
            return failed("equipment step: cannot equip '" + equip->asString() + "' into slot '" + slotName + "'");
        return completed();
    }
    const eve::Value* unequip = field(node, "unequip");
    if (!unequip || !unequip->isString()) return failed("equipment step: neither equip nor unequip is set");
    if (!binding->bag) return failed("equipment step: unequipping requires a bound bag");
    if (!binding->equipment->unequipToBag(unequip->asString(), binding->bag))
        return failed("equipment step: cannot unequip slot '" + unequip->asString() + "'");
    return completed();
}

StepOutcome applyFlag(const SequenceNode& node, const StepContext& context) {
    auto* binding = static_cast<RpgStoryBinding*>(context.host);
    if (!binding || !binding->gameState) return failed("flag step: no game state is bound");
    binding->gameState->setSwitch(stringField(node, "name"), boolField(node, "state", true));
    return completed();
}

StepOutcome applyVariable(const SequenceNode& node, const StepContext& context) {
    auto* binding = static_cast<RpgStoryBinding*>(context.host);
    if (!binding || !binding->gameState) return failed("variable step: no game state is bound");
    const std::string name  = stringField(node, "name");
    const std::string op    = stringField(node, "op");
    const double      value = numberField(node, "value", 0.0);
    if (op == "set") {
        binding->gameState->setVariable(name, value);
    } else if (op == "add") {
        binding->gameState->addVariable(name, value);
    } else {
        return failed("variable step: unsupported op '" + op + "'");
    }
    return completed();
}

// --- movement controller ---------------------------------------------------------------------

/** @brief Render a coordinate without trailing zeros, for diagnostics. */
std::string formatCoordinate(double value) {
    if (std::fabs(value - std::round(value)) <= kNumberEpsilon)
        return std::to_string(static_cast<long long>(std::llround(value)));
    return std::to_string(value);
}

/**
 * @brief Route a `move` step to the host movement controller, or present it.
 *
 * Without a controller the handler returns `Blocked` without doing anything,
 * which is observationally identical to the runtime's own host-presented
 * suspension: the story suspends, the host reads the payload and acknowledges
 * with `advance`. A binding that installs no controller therefore behaves
 * exactly as it did before a controller could be installed.
 */
StepOutcome performMove(const SequenceNode& node, const StepContext& context) {
    auto* binding = static_cast<RpgStoryBinding*>(context.host);
    if (!binding || !binding->moveActor) {
        StepOutcome presented;
        presented.status = StepStatus::Blocked;
        return presented;
    }

    StoryMoveRequest request;
    request.actorId = stringField(node, "actor");
    request.actor   = resolveActor(binding, request.actorId);
    request.x       = numberField(node, "x", 0.0);
    request.y       = numberField(node, "y", 0.0);
    if (const eve::Value* duration = field(node, "duration"); duration && duration->isNumeric()) {
        request.hasDuration = true;
        request.duration    = numberField(node, "duration", 0.0);
    }

    auto started = binding->moveActor(request);
    if (!started.ok()) {
        const auto* diagnostic = started.error();
        return failed(diagnostic ? diagnostic->message() : "move step: the movement controller failed");
    }
    switch (started.value()) {
        case StoryMoveStatus::Arrived: return completed();
        case StoryMoveStatus::Unreachable:
            return failed("move step: actor '" + request.actorId + "' cannot reach (" + formatCoordinate(request.x) +
                          ", " + formatCoordinate(request.y) + ")");
        case StoryMoveStatus::Travelling: break;
    }
    StepOutcome travelling;
    travelling.status = StepStatus::Blocked;
    return travelling;
}

// --- animation controller --------------------------------------------------------------------

/**
 * @brief Route an `animation` step to the host playback controller, or present it.
 *
 * Without a controller the handler returns `Blocked` without doing anything,
 * which is observationally identical to the runtime's own host-presented
 * suspension: the story suspends, the host reads the payload and acknowledges
 * with `advance`. A binding that installs no controller therefore behaves
 * exactly as it did before a controller could be installed.
 *
 * `clip` is passed through verbatim: resolving it would have to pick a rig, a
 * clip format and a playback model, which is precisely the game-mode assumption
 * the port exists to avoid.
 */
StepOutcome performAnimation(const SequenceNode& node, const StepContext& context) {
    auto* binding = static_cast<RpgStoryBinding*>(context.host);
    if (!binding || !binding->playAnimation) {
        StepOutcome presented;
        presented.status = StepStatus::Blocked;
        return presented;
    }

    StoryAnimationRequest request;
    request.targetId = stringField(node, "target");
    request.actor    = resolveActor(binding, request.targetId);
    request.clip     = stringField(node, "clip");
    request.loop     = boolField(node, "loop", false);
    request.hold     = boolField(node, "hold", false);

    auto started = binding->playAnimation(request);
    if (!started.ok()) {
        const auto* diagnostic = started.error();
        return failed(diagnostic ? diagnostic->message() : "animation step: the playback controller failed");
    }
    switch (started.value()) {
        case StoryAnimationStatus::Finished: return completed();
        case StoryAnimationStatus::Unavailable:
            return failed("animation step: target '" + request.targetId + "' cannot play clip '" + request.clip + "'");
        case StoryAnimationStatus::Playing: break;
    }
    StepOutcome playing;
    playing.status = StepStatus::Blocked;
    return playing;
}

// --- condition evaluation --------------------------------------------------------------------

struct ResolvedState {
    bool   found  = false;
    double number = 0.0;
};

ResolvedState resolveStateName(const std::string& name, const RpgStoryBinding* binding) {
    ResolvedState resolved;
    if (!binding || !binding->gameState) return resolved;
    if (name.rfind("switch.", 0) == 0) {
        resolved.found  = true;
        resolved.number = binding->gameState->isSwitchOn(name.substr(7)) ? 1.0 : 0.0;
        return resolved;
    }
    if (name.rfind("variable.", 0) == 0) {
        resolved.found  = true;
        resolved.number = binding->gameState->getVariable(name.substr(9));
        return resolved;
    }
    if (name.rfind("self.", 0) == 0) {
        const std::string rest = name.substr(5);
        const auto        dot  = rest.rfind('.');
        if (dot == std::string::npos) return resolved;
        const std::string scope = rest.substr(0, dot);
        const std::string key   = rest.substr(dot + 1);
        if (!binding->gameState->hasSelfVariable(scope, key)) return resolved;
        resolved.found  = true;
        resolved.number = binding->gameState->getSelfVariable(scope, key);
        return resolved;
    }
    return resolved;
}

bool compareNumbers(double left, double right, const std::string& op) {
    if (op == "eq") return std::fabs(left - right) <= kNumberEpsilon;
    if (op == "ne") return std::fabs(left - right) > kNumberEpsilon;
    if (op == "gt") return left > right + kNumberEpsilon;
    if (op == "lt") return left < right - kNumberEpsilon;
    if (op == "ge") return left >= right - kNumberEpsilon;
    if (op == "le") return left <= right + kNumberEpsilon;
    return false;
}

SequenceConditionOutcome evaluateCondition(const eve::Value& condition, const RpgStoryBinding* binding) {
    if (!condition.isObject()) return {false, "invalid-condition"};

    if (const eve::Value* all = condition.find("all"); all && all->isArray()) {
        for (std::size_t index = 0; index < all->arraySize(); ++index) {
            if (!evaluateCondition(all->at(index), binding).passed) return {false, "child-failed"};
        }
        return {true, {}};
    }
    if (const eve::Value* any = condition.find("any"); any && any->isArray()) {
        for (std::size_t index = 0; index < any->arraySize(); ++index) {
            if (evaluateCondition(any->at(index), binding).passed) return {true, {}};
        }
        return {false, "no-child-passed"};
    }
    if (const eve::Value* negated = condition.find("not"); negated) {
        return evaluateCondition(*negated, binding).passed ? SequenceConditionOutcome{false, "negated"}
                                                           : SequenceConditionOutcome{true, {}};
    }

    const eve::Value* variable  = condition.find("var");
    const eve::Value* operation = condition.find("op");
    const eve::Value* expected  = condition.find("value");
    if (!variable || !variable->isString() || !operation || !operation->isString() || !expected)
        return {false, "invalid-condition"};

    const ResolvedState resolved = resolveStateName(variable->asString(), binding);
    if (!resolved.found) return {false, "missing-value"};

    double expectedNumber = 0.0;
    if (expected->isBool()) {
        expectedNumber = expected->asBool() ? 1.0 : 0.0;
    } else if (expected->isNumeric()) {
        expectedNumber = expected->isInt64() ? static_cast<double>(expected->asInt()) : expected->asDouble();
    } else {
        return {false, "unsupported-operand"};
    }
    if (compareNumbers(resolved.number, expectedNumber, operation->asString())) return {true, {}};
    return {false, "value-mismatch"};
}

// --- vocabulary ------------------------------------------------------------------------------

StepField requiredField(std::string name, StepFieldType type) { return StepField{std::move(name), type, true}; }
StepField optionalField(std::string name, StepFieldType type) { return StepField{std::move(name), type, false}; }

std::function<eve::Result<void>(const SequenceNode&)> equipRequiresSlot() {
    return [](const SequenceNode& node) -> eve::Result<void> {
        if (const eve::Value* equip = node.payload.find("equip"); equip && !equip->isNull()) {
            const eve::Value* slot = node.payload.find("slot");
            if (!slot || !slot->isString() || slot->asString().empty())
                return descriptorFailure("step 'equipment' requires 'slot' when 'equip' is set");
        }
        return eve::Result<void>::success();
    };
}

StepKindRegistry buildRpgStoryRegistry() {
    StepKindRegistry registry;
    const auto        declare = [&registry](StepKindDescriptor descriptor) {
        registry.registerStep(std::move(descriptor)).expect("RPG story step descriptor must register");
    };
    const auto bind = [&registry](const char* type, StepHandler handler) {
        registry.registerHandler(type, std::move(handler)).expect("RPG story step handler must bind");
    };

    // `move` carries a handler so a host can install a movement controller. With
    // no controller bound the handler presents the step itself, so both paths
    // suspend the story and acknowledge with advance().
    declare({"move", "Move an actor", "presentation", StepShape::Await,
             {requiredField("actor", StepFieldType::String), requiredField("x", StepFieldType::Number),
              requiredField("y", StepFieldType::Number), optionalField("duration", StepFieldType::Number)}});
    bind("move", performMove);

    // `animation` carries a handler for the same reason `move` does: a host can
    // install a playback controller and stop branching on the step kind. With no
    // controller bound the handler presents the step itself, so it takes the
    // same host-presented path as the steps below.
    declare({"animation", "Play an animation clip", "presentation", StepShape::Await,
             {requiredField("target", StepFieldType::String), requiredField("clip", StepFieldType::String),
              optionalField("loop", StepFieldType::Boolean), optionalField("hold", StepFieldType::Boolean)}});
    bind("animation", performAnimation);

    // Host-presented steps: registered without a handler, so the runtime suspends
    // and the host presents the payload before acknowledging with advance().
    declare({"select", "Select a world object", "presentation", StepShape::Await,
             {requiredField("object", StepFieldType::String), optionalField("prompt", StepFieldType::String)}});
    declare({"camera", "Move the camera", "presentation", StepShape::Await,
             {requiredField("x", StepFieldType::Number), requiredField("y", StepFieldType::Number),
              optionalField("duration", StepFieldType::Number)}});
    declare({"dialogue", "Play a conversation", "dialogue", StepShape::Await,
             {requiredField("id", StepFieldType::String)}});
    declare({"message", "Show a message", "dialogue", StepShape::Await,
             {requiredField("text", StepFieldType::String)}});

    StepKindDescriptor skill;
    skill.type        = "skill";
    skill.displayName = "Learn or forget a skill";
    skill.category    = "progression";
    skill.shape       = StepShape::Instant;
    skill.fields      = {requiredField("actor", StepFieldType::String), optionalField("learn", StepFieldType::String),
                    optionalField("forget", StepFieldType::String)};
    skill.validate    = exactlyOneOf({"learn", "forget"}, "skill");
    declare(std::move(skill));
    bind("skill", applySkill);

    StepKindDescriptor attribute;
    attribute.type        = "attribute";
    attribute.displayName = "Modify a base attribute";
    attribute.category    = "progression";
    attribute.shape       = StepShape::Instant;
    attribute.fields      = {requiredField("actor", StepFieldType::String), requiredField("name", StepFieldType::String),
                        requiredField("op", StepFieldType::String), requiredField("value", StepFieldType::Number)};
    attribute.validate    = oneOfValues("op", {"set", "add", "sub", "mul"}, "attribute");
    declare(std::move(attribute));
    bind("attribute", applyAttribute);

    StepKindDescriptor item;
    item.type        = "item";
    item.displayName = "Add or remove items";
    item.category    = "inventory";
    item.shape       = StepShape::Instant;
    item.fields      = {optionalField("add", StepFieldType::String), optionalField("remove", StepFieldType::String),
                   optionalField("count", StepFieldType::Integer)};
    item.validate    = exactlyOneOf({"add", "remove"}, "item");
    declare(std::move(item));
    bind("item", applyItem);

    StepKindDescriptor equipment;
    equipment.type        = "equipment";
    equipment.displayName = "Equip or unequip an item";
    equipment.category    = "inventory";
    equipment.shape       = StepShape::Instant;
    equipment.fields      = {requiredField("actor", StepFieldType::String), optionalField("equip", StepFieldType::String),
                        optionalField("unequip", StepFieldType::String), optionalField("slot", StepFieldType::String)};
    equipment.validate    = [](const SequenceNode& node) -> eve::Result<void> {
        auto exclusive = exactlyOneOf({"equip", "unequip"}, "equipment")(node);
        if (!exclusive.ok()) return exclusive;
        return equipRequiresSlot()(node);
    };
    declare(std::move(equipment));
    bind("equipment", applyEquipment);

    StepKindDescriptor flag;
    flag.type        = "flag";
    flag.displayName = "Set a story switch";
    flag.category    = "state";
    flag.shape       = StepShape::Instant;
    flag.fields      = {requiredField("name", StepFieldType::String), requiredField("state", StepFieldType::Boolean)};
    declare(std::move(flag));
    bind("flag", applyFlag);

    StepKindDescriptor variable;
    variable.type        = "variable";
    variable.displayName = "Set or add a story variable";
    variable.category    = "state";
    variable.shape       = StepShape::Instant;
    variable.fields      = {requiredField("name", StepFieldType::String), requiredField("op", StepFieldType::String),
                       requiredField("value", StepFieldType::Number)};
    variable.validate    = oneOfValues("op", {"set", "add"}, "variable");
    declare(std::move(variable));
    bind("variable", applyVariable);

    return registry;
}

std::map<std::string, eve::dnut::SequenceAsset>& storyCatalogue() {
    static std::map<std::string, eve::dnut::SequenceAsset> catalogue;
    return catalogue;
}

eve::Result<void> makeStoryFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "rpg.dnut-story"));
}

std::string completionScope(const std::string& storyId) { return "story." + storyId; }

}  // namespace

StepKindRegistry& rpgStoryStepRegistry() {
    static StepKindRegistry registry = buildRpgStoryRegistry();
    return registry;
}

eve::Result<int> RpgStoryCatalogue::replaceFromDnutStrict(const std::string& source, const std::string& path) {
    eve::dnut::DnutCompileOutput output = eve::dnut::compileDnut(source, path, rpgStoryStepRegistry());
    if (output.hasErrors()) {
        std::string message = "story document contains errors";
        for (const auto& diagnostic : output.diagnostics) {
            if (diagnostic.severity == eve::dnut::DnutSeverity::Error) {
                message = diagnostic.message;
                break;
            }
        }
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message), path, {}, "rpg.dnut-story"));
    }
    if (output.assets.empty())
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "document declares no 'story' block", path, {}, "rpg.dnut-story"));

    std::map<std::string, eve::dnut::SequenceAsset> proposed;
    for (auto& asset : output.assets) {
        const std::string id = asset.id;
        proposed.emplace(id, std::move(asset));
    }
    storyCatalogue() = std::move(proposed);
    return eve::Result<int>::success(static_cast<int>(storyCatalogue().size()));
}

void RpgStoryCatalogue::clear() { storyCatalogue().clear(); }

int RpgStoryCatalogue::count() { return static_cast<int>(storyCatalogue().size()); }

bool RpgStoryCatalogue::contains(const std::string& storyId) { return storyCatalogue().contains(storyId); }

std::vector<std::string> RpgStoryCatalogue::storyIds() {
    std::vector<std::string> ids;
    ids.reserve(storyCatalogue().size());
    for (const auto& entry : storyCatalogue()) ids.push_back(entry.first);
    return ids;
}

class RpgStorySession::Impl {
public:
    std::map<std::string, eve::dnut::SequenceAsset> assets;
    eve::dnut::SequenceRuntime                      runtime;
    RpgStoryBinding                                 ownedBinding;
    RpgStoryBinding*                                binding = nullptr;
    std::string                                     storyId;
    eve::Value                                      emptyPayload = eve::Value::Object{};

    /** @brief Install the resolver, vocabulary, host binding and condition evaluator. */
    void configureRuntime() {
        Impl* self = this;
        runtime.setAssetResolver([self](const std::string& id) -> const eve::dnut::SequenceAsset* {
            const auto found = self->assets.find(id);
            return found == self->assets.end() ? nullptr : &found->second;
        });
        runtime.setStepRegistry(&rpgStoryStepRegistry());
        runtime.setHostContext(self->binding);
        runtime.setConditionEvaluator(
            [self](const eve::Value& condition) { return evaluateCondition(condition, self->binding); });
    }

    /** @brief Snapshot the whole catalogue so nothing borrows it after a hot replacement. */
    [[nodiscard]] bool snapshot(const std::string& id) {
        assets.clear();
        for (const auto& entry : storyCatalogue()) assets.emplace(entry.first, entry.second);
        return assets.contains(id);
    }

    /** @brief Record completion once a successful run leaves the runtime inactive. */
    void publishCompletion() {
        if (runtime.isActive()) return;
        if (!binding || !binding->gameState || storyId.empty()) return;
        binding->gameState->setSelfVariable(completionScope(storyId), "completed", 1.0);
    }
};

RpgStorySession::RpgStorySession() : impl_(std::make_unique<Impl>()) {}
RpgStorySession::~RpgStorySession()                                        = default;
RpgStorySession::RpgStorySession(RpgStorySession&&) noexcept                = default;
RpgStorySession& RpgStorySession::operator=(RpgStorySession&&) noexcept     = default;

eve::Result<void> RpgStorySession::begin(const std::string& storyId, GameState* gameState, Party* party,
                                         eve::inventory::Bag* bag, eve::inventory::EquipmentSet* equipment) {
    impl_->ownedBinding               = RpgStoryBinding{};
    impl_->ownedBinding.gameState     = gameState;
    impl_->ownedBinding.party         = party;
    impl_->ownedBinding.bag           = bag;
    impl_->ownedBinding.equipment     = equipment;
    return begin(storyId, &impl_->ownedBinding);
}

eve::Result<void> RpgStorySession::begin(const std::string& storyId, RpgStoryBinding* binding) {
    stop();
    impl_->binding = binding;
    if (!impl_->snapshot(storyId))
        return makeStoryFailure(eve::DiagnosticCode::NotFound, "story '" + storyId + "' is not published", "storyId");

    if (binding && binding->gameState) {
        const std::string scope = completionScope(storyId);
        const bool        hasCompleted = binding->gameState->hasSelfVariable(scope, "completed");
        const double      completed    = hasCompleted ? binding->gameState->getSelfVariable(scope, "completed") : 0.0;
        if (hasCompleted && completed != 0.0 && completed != 1.0)
            return makeStoryFailure(eve::DiagnosticCode::InvariantViolation,
                                "persisted story-completion flag is invalid", scope + ".completed");
        if (completed == 1.0) {
            const auto definition = impl_->assets.find(storyId);
            if (definition != impl_->assets.end() && !definition->second.repeatable)
                return makeStoryFailure(eve::DiagnosticCode::Conflict,
                                    "non-repeatable story '" + storyId + "' is already complete", "storyId");
            binding->gameState->setSelfVariable(scope, "completed", 0.0);
        }
    }

    impl_->storyId = storyId;
    impl_->configureRuntime();
    const auto definition = impl_->assets.find(storyId);
    if (definition == impl_->assets.end())
        return makeStoryFailure(eve::DiagnosticCode::NotFound, "story '" + storyId + "' is not published", "storyId");
    if (auto started = impl_->runtime.start(&definition->second); !started.ok()) {
        const auto* diagnostic = started.error();
        const std::string message = diagnostic ? diagnostic->message() : "story could not start";
        stop();
        return makeStoryFailure(eve::DiagnosticCode::Failed, message, "storyId");
    }
    impl_->publishCompletion();
    return eve::Result<void>::success();
}

eve::Result<void> RpgStorySession::advance() {
    if (!impl_->runtime.isActive())
        return makeStoryFailure(eve::DiagnosticCode::PreconditionViolation, "story session is not active", "advance");
    if (auto result = impl_->runtime.advance(); !result.ok()) {
        const auto* diagnostic = result.error();
        return makeStoryFailure(eve::DiagnosticCode::Failed,
                            diagnostic ? diagnostic->message() : "story could not advance", "advance");
    }
    impl_->publishCompletion();
    return eve::Result<void>::success();
}

eve::Result<void> RpgStorySession::select(const std::string& routeLabel) {
    if (!impl_->runtime.isActive())
        return makeStoryFailure(eve::DiagnosticCode::PreconditionViolation, "story session is not active", "select");
    if (auto result = impl_->runtime.select(routeLabel); !result.ok()) {
        const auto* diagnostic = result.error();
        return makeStoryFailure(eve::DiagnosticCode::Failed,
                            diagnostic ? diagnostic->message() : "choice could not be selected", "select");
    }
    impl_->publishCompletion();
    return eve::Result<void>::success();
}

eve::Result<void> RpgStorySession::resume(eve::Value result) {
    if (!impl_->runtime.isActive())
        return makeStoryFailure(eve::DiagnosticCode::PreconditionViolation, "story session is not active", "resume");
    if (auto applied = impl_->runtime.resumeStep(std::move(result)); !applied.ok()) {
        const auto* diagnostic = applied.error();
        return makeStoryFailure(eve::DiagnosticCode::Failed,
                            diagnostic ? diagnostic->message() : "story could not resume", "resume");
    }
    impl_->publishCompletion();
    return eve::Result<void>::success();
}

void RpgStorySession::stop() {
    impl_->runtime.stop();
    impl_->assets.clear();
    impl_->storyId.clear();
    impl_->binding = nullptr;
}

bool RpgStorySession::isActive() const { return impl_->runtime.isActive(); }
bool RpgStorySession::isBlocked() const { return impl_->runtime.isBlocked(); }
bool RpgStorySession::isWaitingStep() const { return impl_->runtime.isWaitingStep(); }
std::string RpgStorySession::getStoryId() const { return impl_->storyId; }

std::string RpgStorySession::getStepKind() const {
    const eve::dnut::SequenceNode* node = impl_->runtime.currentNode();
    return node ? node->type : std::string{};
}

const eve::Value& RpgStorySession::getStepPayload() const {
    const eve::dnut::SequenceNode* node = impl_->runtime.currentNode();
    return node ? node->payload : impl_->emptyPayload;
}

std::vector<std::string> RpgStorySession::getChoiceLabels() const {
    std::vector<std::string> labels;
    const eve::dnut::SequenceNode* node = impl_->runtime.currentNode();
    if (!node || node->type != "choice") return labels;
    labels.reserve(node->routes.size());
    for (const auto& route : node->routes) labels.push_back(route.label);
    return labels;
}

eve::Result<void> RpgStorySession::captureState(eve::Value& out) const { return impl_->runtime.captureState(out); }

eve::Result<void> RpgStorySession::restoreState(const std::string& storyId, const eve::Value& in,
                                                RpgStoryBinding* binding) {
    stop();
    impl_->binding = binding;
    if (!impl_->snapshot(storyId))
        return makeStoryFailure(eve::DiagnosticCode::NotFound, "story '" + storyId + "' is not published", "storyId");
    impl_->storyId = storyId;
    impl_->configureRuntime();
    if (auto restored = impl_->runtime.restoreState(in); !restored.ok()) {
        const auto* diagnostic = restored.error();
        const std::string message = diagnostic ? diagnostic->message() : "story cursor could not be restored";
        stop();
        return makeStoryFailure(eve::DiagnosticCode::Failed, message, "storyId");
    }
    return eve::Result<void>::success();
}

}  // namespace eve::rpg
