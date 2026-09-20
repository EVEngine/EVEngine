#pragma once

#include "asset/import/VegetationPreset.h"
#include "editor/EditorProperty.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Host-visible state of the TVE conversion tool. */
struct VegetationConverterState {
    enum class Phase { Empty, Dirty, Prepared, Published, Error };
    Phase                         phase                 = Phase::Empty;
    std::uint64_t                 requestRevision       = 0;
    std::uint64_t                 preparedRevision      = 0;
    std::uint64_t                 publicationGeneration = 0;
    std::size_t                   objectCount           = 0;
    std::size_t                   assetCount            = 0;
    std::size_t                   findingCount          = 0;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Worker boundary used to prepare a complete vegetation package candidate. */
class IVegetationConversionPreparer {
public:
    virtual ~IVegetationConversionPreparer() = default;
    /** @brief Prepare an owning candidate without external mutation or retained request references. */
    [[nodiscard]] virtual Result<asset_import::PreparedAssetImport> prepare(
        const asset_import::UnityVegetationBatchImportRequest& request) const = 0;
};

/** @brief Built-in preparer backed by the canonical TVE batch transaction. */
class NativeVegetationConversionPreparer final : public IVegetationConversionPreparer {
public:
    [[nodiscard]] Result<asset_import::PreparedAssetImport> prepare(
        const asset_import::UnityVegetationBatchImportRequest& request) const override;
};

/** @brief Atomic host publication boundary for one prepared vegetation package. */
class IVegetationConversionPublisher {
public:
    virtual ~IVegetationConversionPublisher() = default;
    /** @brief Publish one immutable complete candidate.
     * @param candidate Borrowed only during this synchronous call and never retained.
     * @param expectedGeneration Host generation observed before publication.
     * @return New generation, or Conflict/failure without partial publication.
     */
    [[nodiscard]] virtual EditorResult<std::uint64_t> publish(const asset_import::PreparedAssetImport& candidate,
                                                              std::uint64_t expectedGeneration) = 0;
};

/** @brief UI-neutral TVE converter controller with revision-safe prepare and publish phases.
 * @ownership The preparer and publisher are borrowed and must outlive this controller.
 * @thread Owner thread only. Calls are synchronous and no controller lock is held across either boundary.
 * @reentrancy Preparer and publisher implementations must not call this controller recursively.
 */
class EVENGINE_API_ORCHESTRATION VegetationConverterController {
public:
    VegetationConverterController(const IVegetationConversionPreparer& preparer,
                                  IVegetationConversionPublisher&      publisher);

    /** @brief Replace the complete owning request and invalidate any older candidate. */
    [[nodiscard]] EditorResult<void> setRequest(asset_import::UnityVegetationBatchImportRequest request,
                                                std::uint64_t expectedPublicationGeneration);
    /** @brief Prepare the current request as one detached EVA candidate. */
    [[nodiscard]] EditorResult<void> prepare();
    /** @brief Publish the candidate only when it still matches the current request revision. */
    [[nodiscard]] EditorResult<std::uint64_t> publish();
    /** @brief Discard request, candidate and diagnostics. */
    [[nodiscard]] EditorResult<void> clear();
    /** @brief Return an owning state snapshot for desktop or in-game UI hosts. */
    [[nodiscard]] VegetationConverterState state() const;

private:
    const IVegetationConversionPreparer&                           preparer_;
    IVegetationConversionPublisher&                                publisher_;
    std::optional<asset_import::UnityVegetationBatchImportRequest> request_;
    std::optional<asset_import::PreparedAssetImport>               candidate_;
    VegetationConverterState                                       state_;
    std::uint64_t                                                  expectedPublicationGeneration_ = 0;
};

}  // namespace eve::editor
