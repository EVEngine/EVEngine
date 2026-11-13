#include "rpg/SkillConditionBindings.h"
#include <map>
#include "common/ConditionCodec.h"
#include "common/ConditionKeys.h"
namespace eve::rpg {
struct SkillConditionBindings::Impl {
    std::map<std::string, SkillConditionPolicy, std::less<>> policies;
};
SkillConditionBindings::SkillConditionBindings(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
SkillConditionBindings::~SkillConditionBindings() = default;
Result<std::shared_ptr<const SkillConditionBindings>> SkillConditionBindings::prepare(
    std::span<const SkillConditionPolicy> policies) {
    using R    = Result<std::shared_ptr<const SkillConditionBindings>>;
    auto state = std::make_shared<Impl>();
    for (const auto& policy : policies) {
        if (policy.name.empty() || policy.revision.empty() || policy.function.valueless_by_exception() ||
            !std::visit([](const auto& function) { return static_cast<bool>(function); }, policy.function))
            return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                "Condition policy name, revision and callback are required",
                                                policy.name));
        if (std::holds_alternative<SkillConditionPolicy::Authority>(policy.function) &&
            (policy.name != condition::kAuthorityAllowPolicy || policy.declaration))
            return R::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "Typed authority callbacks require authority.allow and a scope-derived declaration", policy.name));
        if (policy.declaration) {
            auto encoded =
                condition::encodeCondition(condition::Condition::policyCall(policy.name, {}, policy.declaration));
            if (!encoded) return R::failure(encoded.status());
            auto decoded = condition::decodeCondition(encoded.value());
            if (!decoded) return R::failure(decoded.status());
        }
        if (!state->policies.emplace(policy.name, policy).second)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Duplicate condition policy", policy.name));
    }
    return R::success(std::shared_ptr<const SkillConditionBindings>(new SkillConditionBindings(std::move(state))));
}
std::optional<condition::ConditionResult> SkillConditionBindings::evaluate(std::string_view name,
                                                                           const Value&     arguments,
                                                                           const RPGActor*  actor) const {
    // Pin the actual executable records even if a reentrant callback releases the last external binding owner.
    const auto retained = impl_;
    auto       found    = retained->policies.find(name);
    if (found == retained->policies.end()) return std::nullopt;
    if (const auto* authority = std::get_if<SkillConditionPolicy::Authority>(&found->second.function))
        return condition::resolveAuthorityAllow(name, arguments,
                                                [&](std::string_view scope) { return (*authority)(actor, scope); });
    return std::get<SkillConditionPolicy::Function>(found->second.function)(actor, arguments);
}
Result<std::string> SkillConditionBindings::revision(std::string_view name) const {
    auto found = impl_->policies.find(name);
    if (found == impl_->policies.end())
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Condition policy is absent from the actual binding owner", std::string(name)));
    return Result<std::string>::success(found->second.revision);
}
Result<void> SkillConditionBindings::validateDeclaration(
    std::string_view name, const std::optional<condition::ScriptConditionDeclaration>& declaration,
    const Value& arguments) const {
    auto found = impl_->policies.find(name);
    if (found == impl_->policies.end())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Condition policy is absent from the actual binding owner", std::string(name)));
    const bool authority = std::holds_alternative<SkillConditionPolicy::Authority>(found->second.function);
    std::optional<condition::ScriptConditionDeclaration> scoped;
    if (authority) {
        auto scope = condition::authorityAllowScope(arguments);
        if (!scope)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "authority.allow requires a nonempty scope", std::string(name)));
        scoped = condition::authorityAllow(*scope).scriptDeclaration();
    }
    const auto& actual = authority ? scoped : found->second.declaration;
    if (actual.has_value() != declaration.has_value() ||
        (actual && (actual->name != declaration->name || actual->dependencies != declaration->dependencies ||
                    actual->determinism != declaration->determinism)))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Condition policy declaration differs from its source use", std::string(name)));
    return Result<void>::success();
}
Result<void> validateSkillConditionBindings(const condition::Condition& root, const SkillConditionBindings* bindings) {
    if (root.kind() != condition::ConditionKind::PolicyCall && root.children().empty() && root.isValid())
        return Result<void>::success();
    // @lifetime AST nodes are borrowed only by this synchronous local traversal; no callbacks or retained pointers.
    std::vector<const condition::Condition*> pending{&root};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (!node->isValid())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "Invalid condition AST during implementation binding"));
        if (node->kind() == condition::ConditionKind::PolicyCall) {
            if (!bindings)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported,
                                      "Condition policy requires its actual immutable execution owner", node->key()));
            auto valid = bindings->validateDeclaration(node->key(), node->scriptDeclaration(), node->arguments());
            if (!valid) return valid;
        }
        for (const auto& child : node->children()) pending.push_back(&child);
    }
    return Result<void>::success();
}
}  // namespace eve::rpg
