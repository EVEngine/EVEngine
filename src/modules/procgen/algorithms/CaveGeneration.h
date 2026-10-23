#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <memory>

namespace eve::procgen {
class Params;
class MeshBuild;
class ScalarField3D;
namespace cave_detail {
struct State;
}

/** @brief Ordered admission states of an editable cave build. */
enum class CaveGenerationPhase { Empty, Configured, Skeleton, Sampled, Evolved, Deposited };
/** @brief Independently composable surface operators; order is chosen by the caller. */
enum class CaveSurfaceOperation { Reactivity, ReactivePatches, Condensation, Curvature };

/**
 * @brief Staged limestone generator sharing the canonical mesh.cave algorithms.
 * @remarks Owns parameters, skeleton and dense intermediate fields. No borrowed library, VM callback,
 * graphics or ECS state survives a call. All failed operations preserve the prior stage.
 * Stage values are transient; persist the script URI, configuration and seed and rebuild on reload.
 * Within one build/compiler the legacy schedule is bit-exact; cross-platform floats have no bitwise promise.
 * @thread Owner thread only; no callbacks, locks or reentrant execution.
 */
class EVENGINE_API_DOMAINS CaveGeneration {
public:
    /** @brief Construct an empty, inexpensive generation owner. */
    CaveGeneration();
    /** @brief Release owned intermediates without publication. */
    ~CaveGeneration();
    CaveGeneration(const CaveGeneration&)            = delete;
    CaveGeneration& operator=(const CaveGeneration&) = delete;
    /** @brief Transfer the generation owner. */
    CaveGeneration(CaveGeneration&&) noexcept;
    /** @brief Transfer the generation owner, releasing previous intermediates. */
    CaveGeneration& operator=(CaveGeneration&&) noexcept;
    /** @brief Copy and validate configuration; success resets all derived stages. */
    [[nodiscard]] Result<void> configure(const Params& params);
    /** @brief Generate the editable seeded cave skeleton from configured parameters.
     * @cost Linear in skeleton candidates, much cheaper than volume sampling; run once per configuration.
     */
    [[nodiscard]] Result<void> buildSkeleton();
    /** @brief Edit one chamber in normalized [-1,1] coordinates before sampling; radii must be positive. */
    [[nodiscard]] Result<void> setChamber(int index, float x, float y, float z, float rx, float ry, float rz);
    /** @brief Number of chamber records in the current skeleton. */
    [[nodiscard]] int chamberCount() const noexcept;
    /** @brief Sample the current skeleton and configured geological operators.
     * @cost Volume samples times active geological features; amortize by editing the resulting field.
     */
    [[nodiscard]] Result<void> sampleField();
    /** @brief Apply one selected surface operator; repeated operators and caller-selected order are supported.
     * @cost At least linear in volume samples; compose before extracting a mesh.
     */
    [[nodiscard]] Result<void> evolveSurface(CaveSurfaceOperation operation);
    /** @brief Anchor secondary deposits on the current surface; disallows subsequent surface retreat.
     * @cost Volume samples times deposit candidates; execute after all desired surface edits.
     */
    [[nodiscard]] Result<void> deposit();
    /** @brief Return an independent field snapshot; no internal storage is exposed.
     * @cost Linear in volume samples; batch edits in the returned snapshot before replacement.
     */
    [[nodiscard]] Result<ScalarField3D> field() const;
    /** @brief Replace the field with a same-dimension snapshot before deposition; preserve stage diagnostics.
     * @cost Linear in volume samples; batch edits before calling this boundary.
     */
    [[nodiscard]] Result<void> replaceField(const ScalarField3D& field);
    /** @brief Extract an independent mesh from any sampled stage; omitted evolution/deposits stay omitted.
     * @cost Volume extraction plus emitted triangles and refinement; reuse the returned mesh until inputs change.
     */
    [[nodiscard]] Result<MeshBuild> buildMesh() const;
    /** @brief Current stage; moved-from owners report Empty and can be configured again. */
    [[nodiscard]] CaveGenerationPhase phase() const noexcept;

private:
    std::unique_ptr<cave_detail::State> state_;
    CaveGenerationPhase                 phase_ = CaveGenerationPhase::Empty;
};
}  // namespace eve::procgen
