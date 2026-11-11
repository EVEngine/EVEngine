#pragma once
/** @file EffectDefinitionCodec.h
 * @brief RPG module-interface: canonical owning RPG effect definition codec and side-effect-free shape validation.
 * @remarks No registry, ECS, capability, event, script or tool publication. Custom implementation binding is separate.
 */
#include "common/Result.h"
#include "common/Value.h"
#include "rpg/Effect.h"
namespace eve::rpg {
/** @brief Whether the name is a built-in RPG stack policy; pure, allocation-free and callback-free. */
[[nodiscard]] EVENGINE_API_PLATFORM bool isBuiltinEffectStackPolicy(std::string_view name) noexcept;
/** @brief Validate canonical RPG effect fields without resolving or invoking custom policies/attribute operations.
 * @ownership Synchronous borrow only; no references retained. No callbacks or live registry lookup.
 * @thread Content/simulation owner thread; reentrant immutable validation.
 * @return Success or structured invalid-field failure; custom names require runtime/content participant admission.
 * @cost Linear in modifiers per validation, amortized per asset admission or actual effect application.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> validateEffectDefinition(const EffectDefinition& definition);
/** @brief Encode all canonical effect fields into an owning deterministic object without registering an effect.
 * @ownership Source is borrowed only for this call; result owns all modifier, tag and extra data.
 * @thread Owner thread; no callbacks, policy invocation or registry access.
 * @return Canonical fields or failure. Nonfinite/invalid facts are rejected rather than normalized silently.
 * @remarks Defines RPG effect field protocol v1, retaining the legacy JSON keys. Unknown input fields are ignored
 * by decode; absent optional fields use the existing defaults. Snapshot/asset envelopes own schema/version headers.
 * @cost Full field copy/encoding proportional to effect size, once per saved source revision.
 * @throws std::bad_alloc Local allocation failed without publication.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<Value> encodeEffectDefinition(const EffectDefinition& definition);
/** @brief Strictly type-check the v1 canonical effect fields, returning an owning definition.
 * @ownership Synchronous Value borrow; no subtree, string or live registry references retained.
 * @thread Owner thread; no callbacks, registration, custom binding lookup or ECS changes.
 * @return Validated field facts or structured failure. This is not proof that external policy/operation code exists.
 * @cost Full field parsing/allocation proportional to effect size, once per admitted source revision.
 * @throws std::bad_alloc Local allocation failed without publication.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<EffectDefinition> decodeEffectDefinition(const Value& value);
}  // namespace eve::rpg
