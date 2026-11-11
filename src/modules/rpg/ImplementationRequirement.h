#pragma once
/** @file ImplementationRequirement.h
 * @brief RPG module-interface: immutable external implementation requirements derived from admitted content.
 * @remarks Requirements and saved revisions are owning data, never availability proofs, callbacks or registrations.
 */
#include <optional>
#include <string>
#include <vector>
#include "common/Condition.h"
namespace eve::rpg {
class SkillConditionBindings;
/** @brief Distinct external callback signatures; equal names in different kinds are different requirements. */
enum class ImplementationKind { ConditionPolicy, EffectStackPolicy, AttributeOperation };
/** @brief Domain owning a source use; no universal entity/object inheritance is implied. */
enum class ImplementationSource { Skill, Effect };
/** @brief Owning source location and declared call facts, retained even when other uses share the same name.
 * @remarks A missing declaration remains missing. Arguments/dependency declarations do not prove native/script code
 * identity or that a provider is installed; a restore coordinator must validate the actual binding separately.
 */
struct ImplementationUse {
    ImplementationSource                                 source = ImplementationSource::Skill;
    std::string                                          sourceId;
    std::string                                          path;
    Value                                                arguments;
    std::optional<condition::ScriptConditionDeclaration> declaration;
};
/** @brief One sorted implementation requirement with every owning source use preserved. */
struct ImplementationRequirement {
    ImplementationKind             kind = ImplementationKind::ConditionPolicy;
    std::string                    name;
    std::vector<ImplementationUse> uses;
};
/** @brief Owning publisher-declared implementation revision captured from an actual callback registration.
 * @remarks Not a callback, a runtime generation or permission to publish; compare against the actual owner again
 * at materialization. Empty or missing revisions never imply compatibility.
 */
struct ImplementationIdentity {
    ImplementationKind kind = ImplementationKind::ConditionPolicy;
    std::string        name;
    std::string        revision;
};
/** @brief Copy an actual RPG execution binding's stable declared revision, without invoking its callback.
 * @return Owning revision or explicit absence/unversioned/unsupported binding failure.
 * @thread Simulation owner thread, no locks or retained borrows. Condition policies require the actual immutable
 * actor/call-site binding owner, borrowed only here; an absent owner returns Unsupported rather than name-only success.
 * @throws std::bad_alloc Owning copy failed without mutating any registration.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<std::string> currentImplementationRevision(
    ImplementationKind kind, const std::string& name, const SkillConditionBindings* conditions = nullptr);
}  // namespace eve::rpg
