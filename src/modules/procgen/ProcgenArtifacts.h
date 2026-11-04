#pragma once

/**
 * @file ProcgenArtifacts.h
 * @brief Artifact build/publish ops kept off the high-fanout Procgen facade.
 *
 * `GeneratedArtifact`, `ArtifactStore` and publish receipts pull Grid/Mesh/PointSet,
 * Value, Snapshot and Identity. Call sites that only drive handle-based generation
 * include `Procgen.h`; bindings and artifact publishers include this header.
 */

#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Procgen.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Module-internal artifact surface for CPU build and optional publication.
 * @remarks Friend of `Procgen` so the owning `ArtifactStore` stays behind the pimpl.
 */
struct EVENGINE_API_DOMAINS ProcgenArtifactOps {
    /**
     * @brief Build an owning backend-neutral CPU artifact for a mesh recipe.
     * @param module Procgen owner that resolves @p params.
     * @param recipeId Registered mesh recipe id.
     * @param params Deterministic generation parameters; must not be stale.
     * @param id Non-nil identity for this artifact instance.
     * @return Generated CPU artifact or a structured diagnostic.
     */
    [[nodiscard]] static eve::Result<GeneratedArtifact> buildArtifact(Procgen& module, const std::string& recipeId,
                                                                      ProcgenParamsHandleRef params, ArtifactId id);

    /**
     * @brief Build then transactionally publish through requested optional adapters.
     * @param module Procgen owner that resolves @p params and owns the store.
     * @param recipeId Registered mesh recipe id.
     * @param params Deterministic generation parameters; must not be stale.
     * @param id Non-nil identity for this artifact instance.
     * @param options Scene, graphics and physics adapters required by this call.
     * @return Publication receipt or an Unsupported/validation/adapter diagnostic.
     * @remarks Must run on the main/simulation thread. A failure leaves the store unchanged.
     */
    [[nodiscard]] static eve::Result<ArtifactPublishReceipt> publishArtifact(Procgen&               module,
                                                                             const std::string&     recipeId,
                                                                             ProcgenParamsHandleRef params,
                                                                             ArtifactId             id,
                                                                             ArtifactPublishOptions options = {});

    /**
     * @brief Borrow the compatibility facade's published CPU artifact store.
     * @param module Procgen owner.
     * @return Store owned by @p module for the module lifetime.
     */
    [[nodiscard]] static const ArtifactStore& artifactStore(const Procgen& module) noexcept;

    /**
     * @brief Allocates the next compatibility artifact id for legacy mesh-handle paths.
     * @param module Procgen owner whose sequence counter advances.
     * @return Deterministic child identity under the legacy facade root.
     */
    [[nodiscard]] static ArtifactId nextCompatibilityArtifactId(Procgen& module) noexcept;
};

}  // namespace eve::procgen
