#pragma once
/** @file SkillConditionBindings.h
 * @brief RPG module-interface: immutable owning condition executors with their publisher-declared revision/contracts.
 * @remarks Injected into an actor's Skills or explicit evaluation context; no global registry, ECS entities,
 * capabilities, events, scripts or tools. Executable code is never serialized.
 */
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include "common/Condition.h"
#include "common/Result.h"
namespace eve::rpg {
class RPGActor;
/** @brief Owning input for a named actual policy callback and its immutable declared implementation contract. */
struct SkillConditionPolicy {
    /** @brief Read-only native/script policy executable.
     * @lifetime Actor and arguments are borrowed only during synchronous evaluation and must not be retained.
     * @nullable Actor is null for context-free previews; callbacks that require one must report unavailable.
     */
    using Function =
        std::function<std::optional<condition::ConditionResult>(const RPGActor* actor, const Value& arguments)>;
    /** @brief Read-only authority query for one scope; uses the canonical authority.allow argument/contract adapter.
     * @lifetime Actor/scope are synchronous borrows only; never retained. Actor may be null for previews.
     */
    using Authority = std::function<std::optional<bool>(const RPGActor* actor, std::string_view scope)>;
    using Executor  = std::variant<Function, Authority>;
    std::string                                          name;
    std::string                                          revision;
    std::optional<condition::ScriptConditionDeclaration> declaration;
    Executor                                             function;
};
/** @brief Shared immutable policy execution owner; revision/contract cannot be replaced separately from code.
 * @ownership Owns copied functions and metadata. Actor/context references share real ownership, never a no-op deleter.
 * @thread Simulation owner thread. Callbacks execute synchronously without locks; they must not mutate actors,
 * input trees or content registries, destroy borrowed actors/definitions, or retain temporary arguments. Their
 * captured resources must outlive the shared binding owner.
 */
class EVENGINE_API_PLATFORM SkillConditionBindings {
public:
    /** @brief Release callback/resource ownership; no callback is invoked. */
    ~SkillConditionBindings();
    SkillConditionBindings(const SkillConditionBindings&)            = delete;
    SkillConditionBindings& operator=(const SkillConditionBindings&) = delete;
    /** @brief Copy and admit a complete immutable named execution set without calling policy functions.
     * @return Owning shared binding set or invalid/duplicate-name failure; no source or registry mutation.
     * @remarks Names/revisions/functions are required; optional static declarations use the canonical Condition codec.
     * Authority executors must use name authority.allow and omit a static declaration: their actual typed adapter
     * derives the scope-specific contract, without executing the authority function.
     * Publisher must change revision when behavior changes. authority.allow may be bound explicitly like any policy;
     * a supplied binding set is authoritative, so missing names never fall back to legacy callbacks.
     * @cost Callback/metadata copies and canonical declaration validation per policy at content admission.
     * @throws std::bad_alloc Local allocation failed without publication.
     * @throws Any exception copying a captured callable propagates without publishing an owner.
     */
    [[nodiscard]] static Result<std::shared_ptr<const SkillConditionBindings>> prepare(
        std::span<const SkillConditionPolicy> policies);
    /** @brief Execute the owned named callback, or return unavailable without calling other providers.
     * @param name Synchronous policy-name borrow.
     * @param arguments Synchronous immutable argument borrow, never retained by this owner.
     * @param actor Nullable synchronous read-only domain borrow passed to the actual executor; must never be retained.
     * Mutable actor facts belong to this invocation's actor, not a captured per-actor copy in a shared code owner.
     * @return Callback's condition result, or nullopt when the named implementation/result is unavailable.
     * @remarks Reentry retains immutable records; no lock is held. Callback exceptions propagate to the caller.
     */
    [[nodiscard]] std::optional<condition::ConditionResult> evaluate(std::string_view name, const Value& arguments,
                                                                     const RPGActor* actor = nullptr) const;
    /** @brief Copy the actual callback's declared revision, or NotFound; never execute code.
     * @ownership Returned string survives this owner; owner-thread query only.
     * @throws std::bad_alloc Owning output allocation failed without mutation.
     */
    [[nodiscard]] Result<std::string> revision(std::string_view name) const;
    /** @brief Compare one source use's optional declaration with the actual callback's exact declared contract.
     * @return Success, NotFound, or Conflict; never executes code or changes source/bindings.
     * @remarks Static policy declaration presence/name/dependency order/determinism must match. Typed authority
     * executors derive their declaration from arguments.scope through canonical authorityAllow; different scopes are
     * validated independently. Inputs are borrowed synchronously; no undeclared script fallback or callback invocation.
     */
    [[nodiscard]] Result<void> validateDeclaration(
        std::string_view name, const std::optional<condition::ScriptConditionDeclaration>& declaration,
        const Value& arguments = {}) const;

private:
    struct Impl;
    explicit SkillConditionBindings(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
};
/** @brief Validate every policy source use against its actual immutable executor owner, without executing code.
 * @param condition Synchronous immutable AST borrow; no nodes are retained.
 * @param bindings Nullable synchronous owner borrow; absence is accepted only when the AST has no PolicyCall.
 * @return Success or invalid AST, missing implementation/owner or mismatched declaration failure.
 * @thread Simulation/content owner thread. No registration, entity mutation or callback invocation.
 * @cost Linear AST traversal with temporary local storage per admission/evaluation; default leaf/empty All is
 * allocation-free.
 * @throws std::bad_alloc Temporary traversal storage failed without source changes.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> validateSkillConditionBindings(const condition::Condition&   condition,
                                                                                const SkillConditionBindings* bindings);
}  // namespace eve::rpg
