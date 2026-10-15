#include "condition/ConditionCodec.h"

#include "common/Diagnostic.h"
#include "condition/ConditionKeys.h"

#include <set>
#include <string>
#include <utility>

namespace eve::condition {
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
                                   std::string(name), {}, "decision.condition_codec"));
    return eve::Result<std::string>::success(*text);
}

eve::Result<CompareOperator> parseOperator(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<CompareOperator>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition operator must be a string",
                                   "operator", {}, "decision.condition_codec"));
    static const std::pair<std::string_view, CompareOperator> names[] = {
        {"eq", CompareOperator::Equal},      {"ne", CompareOperator::NotEqual}, {"lt", CompareOperator::Less},
        {"le", CompareOperator::LessEqual},  {"gt", CompareOperator::Greater},  {"ge", CompareOperator::GreaterEqual},
        {"exists", CompareOperator::Exists},
    };
    for (const auto& [name, op] : names)
        if (*text == name) return eve::Result<CompareOperator>::success(op);
    return eve::Result<CompareOperator>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "unknown condition operator", "operator", {},
                                                                        "decision.condition_codec"));
}

eve::Result<DeterminismLevel> parseDeterminism(const eve::Value& value) {
    const auto* text = value.getIf<std::string>();
    if (text == nullptr)
        return eve::Result<DeterminismLevel>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "determinism must be a string",
                                   "scriptDeclaration.determinism", {}, "decision.condition_codec"));
    static const std::pair<std::string_view, DeterminismLevel> names[] = {
        {"bit_exact", DeterminismLevel::BitExact},
        {"tick_deterministic", DeterminismLevel::TickDeterministic},
        {"tolerance_bounded", DeterminismLevel::ToleranceBounded},
        {"explicitly_nondeterministic", DeterminismLevel::ExplicitlyNondeterministic},
    };
    for (const auto& [name, level] : names)
        if (*text == name) return eve::Result<DeterminismLevel>::success(level);
    return eve::Result<DeterminismLevel>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown condition determinism level",
                               "scriptDeclaration.determinism", {}, "decision.condition_codec"));
}

std::string determinismName(DeterminismLevel level) {
    switch (level) {
        case DeterminismLevel::BitExact: return "bit_exact";
        case DeterminismLevel::TickDeterministic: return "tick_deterministic";
        case DeterminismLevel::ToleranceBounded: return "tolerance_bounded";
        case DeterminismLevel::ExplicitlyNondeterministic: return "explicitly_nondeterministic";
    }
    return "bit_exact";
}

eve::Result<Condition> decodeNode(const eve::Value& value);

eve::Result<std::vector<Condition>> decodeChildren(const eve::Value::Object& object) {
    const auto* value = field(object, "children");
    const auto* array = value == nullptr ? nullptr : value->getIf<eve::Value::Array>();
    if (array == nullptr)
        return eve::Result<std::vector<Condition>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "condition children must be an array",
                                   "children", {}, "decision.condition_codec"));
    std::vector<Condition> result;
    result.reserve(array->size());
    for (const auto& child : *array) {
        auto decoded = decodeNode(child);
        if (!decoded) return eve::Result<std::vector<Condition>>::failure(decoded.status());
        result.push_back(std::move(decoded).takeValue());
    }
    return eve::Result<std::vector<Condition>>::success(std::move(result));
}

eve::Result<Condition> decodeLegacyLeaf(std::string_view kind, const eve::Value::Object& object) {
    if (kind == "has_tag" || kind == "has_attribute" || kind == "has_resource" || kind == "authority_check") {
        if (!exactFields(object, {"kind", "key"}))
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "leaf condition contains an unknown field",
                                                                          {}, {}, "decision.condition_codec"));
        auto key = requiredString(object, "key");
        if (!key) return eve::Result<Condition>::failure(key.status());
        auto name = std::move(key).takeValue();
        if (kind == "has_tag") return eve::Result<Condition>::success(tagPresent(std::move(name)));
        if (kind == "has_attribute") return eve::Result<Condition>::success(attributeExists(std::move(name)));
        if (kind == "has_resource") return eve::Result<Condition>::success(resourceExists(std::move(name)));
        return eve::Result<Condition>::success(authorityAllow(std::move(name)));
    }
    if (kind == "state_equals") {
        if (!exactFields(object, {"kind", "key", "expected"}))
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "state_equals contains an unknown field", {},
                                                                          {}, "decision.condition_codec"));
        auto key = requiredString(object, "key");
        if (!key) return eve::Result<Condition>::failure(key.status());
        const auto* expected = field(object, "expected");
        if (expected == nullptr)
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "state_equals requires expected", "expected",
                                                                          {}, "decision.condition_codec"));
        return eve::Result<Condition>::success(stateEquals(std::move(key).takeValue(), *expected));
    }
    return eve::Result<Condition>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unknown condition kind", "kind", {}, "decision.condition_codec"));
}

eve::Result<Condition> decodeNode(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr)
        return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "condition node must be an object", {}, {},
                                                                      "decision.condition_codec"));
    const auto* kindValue = field(*object, "kind");
    const auto* kindText  = kindValue == nullptr ? nullptr : kindValue->getIf<std::string>();
    if (kindText == nullptr || kindText->empty())
        return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "condition node requires kind", "kind", {},
                                                                      "decision.condition_codec"));
    const std::string& kind = *kindText;
    if (kind == "all" || kind == "any" || kind == "not") {
        if (!exactFields(*object, {"kind", "children"}))
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "logical condition contains an unknown field",
                                                                          {}, {}, "decision.condition_codec"));
        auto children = decodeChildren(*object);
        if (!children) return eve::Result<Condition>::failure(children.status());
        auto values = std::move(children).takeValue();
        if (kind == "not" && values.size() != 1)
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "not condition requires one child",
                                                                          "children", {}, "decision.condition_codec"));
        if (kind == "all") return eve::Result<Condition>::success(Condition::all(std::move(values)));
        if (kind == "any") return eve::Result<Condition>::success(Condition::any(std::move(values)));
        return eve::Result<Condition>::success(Condition::not_(std::move(values.front())));
    }
    if (kind == "compare") {
        if (!exactFields(*object, {"kind", "key", "operator", "expected"}) &&
            !exactFields(*object, {"kind", "key", "operator"}))
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "compare condition contains an unknown field",
                                                                          {}, {}, "decision.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<Condition>::failure(key.status());
        const auto* opValue = field(*object, "operator");
        if (opValue == nullptr)
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "compare requires operator", "operator", {},
                                                                          "decision.condition_codec"));
        auto op = parseOperator(*opValue);
        if (!op) return eve::Result<Condition>::failure(op.status());
        eve::Value  expected;
        const auto* expectedField = field(*object, "expected");
        if (op.value() == CompareOperator::Exists) {
            if (expectedField != nullptr) expected = *expectedField;
        } else {
            if (expectedField == nullptr)
                return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                              "compare requires expected", "expected",
                                                                              {}, "decision.condition_codec"));
            expected = *expectedField;
        }
        return eve::Result<Condition>::success(
            Condition::compare(std::move(key).takeValue(), op.value(), std::move(expected)));
    }
    if (kind == "policy_call") {
        if (!exactFields(*object, {"kind", "key", "arguments", "scriptDeclaration", "script"}))
            return eve::Result<Condition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "policy_call contains an unknown field", {},
                                                                          {}, "decision.condition_codec"));
        auto key = requiredString(*object, "key");
        if (!key) return eve::Result<Condition>::failure(key.status());
        eve::Value arguments = eve::Value::Object{};
        if (const auto* args = field(*object, "arguments")) arguments = *args;
        std::optional<ScriptConditionDeclaration> declaration;
        const eve::Value*                         declValue = field(*object, "scriptDeclaration");
        if (declValue == nullptr) declValue = field(*object, "script");
        if (declValue != nullptr) {
            const auto* declObject = declValue->getIf<eve::Value::Object>();
            if (declObject == nullptr)
                return eve::Result<Condition>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "scriptDeclaration must be an object",
                                           "scriptDeclaration", {}, "decision.condition_codec"));
            ScriptConditionDeclaration parsed;
            auto                       name = requiredString(*declObject, "name");
            if (!name) return eve::Result<Condition>::failure(name.status());
            parsed.name = std::move(name).takeValue();
            if (const auto* deps = field(*declObject, "dependencies")) {
                const auto* array = deps->getIf<eve::Value::Array>();
                if (array == nullptr)
                    return eve::Result<Condition>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "dependencies must be an array",
                                               "scriptDeclaration.dependencies", {}, "decision.condition_codec"));
                for (const auto& item : *array) {
                    const auto* text = item.getIf<std::string>();
                    if (text == nullptr || text->empty())
                        return eve::Result<Condition>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "dependency must be a non-empty string",
                            "scriptDeclaration.dependencies", {}, "decision.condition_codec"));
                    parsed.dependencies.push_back(*text);
                }
            }
            if (const auto* det = field(*declObject, "determinism")) {
                if (det->isString()) {
                    auto level = parseDeterminism(*det);
                    if (!level) return eve::Result<Condition>::failure(level.status());
                    parsed.determinism = level.value();
                } else if (det->isInt64()) {
                    const auto raw = det->asInt();
                    if (raw < 0 || raw > static_cast<std::int64_t>(DeterminismLevel::ExplicitlyNondeterministic))
                        return eve::Result<Condition>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument, "unknown condition determinism level",
                            "scriptDeclaration.determinism", {}, "decision.condition_codec"));
                    parsed.determinism = static_cast<DeterminismLevel>(raw);
                } else {
                    return eve::Result<Condition>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "determinism must be a string or integer",
                        "scriptDeclaration.determinism", {}, "decision.condition_codec"));
                }
            }
            declaration = std::move(parsed);
        }
        return eve::Result<Condition>::success(
            Condition::policyCall(std::move(key).takeValue(), std::move(arguments), std::move(declaration)));
    }
    return decodeLegacyLeaf(kind, *object);
}

}  // namespace

eve::Result<eve::Value> encodeCondition(const Condition& condition) {
    if (!condition.isValid())
        return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition is invalid", {}, {}, "decision.condition_codec"));
    eve::Value::Object object{{"kind", eve::Value(conditionKindName(condition.kind()))}};
    if (condition.kind() == ConditionKind::All || condition.kind() == ConditionKind::Any ||
        condition.kind() == ConditionKind::Not) {
        eve::Value::Array children;
        for (const auto& child : condition.children()) {
            auto encoded = encodeCondition(child);
            if (!encoded) return eve::Result<eve::Value>::failure(encoded.status());
            children.push_back(std::move(encoded).takeValue());
        }
        object.emplace("children", eve::Value(std::move(children)));
    } else if (condition.kind() == ConditionKind::Compare) {
        object.emplace("key", eve::Value(condition.key()));
        object.emplace("operator", eve::Value(compareOperatorName(condition.compareOperator())));
        if (condition.compareOperator() != CompareOperator::Exists || !condition.expected().isNull())
            object.emplace("expected", condition.expected());
    } else if (condition.kind() == ConditionKind::PolicyCall) {
        object.emplace("key", eve::Value(condition.key()));
        object.emplace("arguments", condition.arguments());
        if (condition.scriptDeclaration()) {
            const auto&       declaration = *condition.scriptDeclaration();
            eve::Value::Array dependencies;
            for (const auto& dependency : declaration.dependencies) dependencies.emplace_back(dependency);
            object.emplace(
                "scriptDeclaration",
                eve::Value(eve::Value::Object{{"name", eve::Value(declaration.name)},
                                              {"dependencies", eve::Value(std::move(dependencies))},
                                              {"determinism", eve::Value(determinismName(declaration.determinism))}}));
        }
    } else {
        return eve::Result<eve::Value>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                       "unsupported condition kind", "kind", {},
                                                                       "decision.condition_codec"));
    }
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<Condition> decodeCondition(const eve::Value& value) { return decodeNode(value); }

}  // namespace eve::condition
