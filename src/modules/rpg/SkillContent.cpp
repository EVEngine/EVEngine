#include "rpg/SkillContent.h"
#include <algorithm>
#include <map>
#include "attributes/AttributeSet.h"
#include "rpg/AttributeSystem.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillDefinitionRuntime.h"
#include "rpg/StatusSystem.h"
namespace eve::rpg {
namespace {
class RequirementBuilder {
    std::map<std::pair<ImplementationKind, std::string>, std::vector<ImplementationUse>> entries_;
    void add(ImplementationKind kind, const std::string& name, ImplementationUse use) {
        entries_[{kind, name}].push_back(std::move(use));
    }

public:
    void skill(const SkillDefinition& definition) {
        if (definition.castCondition.kind() != condition::ConditionKind::PolicyCall &&
            definition.castCondition.children().empty())
            return;
        // @lifetime Local traversal borrows immutable AST nodes only during this synchronous call; no callbacks.
        std::vector<std::pair<const condition::Condition*, std::string>> pending;
        pending.emplace_back(&definition.castCondition, "castCondition");
        while (!pending.empty()) {
            auto [node, path] = std::move(pending.back());
            pending.pop_back();
            if (node->kind() == condition::ConditionKind::PolicyCall)
                add(ImplementationKind::ConditionPolicy, node->key(),
                    {ImplementationSource::Skill, definition.id, path, node->arguments(), node->scriptDeclaration()});
            const auto& children = node->children();
            for (std::size_t i = children.size(); i > 0; --i)
                pending.emplace_back(&children[i - 1], path + ".children[" + std::to_string(i - 1) + "]");
        }
    }
    void effect(const EffectDefinition& definition) {
        if (!isBuiltinEffectStackPolicy(definition.stackPolicy))
            add(ImplementationKind::EffectStackPolicy, definition.stackPolicy,
                {ImplementationSource::Effect, definition.id, "stackPolicy", {}, {}});
        for (std::size_t i = 0; i < definition.modifiers.size(); ++i) {
            const auto& modifier = definition.modifiers[i];
            auto        parsed   = attributes::parseAttributeOperation(modifier.op, modifier.value);
            if (parsed.ok()) continue;
            add(ImplementationKind::AttributeOperation, modifier.op,
                {ImplementationSource::Effect,
                 definition.id,
                 "modifiers[" + std::to_string(i) + "].op",
                 Value::object(
                     {{"attribute", modifier.attribute}, {"value", modifier.value}, {"priority", modifier.priority}}),
                 {}});
        }
    }
    std::vector<ImplementationRequirement> finish() {
        std::vector<ImplementationRequirement> result;
        result.reserve(entries_.size());
        for (auto& [key, uses] : entries_) result.push_back({key.first, key.second, std::move(uses)});
        return result;
    }
};
}  // namespace
struct SkillContent::Impl {
    struct Entry {
        SkillDefinition definition;
        std::size_t     source = 0;
    };
    std::map<std::string, Entry, std::less<>>            entries;
    std::vector<definitions::Definition>                 records;
    std::map<std::string, EffectDefinition, std::less<>> effects;
    std::vector<ImplementationRequirement>               requirements;
    std::vector<ImplementationIdentity>                  identities;
};
SkillContent::SkillContent(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SkillContent::~SkillContent()                                         = default;
SkillContent::SkillContent(SkillContent&&) noexcept                   = default;
SkillContent&        SkillContent::operator=(SkillContent&&) noexcept = default;
Result<SkillContent> SkillContent::prepare(std::span<const definitions::Definition> records) {
    auto state = std::make_unique<Impl>();
    for (const auto& record : records) {
        auto typed = SkillDefinitionRuntime::prepareCaptured(record);
        if (!typed) return Result<SkillContent>::failure(typed.status());
        if (!state->entries.emplace(record.id, Impl::Entry{std::move(typed).takeValue(), state->records.size()}).second)
            return Result<SkillContent>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Duplicate archived skill", record.id));
        state->records.push_back(record);
    }
    std::sort(state->records.begin(), state->records.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (std::size_t i = 0; i < state->records.size(); ++i) state->entries.at(state->records[i].id).source = i;
    RequirementBuilder requirements;
    for (const auto& [id, entry] : state->entries) {
        (void)id;
        requirements.skill(entry.definition);
    }
    state->requirements = requirements.finish();
    return Result<SkillContent>::success(SkillContent(std::move(state)));
}
Result<SkillContent> SkillContent::prepareWithEffects(std::span<const definitions::Definition> records,
                                                      std::span<const EffectDefinition>        effects,
                                                      std::span<const ImplementationIdentity>  identities) {
    auto prepared = prepare(records);
    if (!prepared) return Result<SkillContent>::failure(prepared.status());
    auto content = std::move(prepared).takeValue();
    for (const auto& effect : effects) {
        auto valid = validateEffectDefinition(effect);
        if (!valid) return Result<SkillContent>::failure(valid.status());
        if (!content.impl_->effects.emplace(effect.id, effect).second)
            return Result<SkillContent>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Duplicate archived effect", effect.id));
    }
    for (const auto& [id, entry] : content.impl_->entries)
        for (const auto& effect : entry.definition.grantedEffects)
            if (!content.impl_->effects.contains(effect))
                return Result<SkillContent>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "Archived skill references an absent effect: " + effect, id));
    RequirementBuilder requirements;
    for (const auto& [id, entry] : content.impl_->entries) {
        (void)id;
        requirements.skill(entry.definition);
    }
    for (const auto& [id, effect] : content.impl_->effects) {
        (void)id;
        requirements.effect(effect);
    }
    content.impl_->requirements = requirements.finish();
    content.impl_->identities.assign(identities.begin(), identities.end());
    auto identityKey = [](const auto& value) { return std::pair{value.kind, std::string_view(value.name)}; };
    std::sort(content.impl_->identities.begin(), content.impl_->identities.end(),
              [&](const auto& a, const auto& b) { return identityKey(a) < identityKey(b); });
    for (std::size_t i = 0; i < content.impl_->identities.size(); ++i) {
        const auto& identity = content.impl_->identities[i];
        auto        found    = std::lower_bound(content.impl_->requirements.begin(), content.impl_->requirements.end(),
                                                identityKey(identity),
                                                [&](const auto& a, const auto& key) { return identityKey(a) < key; });
        if (identity.name.empty() || identity.revision.empty() || found == content.impl_->requirements.end() ||
            identityKey(*found) != identityKey(identity) ||
            (i && identityKey(content.impl_->identities[i - 1]) == identityKey(identity)))
            return Result<SkillContent>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Invalid, duplicate or unreferenced archived implementation identity",
                identity.name));
    }
    return Result<SkillContent>::success(std::move(content));
}
ResultRef<const SkillDefinition> SkillContent::definition(std::string_view id) const {
    if (impl_) {
        auto found = impl_->entries.find(id);
        if (found != impl_->entries.end())
            return ResultRef<const SkillDefinition>::success(std::cref(found->second.definition));
    }
    return ResultRef<const SkillDefinition>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "Archived skill absent", std::string(id)));
}
ResultRef<const EffectDefinition> SkillContent::effect(std::string_view id) const {
    if (impl_) {
        auto found = impl_->effects.find(id);
        if (found != impl_->effects.end()) return ResultRef<const EffectDefinition>::success(std::cref(found->second));
    }
    return ResultRef<const EffectDefinition>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "Archived effect absent", std::string(id)));
}
Result<std::string> currentImplementationRevision(ImplementationKind kind, const std::string& name,
                                                  const SkillConditionBindings* conditions) {
    switch (kind) {
        case ImplementationKind::EffectStackPolicy: return StatusSystem::stackPolicyRevision(name);
        case ImplementationKind::AttributeOperation: return AttributeSystem::customOps().implementationRevision(name);
        case ImplementationKind::ConditionPolicy:
            if (conditions) return conditions->revision(name);
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported,
                                  "Condition implementation identity requires its actual call-site binding", name));
    }
    return Result<std::string>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown implementation kind", name));
}
Result<void> SkillContent::implementationClosure() const {
    if (!impl_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Content consumed"));
    if (impl_->identities.size() != impl_->requirements.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Archived external implementation revisions do not cover content requirements"));
    return Result<void>::success();
}
Result<void> SkillContent::validateImplementationBindings(const SkillConditionBindings* conditions) const {
    auto closed = implementationClosure();
    if (!closed) return closed;
    for (const auto& identity : impl_->identities) {
        auto actual = currentImplementationRevision(identity.kind, identity.name, conditions);
        if (!actual) return Result<void>::failure(actual.status());
        if (actual.value() != identity.revision)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Actual implementation revision differs from archived execution binding",
                identity.name));
    }
    for (const auto& requirement : impl_->requirements) {
        if (requirement.kind != ImplementationKind::ConditionPolicy) continue;
        for (const auto& use : requirement.uses) {
            auto valid = conditions->validateDeclaration(requirement.name, use.declaration, use.arguments);
            if (!valid) return valid;
        }
    }
    return Result<void>::success();
}
std::span<const ImplementationRequirement> SkillContent::implementationRequirements() const noexcept {
    return impl_ ? std::span<const ImplementationRequirement>(impl_->requirements)
                 : std::span<const ImplementationRequirement>{};
}
bool SkillContent::contains(std::string_view id) const noexcept { return impl_ && impl_->entries.contains(id); }
ResultRef<const definitions::Definition> SkillContent::source(std::string_view id) const {
    if (impl_) {
        auto found = impl_->entries.find(id);
        if (found != impl_->entries.end())
            return ResultRef<const definitions::Definition>::success(std::cref(impl_->records[found->second.source]));
    }
    return ResultRef<const definitions::Definition>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "Archived skill absent", std::string(id)));
}
std::span<const definitions::Definition> SkillContent::records() const noexcept {
    return impl_ ? std::span<const definitions::Definition>(impl_->records)
                 : std::span<const definitions::Definition>{};
}
}  // namespace eve::rpg
