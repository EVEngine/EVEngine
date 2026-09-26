#include "emergence/ConditionCodec.h"

#include "common/Diagnostic.h"

#include <optional>
#include <set>
#include <string>
#include <utility>

namespace eve::emergence {
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
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition field must be a non-empty string", std::string(name), {},
            "emergence.condition_codec"));
    return eve::Result<std::string>::success(*text);
}

eve::Result<decision::ConditionKind> parseKind(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<decision::ConditionKind>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition kind must be a string", "kind", {},
            "emergence.condition_codec"));
    using K                                             = decision::ConditionKind;
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
        eve::DiagnosticCode::InvalidArgument, "unknown condition kind", "kind", {}, "emergence.condition_codec"));
}

eve::Result<decision::CompareOperator> parseOperator(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<decision::CompareOperator>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition operator must be a string", "operator", {},
            "emergence.condition_codec"));
    using O                                             = decision::CompareOperator;
    static const std::pair<std::string_view, O> names[] = {
        {"eq", O::Equal},     {"ne", O::NotEqual}, {"lt", O::Less},
        {"le", O::LessEqual}, {"gt", O::Greater},  {"ge", O::GreaterEqual},
    };
    for (const auto& [name, op] : names)
        if (*text == name) return eve::Result<O>::success(op);
    return eve::Result<O>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unknown condition operator", "operator", {},
        "emergence.condition_codec"));
}

eve::Result<decision::DeterminismLevel> parseDeterminism(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<decision::DeterminismLevel>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "determinism must be a string", "scriptDeclaration.determinism", {},
            "emergence.condition_codec"));
    using D                                             = decision::DeterminismLevel;
    static const std::pair<std::string_view, D> names[] = {
        {"bit_exact", D::BitExact},
        {"tick_deterministic", D::TickDeterministic},
        {"tolerance_bounded", D::ToleranceBounded},
        {"explicitly_nondeterministic", D::ExplicitlyNondeterministic},
    };
    for (const auto& [name, level] : names)
        if (*text == name) return eve::Result<D>::success(level);
    return eve::Result<D>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unknown condition determinism level", "scriptDeclaration.determinism",
        {}, "emergence.condition_codec"));
}

eve::Result<decision::Condition> decodeNode(const eve::Value& value);

eve::Result<std::vector<decision::Condition>> decodeChildren(const eve::Value::Object& object) {
    const auto* value = field(object, "children");
    const auto* array = value == nullptr ? nullptr : value->getIf<eve::Value::Array>();
    if (array == nullptr)
        return eve::Result<std::vector<decision::Condition>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition children must be an array", "children", {},
            "emergence.condition_codec"));
    std::vector<decision::Condition> result;
    result.reserve(array->size());
    for (const auto& child : *array) {
        auto decoded = decodeNode(child);
        if (!decoded) return eve::Result<std::vector<decision::Condition>>::failure(decoded.status());
        result.push_back(std::move(decoded).takeValue());
    }
    return eve::Result<std::vector<decision::Condition>>::success(std::move(result));
}

eve::Result<decision::Condition> decodeNode(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr)
        return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition node must be an object", {}, {},
            "emergence.condition_codec"));
    const auto* kindValue = field(*object, "kind");
    if (kindValue == nullptr)
        return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition node requires kind", "kind", {},
            "emergence.condition_codec"));
    auto kind = parseKind(*kindValue);
    if (!kind) return eve::Result<decision::Condition>::failure(kind.status());
    const auto parsedKind = kind.value();
    using K               = decision::ConditionKind;
    if (parsedKind == K::All || parsedKind == K::Any || parsedKind == K::Not) {
        if (!exactFields(*object, {"kind", "children"}))
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "logical condition contains an unknown field", {}, {},
                "emergence.condition_codec"));
        auto children = decodeChildren(*object);
        if (!children) return eve::Result<decision::Condition>::failure(children.status());
        auto values = std::move(children).takeValue();
        if (parsedKind == K::Not && values.size() != 1)
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "not condition requires one child", "children", {},
                "emergence.condition_codec"));
        if (parsedKind == K::All)
            return eve::Result<decision::Condition>::success(decision::Condition::all(std::move(values)));
        if (parsedKind == K::Any)
            return eve::Result<decision::Condition>::success(decision::Condition::any(std::move(values)));
        return eve::Result<decision::Condition>::success(decision::Condition::not_(std::move(values.front())));
    }
    if (parsedKind == K::Compare) {
        if (!exactFields(*object, {"kind", "key", "operator", "expected"}))
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "compare condition contains an unknown field", {}, {},
                "emergence.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<decision::Condition>::failure(key.status());
        const auto* opValue = field(*object, "operator");
        if (opValue == nullptr)
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "compare requires operator", "operator", {},
                "emergence.condition_codec"));
        auto op = parseOperator(*opValue);
        if (!op) return eve::Result<decision::Condition>::failure(op.status());
        const auto* expected = field(*object, "expected");
        if (expected == nullptr)
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "compare requires expected", "expected", {},
                "emergence.condition_codec"));
        return eve::Result<decision::Condition>::success(
            decision::Condition::compare(std::move(key).takeValue(), op.value(), *expected));
    }
    if (parsedKind == K::HasTag || parsedKind == K::HasAttribute || parsedKind == K::HasResource ||
        parsedKind == K::AuthorityCheck) {
        if (!exactFields(*object, {"kind", "key"}))
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "leaf condition contains an unknown field", {}, {},
                "emergence.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<decision::Condition>::failure(key.status());
        auto name = std::move(key).takeValue();
        if (parsedKind == K::HasTag) return eve::Result<decision::Condition>::success(decision::Condition::hasTag(std::move(name)));
        if (parsedKind == K::HasAttribute)
            return eve::Result<decision::Condition>::success(decision::Condition::hasAttribute(std::move(name)));
        if (parsedKind == K::HasResource)
            return eve::Result<decision::Condition>::success(decision::Condition::hasResource(std::move(name)));
        return eve::Result<decision::Condition>::success(decision::Condition::authorityCheck(std::move(name)));
    }
    if (parsedKind == K::StateEquals) {
        if (!exactFields(*object, {"kind", "key", "expected"}))
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "state_equals contains an unknown field", {}, {},
                "emergence.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<decision::Condition>::failure(key.status());
        const auto* expected = field(*object, "expected");
        if (expected == nullptr)
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "state_equals requires expected", "expected", {},
                "emergence.condition_codec"));
        return eve::Result<decision::Condition>::success(
            decision::Condition::stateEquals(std::move(key).takeValue(), *expected));
    }
    if (parsedKind == K::PolicyCall) {
        if (!exactFields(*object, {"kind", "key", "arguments", "scriptDeclaration"}))
            return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "policy_call contains an unknown field", {}, {},
                "emergence.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<decision::Condition>::failure(key.status());
        eve::Value arguments = eve::Value::Object{};
        if (const auto* args = field(*object, "arguments")) arguments = *args;
        std::optional<decision::ScriptConditionDeclaration> declaration;
        if (const auto* declValue = field(*object, "scriptDeclaration")) {
            const auto* declObject = declValue->getIf<eve::Value::Object>();
            if (declObject == nullptr)
                return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "scriptDeclaration must be an object", "scriptDeclaration",
                    {}, "emergence.condition_codec"));
            decision::ScriptConditionDeclaration parsed;
            auto name = requiredString(*declObject, "name");
            if (!name) return eve::Result<decision::Condition>::failure(name.status());
            parsed.name = std::move(name).takeValue();
            if (const auto* deps = field(*declObject, "dependencies")) {
                const auto* array = deps->getIf<eve::Value::Array>();
                if (array == nullptr)
                    return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "dependencies must be an array",
                        "scriptDeclaration.dependencies", {}, "emergence.condition_codec"));
                for (const auto& item : *array) {
                    const auto* text = item.getIf<std::string>();
                    if (text == nullptr || text->empty())
                        return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "dependency must be a non-empty string",
                            "scriptDeclaration.dependencies", {}, "emergence.condition_codec"));
                    parsed.dependencies.push_back(*text);
                }
            }
            if (const auto* det = field(*declObject, "determinism")) {
                auto level = parseDeterminism(*det);
                if (!level) return eve::Result<decision::Condition>::failure(level.status());
                parsed.determinism = level.value();
            }
            declaration = std::move(parsed);
        }
        return eve::Result<decision::Condition>::success(
            decision::Condition::policyCall(std::move(key).takeValue(), std::move(arguments), std::move(declaration)));
    }
    return eve::Result<decision::Condition>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unsupported condition kind", "kind", {}, "emergence.condition_codec"));
}

}  // namespace

eve::Result<decision::Condition> decodeCondition(const eve::Value& value) { return decodeNode(value); }

}  // namespace eve::emergence
