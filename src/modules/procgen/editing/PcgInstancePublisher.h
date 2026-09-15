#pragma once

#include "editing/EditingProtocol.h"
#include "procgen/Procgen.h"
#include "procgen/editing/ProcgenEditingTypes.h"

#include <string>

namespace eve::procgen_editing {

/**
 * @brief Undoable publish/remove of spawn.mesh PointSet batches through Procgen scene sink.
 *
 * Closes the editor loop for Spawner → scene instances without coupling PointGraph to Scene.
 * `planPublish` records batch identity and attribute names; callers pass the live point-set
 * handle to `applyPublish`. `planRemove` / `apply` undo a published batch.
 */
class PcgInstancePublisher {
public:
    static constexpr const char* kPublishType = "procgen.pcg.publishInstances.v1";
    static constexpr const char* kRemoveType  = "procgen.pcg.removeInstances.v1";

    /** @brief Build a publish operation whose inverse removes the same batch id. */
    [[nodiscard]] EditorResult<editing::DomainOperation> planPublish(
        const std::string& batchId, const std::string& assetAttribute = "mesh",
        const std::string& defaultAsset = {}) const;

    /** @brief Build a remove operation (inverse empty; redo requires a fresh publish plan). */
    [[nodiscard]] EditorResult<editing::DomainOperation> planRemove(const std::string& batchId) const;

    /** @brief Execute a planPublish operation with a live point-set handle. */
    [[nodiscard]] EditorResult<void> applyPublish(procgen::Procgen& procgen,
                                                  const editing::DomainOperation& operation,
                                                  procgen::ProcgenPointSetHandleRef points) const;

    /**
     * @brief Execute a planRemove operation, or undo a publish by applying its inverse payload.
     * @note Passing a publish operation applies its inverse remove.
     */
    [[nodiscard]] EditorResult<void> apply(procgen::Procgen& procgen,
                                           const editing::DomainOperation& operation) const;
};

}  // namespace eve::procgen_editing
