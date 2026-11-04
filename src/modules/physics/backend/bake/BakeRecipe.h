#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::physics {
/** @brief Explicit source coordinates; planar pixels preserve XY axes without an implicit flip. */
enum class BakeCoordinates { MetresRightHandedYUp, PlanarPixels };
/**
 * @brief Bounded provenance builder for explicit-coordinate baked recordings.
 * @ownership Owns names and content digests; never retains source bytes or world pointers.
 * @thread Owner-thread mutation; const encoding is reentrant without concurrent mutation. No callbacks or IO.
 * Schemas EVECREC1 (metres) and EVEBREC1 (planar pixels) fix units, field order and length framing. Unknown
 * versions/fields are rejected. Producers must supply algorithm/backend revisions, all source dependencies, parameters,
 * the actual fixed timestep and sampling stride, and every named RNG seed. These describe admission, not cross-backend
 * determinism.
 */
class EVENGINE_API_BACKENDS BakeRecipe {
public:
    /** @brief Select the producer coordinate contract; unknown values fail without changing the recipe. */
    [[nodiscard]] Result<void> setCoordinates(BakeCoordinates coordinates);
    /** @brief Current coordinate contract, defaulting to the original metre convention. */
    [[nodiscard]] BakeCoordinates coordinates() const noexcept;
    /** @brief Set an explicit algorithm/build revision token; invalid input leaves the builder unchanged. */
    [[nodiscard]] Result<void> setProducer(std::string_view revision);
    /** @brief Set the explicit backend and its revision; never infer it from the player platform. */
    [[nodiscard]] Result<void> setBackend(std::string_view revision);
    /** @brief Set the actual finite positive simulation step in seconds and positive ticks per captured sample. */
    [[nodiscard]] Result<void> setTiming(float stepSeconds, std::uint32_t sampleStride);
    /** @brief Digest complete parameter bytes, including defaults resolved by the producer.
     * @cost Linear in bytes; do once before baking, not during capture or playback.
     */
    [[nodiscard]] Result<void> setParameters(std::string_view bytes);
    /** @brief Insert or replace a named dependency digest; names are stable logical IDs, not absolute paths.
     * @cost Linear in source bytes; do once per dependency admission, not per frame.
     * @return Failure preserves prior entries. Up to 64 sources and 256 bytes per token.
     */
    [[nodiscard]] Result<void> setSource(std::string_view name, std::string_view bytes);
    /** @brief Insert or replace one of at most 64 named RNG seeds. Empty stream sets explicitly mean no RNG. */
    [[nodiscard]] Result<void> setRandomStream(std::string_view name, std::uint64_t seed);
    /** @brief Encode complete provenance canonically, sorting dependency and RNG names.
     * @cost Linear in bounded metadata; encode once per admission, never per frame.
     * @return Owning canonical schema bytes, or failure for incomplete metadata. Empty parameter bytes are valid.
     */
    [[nodiscard]] Result<std::string> encode() const;
    /** @brief Parse a bounded canonical recipe record, rejecting duplicate names and trailing bytes.
     * @cost Linear in metadata, once per asset load.
     * @return Complete owning recipe; failures publish no partial state.
     */
    [[nodiscard]] static Result<BakeRecipe> decode(std::string_view bytes);
    /** @brief Validate a sample against the declared fixed-step/stride grid.
     * @return Failure for nonfinite, unrepresentable or off-grid timestamps. Four float epsilons relative
     * to max(expected time, step), capped at a quarter sample interval, tolerate producer multiplication order;
     * frame zero must be exactly zero.
     */
    [[nodiscard]] Result<void> validateSampleTime(std::uint32_t frame, float seconds) const;
    /** @brief Compute the noncryptographic cache key of the complete canonical recipe.
     * @cost Linear in bounded metadata, once per admission. Not a security/authentication digest.
     */
    [[nodiscard]] Result<std::string> key() const;

private:
    BakeCoordinates                      coordinates_ = BakeCoordinates::MetresRightHandedYUp;
    std::string                          producer_, backend_, parameters_;
    float                                stepSeconds_  = 0;
    std::uint32_t                        sampleStride_ = 0;
    std::map<std::string, std::string>   sources_;
    std::map<std::string, std::uint64_t> randomStreams_;
};
}  // namespace eve::physics
