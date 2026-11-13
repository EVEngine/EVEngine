#pragma once

#include "spawn/SpawnMap.h"

namespace eve::definitions {
class DefinitionRegistry;
}

namespace eve::spawn {
/**
 * @brief Canonical Definitions-backed template admission and immutable revision source.
 * @ownership Registry and unit validator are borrowed and must outlive this adapter.
 * @thread Registry/content owner thread only; no locks or retained provider callbacks.
 * @reentrancy Unit validation must not reenter publication or mutate the registry.
 */
class EVENGINE_API_FOUNDATION SpawnTemplateSource final : public ITemplateResolver {
public:
    /** @brief Bind stable registry and validator references; neither is owned or retained by results. */
    SpawnTemplateSource(definitions::DefinitionRegistry& registry, const IUnitDefinitionResolver& units);
    /**
     * @brief Validate then atomically insert/replace the canonical source definition.
     * @return Success or failure preserving the old registry revision.
     * @cost Linear in asset size and referenced members, once per publication.
     */
    [[nodiscard]] Result<void> publish(const Value& source);
    /**
     * @brief Resolve and compile the current registry revision as independent owning data.
     * @return Immutable revision; earlier returned revisions remain valid after replacement/removal.
     * @cost Linear in template size plus reference validation, once per map admission.
     */
    [[nodiscard]] Result<CompiledTemplate> resolve(const DefinitionRef& reference) const override;

private:
    definitions::DefinitionRegistry& registry_;
    const IUnitDefinitionResolver&   units_;
};
}  // namespace eve::spawn
