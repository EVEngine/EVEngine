#include "rpg/SkillConditionCodec.h"

#include <set>
#include <string>
#include <utility>

namespace eve::rpg {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message),
                                                          std::move(path), {}, "rpg.skill.condition_codec"));
}

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
        return invalid<std::string>("condition field must be a non-empty string", std::string(name));
    return eve::Result<std::string>::success(*text);
}

eve::Result<eve::decision::ConditionKind> parseKind(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr) return invalid<eve::decision::ConditionKind>("condition kind must be a string", "kind");
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
    return invalid<K>("unknown condition kind", "kind");
}

eve::Result<eve::decision::CompareOperator> parseOperator(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return invalid<eve::decision::CompareOperator>("condition operator must be a string", "operator");
    using O                                             = eve::decision::CompareOperator;
    static const std::pair<std::string_view, O> names[] = {
        {"eq", O::Equal},     {"ne", O::NotEqual}, {"lt", O::Less},
        {"le", O::LessEqual}, {"gt", O::Greater},  {"ge", O::GreaterEqual},
    };
    for (const auto& [name, op] : names)
        if (*text == name) return eve::Result<O>::success(op);
    return invalid<O>("unknown condition operator", "operator");
}

eve::Result<eve::decision::DeterminismLevel> parseDeterminism(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return invalid<eve::decision::DeterminismLevel>("determinism must be a string",
                                                        "scriptDeclaration.determinism");
    using D                                             = eve::decision::DeterminismLevel;
    static const std::pair<std::string_view, D> names[] = {
        {"bit_exact", D::BitExact},
        {"tick_deterministic", D::TickDeterministic},
        {"tolerance_bounded", D::ToleranceBounded},
        {"explicitly_nondeterministic", D::ExplicitlyNondeterministic},
    };
    for (const auto& [name, level] : names)
        if (*text == name) return eve::Result<D>::success(level);
    return invalid<D>("unknown condition determinism level", "scriptDeclaration.determinism");
}

eve::Result<eve::decision::Condition> decodeNode(const eve::Value& value);

eve::Result<std::vector<eve::decision::Condition>> decodeChildren(const eve::Value::Object& object) {
    const auto* value = field(object, "children");
    const auto* array = value == nullptr ? nullptr : value->getIf<eve::Value::Array>();
    if (array == nullptr)
        return invalid<std::vector<eve::decision::Condition>>("condition children must be an array", "children");
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
    if (object == nullptr) return invalid<eve::decision::Condition>("condition node must be an object");
    const auto* kindValue = field(*object, "kind");
    if (kindValue == nullptr) return invalid<eve::decision::Condition>("condition node requires kind", "kind");
    auto kind = parseKind(*kindValue);
    if (!kind) return eve::Result<eve::decision::Condition>::failure(kind.status());
    const auto parsedKind = kind.value();
    using K               = eve::decision::ConditionKind;
    if (parsedKind == K::All || parsedKind == K::Any || parsedKind == K::Not) {
        if (!exactFields(*object, {"kind", "children"}))
            return invalid<eve::decision::Condition>("logical condition contains an unknown field");
        auto children = decodeChildren(*object);
        if (!children) return eve::Result<eve::decision::Condition>::failure(children.status());
        auto values = std::move(children).takeValue();
        if (parsedKind == K::Not && values.size() != 1)
            return invalid<eve::decision::Condition>("not condition requires one child", "children");
        if (parsedKind == K::All)
            return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::all(std::move(values)));
        if (parsedKind == K::Any)
            return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::any(std::move(values)));
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::not_(std::move(values.front())));
    }
    if (parsedKind == K::Compare) {
        if (!exactFields(*object, {"kind", "key", "operator", "expected"}))
            return invalid<eve::decision::Condition>("compare condition contains an unknown field");
        auto        key      = requiredString(*object, "key");
        const auto* op       = field(*object, "operator");
        const auto* expected = field(*object, "expected");
        if (!key || op == nullptr || expected == nullptr)
            return invalid<eve::decision::Condition>("compare condition is incomplete");
        auto parsedOp = parseOperator(*op);
        if (!parsedOp) return eve::Result<eve::decision::Condition>::failure(parsedOp.status());
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::compare(std::move(key).takeValue(), parsedOp.value(), *expected));
    }
    if (parsedKind == K::StateEquals) {
        if (!exactFields(*object, {"kind", "key", "expected"}))
            return invalid<eve::decision::Condition>("state condition contains an unknown field");
        auto        key      = requiredString(*object, "key");
        const auto* expected = field(*object, "expected");
        if (!key || expected == nullptr) return invalid<eve::decision::Condition>("state condition is incomplete");
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::stateEquals(std::move(key).takeValue(), *expected));
    }
    if (parsedKind == K::PolicyCall) {
        if (!exactFields(*object, {"kind", "key", "arguments", "scriptDeclaration"}))
            return invalid<eve::decision::Condition>("policy condition contains an unknown field");
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<eve::decision::Condition>::failure(key.status());
        eve::Value arguments = eve::Value::Object{};
        if (const auto* value = field(*object, "arguments")) arguments = *value;
        std::optional<eve::decision::ScriptConditionDeclaration> declaration;
        if (const auto* value = field(*object, "scriptDeclaration")) {
            const auto* declarationObject = value->getIf<eve::Value::Object>();
            if (declarationObject == nullptr ||
                !exactFields(*declarationObject, {"name", "dependencies", "determinism"}))
                return invalid<eve::decision::Condition>("script declaration is invalid", "scriptDeclaration");
            auto        name            = requiredString(*declarationObject, "name");
            const auto* dependencyValue = field(*declarationObject, "dependencies");
            const auto* dependencies =
                dependencyValue == nullptr ? nullptr : dependencyValue->getIf<eve::Value::Array>();
            const auto* determinism = field(*declarationObject, "determinism");
            if (!name || dependencies == nullptr || determinism == nullptr)
                return invalid<eve::decision::Condition>("script declaration is incomplete", "scriptDeclaration");
            auto level = parseDeterminism(*determinism);
            if (!level) return eve::Result<eve::decision::Condition>::failure(level.status());
            eve::decision::ScriptConditionDeclaration parsed{std::move(name).takeValue(), {}, level.value()};
            for (const auto& dependency : *dependencies) {
                const auto* text = dependency.getIf<std::string>();
                if (text == nullptr || text->empty())
                    return invalid<eve::decision::Condition>("script dependency is invalid",
                                                             "scriptDeclaration.dependencies");
                parsed.dependencies.push_back(*text);
            }
            declaration = std::move(parsed);
        }
        return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::policyCall(
            std::move(key).takeValue(), std::move(arguments), std::move(declaration)));
    }
    if (!exactFields(*object, {"kind", "key"}))
        return invalid<eve::decision::Condition>("leaf condition contains an unknown field");
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
        default: return invalid<eve::decision::Condition>("unsupported condition node kind");
    }
}

}  // namespace

eve::Result<eve::Value> encodeSkillCondition(const eve::decision::Condition& condition) {
    if (!condition.isValid()) return invalid<eve::Value>("skill condition is invalid");
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
