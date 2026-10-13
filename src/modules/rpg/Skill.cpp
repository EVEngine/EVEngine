#include "rpg/Skill.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include "common/Json.h"
#include "common/Value.h"
#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillAction.h"
#include "rpg/SkillCondition.h"
#include "rpg/SkillConditionCodec.h"

// Skill.cpp
namespace eve::rpg {

using eve::json::Value;

namespace {

eve::definitions::DefinitionRegistry& canonicalSkillRegistry() {
    static eve::definitions::DefinitionRegistry registry;
    return registry;
}

eve::Result<std::string> canonicalJson(const SkillDefinition& def) {
    eve::Value::Object object;
    object.emplace("id", eve::Value(def.id));
    object.emplace("cooldown", eve::Value(static_cast<double>(def.cooldown)));
    object.emplace("castTime", eve::Value(static_cast<double>(def.castTime)));
    object.emplace("targetType", eve::Value(def.targetType));

    auto condition = encodeSkillCondition(def.castCondition);
    if (!condition) return eve::Result<std::string>::failure(condition.status());
    object.emplace("castCondition", std::move(condition).takeValue());

    eve::Value::Array effects;
    for (const auto& effect : def.grantedEffects) effects.emplace_back(effect);
    object.emplace("grantedEffects", eve::Value(std::move(effects)));

    eve::Value::Array tags;
    for (const auto& tag : def.tags) tags.emplace_back(tag);
    object.emplace("tags", eve::Value(std::move(tags)));

    eve::Value::Object extra;
    for (const auto& [key, value] : def.extra) extra.emplace(key, eve::Value(value));
    object.emplace("extra", eve::Value(std::move(extra)));

    eve::Value::Array costs;
    if (def.cost) {
        for (const auto& item : def.cost->items()) {
            costs.emplace_back(eve::Value(eve::Value::Object{
                {"attribute", eve::Value(item.resource.value())},
                {"amount", eve::Value(item.amount.value())},
            }));
        }
    }
    object.emplace("costs", eve::Value(std::move(costs)));
    return eve::Value(std::move(object)).toJson();
}

}  // namespace

bool SkillDefinition::hasTag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::unordered_map<std::string, SkillDefinition>& table() {
    static std::unordered_map<std::string, SkillDefinition> t;
    return t;
}

eve::definitions::DefinitionRegistry& SkillRegistry::definitionRegistry() { return canonicalSkillRegistry(); }

void SkillRegistry::registerSkill(const SkillDefinition& def) {
    if (def.id.empty()) return;
    auto json = canonicalJson(def);
    if (!json) {
        json.ignore("legacy skill registration cannot serialize invalid definition");
        return;
    }
    auto&                                          registry = canonicalSkillRegistry();
    auto                                           existing = registry.resolve("rpg.skill", def.id);
    eve::Result<eve::definition::DefinitionHandle> result =
        existing.ok() ? registry.replace("rpg.skill", def.id, 1, std::move(json).takeValue())
        : existing.status().code() == eve::StatusCode::NotFound
            ? registry.insert("rpg.skill", def.id, 1, std::move(json).takeValue())
            : eve::Result<eve::definition::DefinitionHandle>::failure(existing.status());
    if (!result) return;
    table()[def.id] = def;
}

const SkillDefinition* SkillRegistry::find(const std::string& id) {
    auto& t  = table();
    auto  it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool SkillRegistry::remove(const std::string& id) {
    auto& registry = canonicalSkillRegistry();
    auto  result   = registry.remove("rpg.skill", id);
    if (!result) return false;
    return table().erase(id) > 0;
}

void SkillRegistry::clear() {
    auto& skills   = table();
    auto& registry = canonicalSkillRegistry();
    for (const auto& [id, unused] : skills) {
        (void)unused;
        auto result = registry.remove("rpg.skill", id);
        result.ignore("legacy SkillRegistry::clear removes canonical skill definitions");
    }
    skills.clear();
}

int SkillRegistry::count() { return int(table().size()); }

namespace {

SkillDefinition parseSkillObject(Value o, std::string* error) {
    SkillDefinition def;
    if (!o.isObject()) return def;
    def.id             = o.getString("id");
    def.cooldown       = o.getFloat("cooldown", 0.f);
    def.castTime       = o.getFloat("castTime", 0.f);
    def.targetType     = o.getString("targetType", "self");
    def.grantedEffects = o.getStringArray("grantedEffects");
    def.tags           = o.getStringArray("tags");

    const Value                              costs = o.get("costs");
    std::vector<eve::resource::ResourceCost> costItems;
    for (size_t i = 0; i < costs.size(); ++i) {
        const Value       co       = costs.at(i);
        const std::string resource = co.getString("attribute");
        const double      amount   = co.getDouble("amount", 0.0);
        // Keep the legacy JSON shape, but do not silently truncate a value
        // into the canonical integral resource Amount type.
        if (resource.empty()) continue;
        if (!std::isfinite(amount) || amount <= 0.0 || std::floor(amount) != amount || amount >= std::ldexp(1.0, 63)) {
            if (error) *error = "skill cost must be a finite positive integer: " + resource;
            return {};
        }
        auto item = eve::resource::ResourceCost::create(resource, static_cast<std::int64_t>(amount));
        if (!item) {
            if (error) *error = item.status().describe();
            return {};
        }
        costItems.push_back(std::move(item).takeValue());
    }
    if (!costItems.empty()) {
        auto canonical = eve::resource::CostSpec::create(std::move(costItems));
        if (!canonical) {
            if (error) *error = canonical.status().describe();
            return {};
        }
        def.cost = std::move(canonical).takeValue();
    }

    def.extra = o.getStringMap("extra");
    return def;
}

}  // namespace

int SkillRegistry::loadFromJson(const std::string& json, std::string* error) {
    std::string               err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }

    const Value root = doc.root();
    int         n    = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) {
            SkillDefinition def = parseSkillObject(root.at(i), error);
            if (def.id.empty()) continue;
            registerSkill(def);
            ++n;
        }
    } else if (root.isObject()) {
        SkillDefinition def = parseSkillObject(root, error);
        if (!def.id.empty()) {
            registerSkill(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::rpg

// SkillAction.cpp
namespace eve::rpg {
namespace {

eve::Result<eve::LogicalId> skillActionId(const SkillDefinition& skill) {
    if (skill.id.empty())
        return eve::Result<eve::LogicalId>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill id must not be empty", "skill.id"));
    const auto id = eve::LogicalId::fromParts("rpg", "skill." + skill.id);
    if (!id)
        return eve::Result<eve::LogicalId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill id cannot form a valid action logical id", "skill.id"));
    return eve::Result<eve::LogicalId>::success(*id);
}

eve::Result<eve::Duration> seconds(float value, const char* path) {
    auto duration = eve::Duration::fromSeconds(static_cast<double>(value));
    if (!duration) {
        const eve::Status status = duration.status();
        (void)status;
        return eve::Result<eve::Duration>::failure(duration.status());
    }
    if (duration.value().nanoseconds() < 0)
        return eve::Result<eve::Duration>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill action duration must be non-negative", path));
    return duration;
}

}  // namespace

eve::Result<action::ActionDefinition> SkillActionAdapter::makeDefinition(const SkillDefinition& skill) {
    auto id = skillActionId(skill);
    if (!id) return eve::Result<action::ActionDefinition>::failure(id.status());
    auto windup = seconds(skill.castTime, "skill.castTime");
    if (!windup) return eve::Result<action::ActionDefinition>::failure(windup.status());
    auto cooldown = seconds(skill.cooldown, "skill.cooldown");
    if (!cooldown) return eve::Result<action::ActionDefinition>::failure(cooldown.status());

    action::ActionDefinition definition;
    definition.id            = std::move(id).takeValue();
    definition.timing.windup = std::move(windup).takeValue();
    definition.condition     = skill.castCondition;
    definition.targetingMode =
        skill.targetType == "self" ? action::TargetingMode::None : action::TargetingMode::Explicit;
    definition.cost      = skill.cost;
    definition.effectIds = skill.grantedEffects;
    // A skill is an Active operation even when it currently has no effect id;
    // the injected executor may emit a settlement/event for that operation.
    definition.activeExecutionRequired = true;
    definition.metadata.emplace("cooldown", eve::Value(cooldown.value().seconds()));
    definition.metadata.emplace("targetType", eve::Value(skill.targetType));
    definition.metadata.emplace("skillId", eve::Value(skill.id));

    auto valid = definition.validate();
    if (!valid) return eve::Result<action::ActionDefinition>::failure(valid.status());
    return eve::Result<action::ActionDefinition>::success(std::move(definition));
}

eve::Result<action::ActionRequest> SkillActionAdapter::makeRequest(const SkillDefinition&           skill,
                                                                   std::optional<ecs::EntityHandle> source,
                                                                   std::optional<ecs::EntityHandle> target,
                                                                   eve::SimulationTick              requestedTick) {
    auto definition = makeDefinition(skill);
    if (!definition) return eve::Result<action::ActionRequest>::failure(definition.status());
    auto checkedDefinition = std::move(definition).takeValue();

    if (checkedDefinition.targetingMode == action::TargetingMode::None && target)
        return eve::Result<action::ActionRequest>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "self skill request cannot carry an explicit target", "target"));
    if (checkedDefinition.targetingMode == action::TargetingMode::Explicit && !target)
        return eve::Result<action::ActionRequest>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "non-self skill request requires an explicit target", "target"));

    action::ActionRequest request;
    request.actionId      = checkedDefinition.id;
    request.source        = std::move(source);
    request.requestedTick = requestedTick;
    if (target) request.targetEntities.push_back(*target);
    auto valid = request.validate(checkedDefinition);
    if (!valid) return eve::Result<action::ActionRequest>::failure(valid.status());
    return eve::Result<action::ActionRequest>::success(std::move(request));
}

eve::Result<action::ActionExecutionId> SkillActionAdapter::submit(action::ActionRuntime&           runtime,
                                                                  const SkillDefinition&           skill,
                                                                  std::optional<ecs::EntityHandle> source,
                                                                  std::optional<ecs::EntityHandle> target,
                                                                  eve::SimulationTick              requestedTick) {
    auto definition = makeDefinition(skill);
    if (!definition) return eve::Result<action::ActionExecutionId>::failure(definition.status());
    auto request = makeRequest(skill, std::move(source), std::move(target), requestedTick);
    if (!request) return eve::Result<action::ActionExecutionId>::failure(request.status());
    return runtime.submit(std::move(definition).takeValue(), std::move(request).takeValue());
}

}  // namespace eve::rpg

// SkillCondition.cpp
namespace eve::rpg {
namespace {

std::optional<eve::Value> skillValue(const RPGActor* actor, const SkillDefinition& definition, std::string_view key) {
    if (!actor) return std::nullopt;
    auto*       mutableActor = const_cast<RPGActor*>(actor);
    const auto* runtime      = [&]() -> const SkillRuntime* {
        const auto it = mutableActor->skills()->known.find(definition.id);
        return it == mutableActor->skills()->known.end() ? nullptr : &it->second;
    }();
    if (key == "skill.id") return eve::Value(definition.id);
    if (key == "skill.targetType") return eve::Value(definition.targetType);
    if (key == "skill.known") return eve::Value(runtime != nullptr);
    if (key == "skill.casting") return eve::Value(mutableActor->skills()->casting.active);
    if (key == "skill.cooldown") return eve::Value(runtime ? runtime->cooldownRemaining : 0.0);
    if (key == "skill.phase") return eve::Value(mutableActor->skills()->casting.active ? "casting" : "idle");
    return std::nullopt;
}

}  // namespace

SkillConditionContext::SkillConditionContext(const RPGActor* actor, const SkillDefinition& definition,
                                             SkillConditionQueries queries)
    : actor_(actor), definition_(&definition), queries_(std::move(queries)) {}

std::optional<eve::Value> SkillConditionContext::value(std::string_view key) const {
    return skillValue(actor_, *definition_, key);
}

std::optional<bool> SkillConditionContext::hasTag(std::string_view tag) const {
    if (!actor_) return std::nullopt;
    if (definition_->hasTag(std::string(tag))) return true;
    return const_cast<RPGActor*>(actor_)->hasStatusTag(std::string(tag));
}

std::optional<eve::Value> SkillConditionContext::attribute(std::string_view key) const {
    if (!actor_ || key.empty()) return std::nullopt;
    auto* mutableActor = const_cast<RPGActor*>(actor_);
    if (!AttributeSystem::hasAttribute(mutableActor, std::string(key))) return std::nullopt;
    return eve::Value(AttributeSystem::getFinal(mutableActor, std::string(key)));
}

std::optional<eve::Value> SkillConditionContext::resource(std::string_view key) const {
    // RPG resources are attributes by design (mana, stamina, and game-defined
    // resources use the same canonical AttributeSet source).
    return attribute(key);
}

std::optional<eve::Value> SkillConditionContext::state(std::string_view key) const { return value(key); }

std::optional<bool> SkillConditionContext::authority(std::string_view scope) const {
    if (!queries_.authority) return std::nullopt;
    return queries_.authority(scope);
}

std::optional<decision::ConditionResult> SkillConditionContext::policy(std::string_view  name,
                                                                       const eve::Value& arguments) const {
    if (!queries_.policy) return std::nullopt;
    return queries_.policy(name, arguments);
}

decision::ConditionResult SkillConditionAdapter::evaluate(const RPGActor* actor, const SkillDefinition& definition,
                                                          const decision::Condition& condition,
                                                          SkillConditionQueries      queries) {
    SkillConditionContext context(actor, definition, std::move(queries));
    return condition.evaluate(context);
}

}  // namespace eve::rpg

// SkillConditionCodec.cpp
namespace eve::rpg {
namespace {

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

bool exactFields(const eve::Value::Object& object, std::initializer_list<std::string_view> allowed) {
    const std::set<std::string_view> fields(allowed.begin(), allowed.end());
    for (const auto& [name, unused] : object) {
        (void)unused;
        if (!fields.contains(name)) return false;
    }
    return true;
}

eve::Result<std::string> requiredString(const eve::Value::Object& object, std::string_view name) {
    const auto* value = field(object, name);
    const auto* text  = value == nullptr ? nullptr : value->getIf<std::string>();
    if (text == nullptr || text->empty())
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition field must be a non-empty string",
                                   std::string(name), {}, "rpg.skill.condition_codec"));
    return eve::Result<std::string>::success(*text);
}

eve::Result<eve::decision::ConditionKind> parseKind(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<eve::decision::ConditionKind>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition kind must be a string", "kind", {},
                                   "rpg.skill.condition_codec"));
    using K                                             = eve::decision::ConditionKind;
    static const std::pair<std::string_view, K> names[] = {
        {"all", K::All},
        {"any", K::Any},
        {"not", K::Not},
        {"compare", K::Compare},
        {"has_tag", K::HasTag},
        {"has_attribute", K::HasAttribute},
        {"has_resource", K::HasResource},
        {"state_equals", K::StateEquals},
        {"authority_check", K::AuthorityCheck},
        {"policy_call", K::PolicyCall},
    };
    for (const auto& [name, kind] : names)
        if (*text == name) return eve::Result<K>::success(kind);
    return eve::Result<K>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unknown condition kind", "kind", {}, "rpg.skill.condition_codec"));
}

eve::Result<eve::decision::CompareOperator> parseOperator(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<eve::decision::CompareOperator>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition operator must be a string",
                                   "operator", {}, "rpg.skill.condition_codec"));
    using O                                             = eve::decision::CompareOperator;
    static const std::pair<std::string_view, O> names[] = {
        {"eq", O::Equal},     {"ne", O::NotEqual}, {"lt", O::Less},
        {"le", O::LessEqual}, {"gt", O::Greater},  {"ge", O::GreaterEqual},
    };
    for (const auto& [name, op] : names)
        if (*text == name) return eve::Result<O>::success(op);
    return eve::Result<O>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                          "unknown condition operator", "operator", {},
                                                          "rpg.skill.condition_codec"));
}

eve::Result<eve::decision::DeterminismLevel> parseDeterminism(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<eve::decision::DeterminismLevel>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "determinism must be a string",
                                   "scriptDeclaration.determinism", {}, "rpg.skill.condition_codec"));
    using D                                             = eve::decision::DeterminismLevel;
    static const std::pair<std::string_view, D> names[] = {
        {"bit_exact", D::BitExact},
        {"tick_deterministic", D::TickDeterministic},
        {"tolerance_bounded", D::ToleranceBounded},
        {"explicitly_nondeterministic", D::ExplicitlyNondeterministic},
    };
    for (const auto& [name, level] : names)
        if (*text == name) return eve::Result<D>::success(level);
    return eve::Result<D>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown condition determinism level",
                               "scriptDeclaration.determinism", {}, "rpg.skill.condition_codec"));
}

eve::Result<eve::decision::Condition> decodeNode(const eve::Value& value);

eve::Result<std::vector<eve::decision::Condition>> decodeChildren(const eve::Value::Object& object) {
    const auto* value = field(object, "children");
    const auto* array = value == nullptr ? nullptr : value->getIf<eve::Value::Array>();
    if (array == nullptr)
        return eve::Result<std::vector<eve::decision::Condition>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition children must be an array",
                                   "children", {}, "rpg.skill.condition_codec"));
    std::vector<eve::decision::Condition> result;
    result.reserve(array->size());
    for (const auto& child : *array) {
        auto decoded = decodeNode(child);
        if (!decoded) return eve::Result<std::vector<eve::decision::Condition>>::failure(decoded.status());
        result.push_back(std::move(decoded).takeValue());
    }
    return eve::Result<std::vector<eve::decision::Condition>>::success(std::move(result));
}

eve::Result<eve::decision::Condition> decodeNode(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr)
        return eve::Result<eve::decision::Condition>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition node must be an object", {}, {},
                                   "rpg.skill.condition_codec"));
    const auto* kindValue = field(*object, "kind");
    if (kindValue == nullptr)
        return eve::Result<eve::decision::Condition>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition node requires kind", "kind", {},
                                   "rpg.skill.condition_codec"));
    auto kind = parseKind(*kindValue);
    if (!kind) return eve::Result<eve::decision::Condition>::failure(kind.status());
    const auto parsedKind = kind.value();
    using K               = eve::decision::ConditionKind;
    if (parsedKind == K::All || parsedKind == K::Any || parsedKind == K::Not) {
        if (!exactFields(*object, {"kind", "children"}))
            return eve::Result<eve::decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "logical condition contains an unknown field", {}, {},
                "rpg.skill.condition_codec"));
        auto children = decodeChildren(*object);
        if (!children) return eve::Result<eve::decision::Condition>::failure(children.status());
        auto values = std::move(children).takeValue();
        if (parsedKind == K::Not && values.size() != 1)
            return eve::Result<eve::decision::Condition>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "not condition requires one child",
                                       "children", {}, "rpg.skill.condition_codec"));
        if (parsedKind == K::All)
            return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::all(std::move(values)));
        if (parsedKind == K::Any)
            return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::any(std::move(values)));
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::not_(std::move(values.front())));
    }
    if (parsedKind == K::Compare) {
        if (!exactFields(*object, {"kind", "key", "operator", "expected"}))
            return eve::Result<eve::decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "compare condition contains an unknown field", {}, {},
                "rpg.skill.condition_codec"));
        auto        key      = requiredString(*object, "key");
        const auto* op       = field(*object, "operator");
        const auto* expected = field(*object, "expected");
        if (!key || op == nullptr || expected == nullptr)
            return eve::Result<eve::decision::Condition>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "compare condition is incomplete", {}, {},
                                       "rpg.skill.condition_codec"));
        auto parsedOp = parseOperator(*op);
        if (!parsedOp) return eve::Result<eve::decision::Condition>::failure(parsedOp.status());
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::compare(std::move(key).takeValue(), parsedOp.value(), *expected));
    }
    if (parsedKind == K::StateEquals) {
        if (!exactFields(*object, {"kind", "key", "expected"}))
            return eve::Result<eve::decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "state condition contains an unknown field", {}, {},
                "rpg.skill.condition_codec"));
        auto        key      = requiredString(*object, "key");
        const auto* expected = field(*object, "expected");
        if (!key || expected == nullptr)
            return eve::Result<eve::decision::Condition>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "state condition is incomplete", {}, {},
                                       "rpg.skill.condition_codec"));
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::stateEquals(std::move(key).takeValue(), *expected));
    }
    if (parsedKind == K::PolicyCall) {
        if (!exactFields(*object, {"kind", "key", "arguments", "scriptDeclaration"}))
            return eve::Result<eve::decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "policy condition contains an unknown field", {}, {},
                "rpg.skill.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<eve::decision::Condition>::failure(key.status());
        eve::Value arguments = eve::Value::Object{};
        if (const auto* value = field(*object, "arguments")) arguments = *value;
        std::optional<eve::decision::ScriptConditionDeclaration> declaration;
        if (const auto* value = field(*object, "scriptDeclaration")) {
            const auto* declarationObject = value->getIf<eve::Value::Object>();
            if (declarationObject == nullptr ||
                !exactFields(*declarationObject, {"name", "dependencies", "determinism"}))
                return eve::Result<eve::decision::Condition>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "script declaration is invalid",
                                           "scriptDeclaration", {}, "rpg.skill.condition_codec"));
            auto        name            = requiredString(*declarationObject, "name");
            const auto* dependencyValue = field(*declarationObject, "dependencies");
            const auto* dependencies =
                dependencyValue == nullptr ? nullptr : dependencyValue->getIf<eve::Value::Array>();
            const auto* determinism = field(*declarationObject, "determinism");
            if (!name || dependencies == nullptr || determinism == nullptr)
                return eve::Result<eve::decision::Condition>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "script declaration is incomplete",
                                           "scriptDeclaration", {}, "rpg.skill.condition_codec"));
            auto level = parseDeterminism(*determinism);
            if (!level) return eve::Result<eve::decision::Condition>::failure(level.status());
            eve::decision::ScriptConditionDeclaration parsed{std::move(name).takeValue(), {}, level.value()};
            for (const auto& dependency : *dependencies) {
                const auto* text = dependency.getIf<std::string>();
                if (text == nullptr || text->empty())
                    return eve::Result<eve::decision::Condition>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "script dependency is invalid",
                                               "scriptDeclaration.dependencies", {}, "rpg.skill.condition_codec"));
                parsed.dependencies.push_back(*text);
            }
            declaration = std::move(parsed);
        }
        return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::policyCall(
            std::move(key).takeValue(), std::move(arguments), std::move(declaration)));
    }
    if (!exactFields(*object, {"kind", "key"}))
        return eve::Result<eve::decision::Condition>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "leaf condition contains an unknown field", {},
                                   {}, "rpg.skill.condition_codec"));
    auto key = requiredString(*object, "key");
    if (!key) return eve::Result<eve::decision::Condition>::failure(key.status());
    switch (parsedKind) {
        case K::HasTag:
            return eve::Result<eve::decision::Condition>::success(
                eve::decision::Condition::hasTag(std::move(key).takeValue()));
        case K::HasAttribute:
            return eve::Result<eve::decision::Condition>::success(
                eve::decision::Condition::hasAttribute(std::move(key).takeValue()));
        case K::HasResource:
            return eve::Result<eve::decision::Condition>::success(
                eve::decision::Condition::hasResource(std::move(key).takeValue()));
        case K::AuthorityCheck:
            return eve::Result<eve::decision::Condition>::success(
                eve::decision::Condition::authorityCheck(std::move(key).takeValue()));
        default:
            return eve::Result<eve::decision::Condition>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unsupported condition node kind", {}, {},
                                       "rpg.skill.condition_codec"));
    }
}

}  // namespace

eve::Result<eve::Value> encodeSkillCondition(const eve::decision::Condition& condition) {
    if (!condition.isValid())
        return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill condition is invalid", {}, {}, "rpg.skill.condition_codec"));
    using K = eve::decision::ConditionKind;
    eve::Value::Object object{{"kind", eve::Value(eve::decision::conditionKindName(condition.kind()))}};
    if (condition.kind() == K::All || condition.kind() == K::Any || condition.kind() == K::Not) {
        eve::Value::Array children;
        for (const auto& child : condition.children()) {
            auto encoded = encodeSkillCondition(child);
            if (!encoded) return eve::Result<eve::Value>::failure(encoded.status());
            children.push_back(std::move(encoded).takeValue());
        }
        object.emplace("children", eve::Value(std::move(children)));
    } else if (condition.kind() == K::Compare) {
        object.emplace("key", eve::Value(condition.key()));
        object.emplace("operator", eve::Value(eve::decision::compareOperatorName(condition.compareOperator())));
        object.emplace("expected", condition.expected());
    } else if (condition.kind() == K::StateEquals) {
        object.emplace("key", eve::Value(condition.key()));
        object.emplace("expected", condition.expected());
    } else {
        object.emplace("key", eve::Value(condition.key()));
        if (condition.kind() == K::PolicyCall) {
            object.emplace("arguments", condition.arguments());
            if (condition.scriptDeclaration()) {
                const auto&       declaration = *condition.scriptDeclaration();
                eve::Value::Array dependencies;
                for (const auto& dependency : declaration.dependencies) dependencies.emplace_back(dependency);
                std::string determinism;
                switch (declaration.determinism) {
                    case eve::decision::DeterminismLevel::BitExact: determinism = "bit_exact"; break;
                    case eve::decision::DeterminismLevel::TickDeterministic: determinism = "tick_deterministic"; break;
                    case eve::decision::DeterminismLevel::ToleranceBounded: determinism = "tolerance_bounded"; break;
                    case eve::decision::DeterminismLevel::ExplicitlyNondeterministic:
                        determinism = "explicitly_nondeterministic";
                        break;
                }
                object.emplace("scriptDeclaration",
                               eve::Value(eve::Value::Object{{"name", eve::Value(declaration.name)},
                                                             {"dependencies", eve::Value(std::move(dependencies))},
                                                             {"determinism", eve::Value(std::move(determinism))}}));
            }
        }
    }
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<eve::decision::Condition> decodeSkillCondition(const eve::Value& value) { return decodeNode(value); }

}  // namespace eve::rpg
