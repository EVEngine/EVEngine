#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/backend/bake/collapse/BonePresentation.h"

namespace eve::physics {
class CollapseCaptureSource;
class BakeRecipe;
using CollapseBakeRecipe = BakeRecipe;
struct FractureGeometry;
/**
 * @brief Owning, solver-independent baked rigid-fragment animation within Physics.
 * @ownership Owns all samples; retains no instance/world pointers.
 * @thread Mutation requires exclusive access. Const sampling/saving may run concurrently
 * when no caller mutates the clip. Sampling/saving have no callbacks. Copies may play independently.
 * capture synchronously invokes the source interface without locks; source methods must not reenter or
 * mutate this clip, and the source must remain stable throughout the call.
 * @lifetime Valid after source instance/world destruction and process restart.
 * File schema EVECOLL1 versions 3/4 checksum all bytes (noncryptographic); v4 embeds provenance. Versions 1/2
 * load as unverified legacy input; saving upgrades them. Version 1 migrates as boxes. Unknown
 * versions, trailing bytes and malformed data are rejected before publication.
 */
class EVENGINE_API_BACKENDS CollapseAnimation {
public:
    /** @brief Freeze complete provenance before the first recorded frame.
     * @cost Linear in bounded metadata, once per bake admission. Does not retain a mutable builder alias.
     * @return Failure leaves clip unchanged; recordings and loaded assets cannot be relabeled after capture.
     */
    [[nodiscard]] Result<void> beginBake(const CollapseBakeRecipe& recipe);
    /** @brief Immutable recorded provenance; null means historical/unidentified recording.
     * @ownership Shared result extends metadata lifetime beyond the clip. No borrowed world or asset data.
     */
    [[nodiscard]] std::shared_ptr<const CollapseBakeRecipe> recipe() const noexcept;
    /** @brief Load only if embedded provenance exactly matches the supplied expected recipe.
     * @cost Linear file validation plus bounded metadata comparison, once per asset admission.
     * @return NotFound for absent files, Conflict for mismatched provenance, PreconditionViolation for legacy
     * unidentified clips. A failure never replaces a caller's current asset.
     */
    [[nodiscard]] static Result<CollapseAnimation> loadForRecipe(std::string_view          path,
                                                                 const CollapseBakeRecipe& expected);
    /** @brief Stable noncryptographic cache identity over every source byte.
     * Compatibility facade; canonical native implementation is physics/bake bakedAssetContentKey.
     * @param source Borrowed recipe/model/script bytes, consumed synchronously.
     * @return Owning 32-character lowercase hex key; not a security digest.
     * @cost Linear native byte hashing, once per source/recipe admission, never per frame.
     * @thread Reentrant, no IO, global state or callbacks.
     */
    [[nodiscard]] static std::string contentKey(std::string_view source);
    /** @brief Record a complete frame at strictly increasing seconds, starting at zero.
     * @cost Linear in fragments per frame; the first embedded geometry is also validated quadratically in
     * vertices per fragment. Amortize that admission over the entire offline recording.
     * @return Failure leaves recorded frames unchanged. Source is borrowed only here.
     */
    [[nodiscard]] Result<void> capture(const CollapseCaptureSource& source, float seconds);
    /** @brief Sample a fragment, clamping time to endpoints; linear translation and shortest quaternion nlerp.
     * @return Owning pose; state switches at the recorded frame boundary.
     */
    [[nodiscard]] Result<BonePresentation> sample(float seconds, int bone) const;
    /** @brief Save a new file; refuses existing paths to preserve previous bakes.
     * @cost Linear in recorded samples, with one bounded serialized buffer; perform outside the frame loop.
     * @return Checked IO status. Complete staged data is published without replacement.
     * Filesystems without hard links fail explicitly. Linux syncs data before publication and the parent
     * directory afterwards; other platforms currently guarantee atomic visibility only. Storage must honor fsync.
     * Linux requires anonymous O_TMPFILE staging and procfs descriptor links; unsupported staging fails explicitly.
     * A Linux barrier failure carries publication=not_published or published and durability=unconfirmed
     * diagnostic details. A published failure leaves a complete target: inspect it before deciding to retry.
     * Failure never removes the target; only this call's private temporary files are cleaned.
     */
    [[nodiscard]] Result<void> save(std::string_view path) const;
    /** @brief Load and validate a file before publishing an owning animation.
     * @cost Linear in file size; perform once per asset, reuse for all playback instances.
     * @return Complete animation or failure, with no observable partial restore.
     */
    [[nodiscard]] static Result<CollapseAnimation> load(std::string_view path);
    /** @brief Recorded duration in seconds. */
    [[nodiscard]] float duration() const noexcept;
    /** @brief Number of fragments. */
    [[nodiscard]] int boneCount() const noexcept;
    /** @brief Number of recorded frames. */
    [[nodiscard]] int frameCount() const noexcept;

    /** @brief Shared immutable embedded meshes; empty for v1/box animations.
     * @ownership Return value extends mesh lifetime across clip replacement or destruction.
     * @thread Single caller during mutation; immutable geometry is safe to share.
     */
    [[nodiscard]] std::shared_ptr<const FractureGeometry> fragmentGeometry() const noexcept;

private:
    struct Frame {
        float                         seconds;
        std::vector<BonePresentation> bones;
    };
    std::shared_ptr<const CollapseBakeRecipe> recipe_;
    std::vector<Frame>                        frames_;
    std::shared_ptr<const FractureGeometry>   geometry_;
};
}  // namespace eve::physics
