#pragma once

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <vector>
#include "common/Result.h"
#include "common/Value.h"

namespace eve::graphics {

/** @brief Independent vegetation field channels; values use linear color and world units. */
enum class VegetationChannel : uint8_t { Color, Extras, Motion, Vertex };
/** @brief Ordered field composition operation. */
enum class VegetationBlend : uint8_t { Replace, Add, Multiply, Minimum, Maximum };
/** @brief Analytic influence shape in an element's local coordinates. */
enum class VegetationShape : uint8_t { Ellipsoid, Box };

/** @brief Owning linear RGBA mask, sampled bilinearly with clamped addressing. */
struct VegetationMask {
    uint32_t               width = 0, height = 0;
    std::vector<glm::vec4> pixels;
};

/** @brief Owning element definition; no scene, texture or entity pointers survive admission.
 * Center/extents are world units; yaw is radians around +Y. Layer bits select 0..8.
 * Mask alpha multiplies opacity and mask RGB multiplies the target RGB.
 * Equal priorities preserve input order. Seasons interpolate winter/spring/summer/autumn.
 */
struct VegetationElement {
    VegetationChannel        channel = VegetationChannel::Color;
    VegetationBlend          blend   = VegetationBlend::Replace;
    VegetationShape          shape   = VegetationShape::Ellipsoid;
    glm::vec3                center{0.f}, extents{1.f};
    float                    yaw = 0.f, opacity = 1.f, edgeFade = 0.25f;
    uint16_t                 layers   = 1;
    int                      priority = 0;
    glm::vec4                value{1.f};
    bool                     seasonal = false;
    std::array<glm::vec4, 4> seasons{glm::vec4(1.f), glm::vec4(1.f), glm::vec4(1.f), glm::vec4(1.f)};
    VegetationMask           mask;
};

/** @brief Owning global defaults. Extras = emission, wetness, overlay, alpha.
 * Motion = signed world X/Z direction, wind power, interaction displacement.
 * Vertex = signed X/Z tilt, world height offset, size multiplier.
 * Color = linear RGB tint and replacement factor; zero multiplies, one replaces.
 */
struct VegetationGlobals {
    glm::vec4 color{1.f, 1.f, 1.f, 0.f};
    glm::vec4 extras{1.f, 0.f, 0.f, 1.f};
    glm::vec4 motion{1.f, 0.f, 0.5f, 0.f};
    glm::vec4 vertex{0.f, 0.f, 0.f, 1.f};
    float     season = 2.f;
};

/** @brief Owning sampled field channels; no lifetime dependency on the field. */
struct VegetationSample {
    glm::vec4 color, extras, motion, vertex;
};

/** @brief Four owning arrays in row-major X/Z order at texel centers. */
struct VegetationAtlas {
    uint32_t                              width = 0, height = 0;
    glm::vec3                             center{0.f}, extent{1.f};
    std::array<std::vector<glm::vec4>, 4> channels;
};

/** @brief One owning vegetation channel in row-major X/Z order at texel centers.
 * Channel atlases may use independent dimensions and world mappings when published as a GPU field set.
 */
struct VegetationChannelAtlas {
    uint32_t               width = 0, height = 0;
    glm::vec3              center{0.f}, extent{1.f};
    std::vector<glm::vec4> pixels;
};

/** @brief Backend-independent, owning vegetation influence field.
 * Single writer; concurrent const evaluation is safe while no replace occurs.
 * No callbacks, external resources, global singleton, ECS mutation or wall clock.
 * Returned samples/atlases are owning projections, never additional mutable authority.
 */
class VegetationField {
public:
    /** @brief Return detached global defaults; concurrent reads require no writer. No callbacks. */
    VegetationGlobals globalValues() const { return globals_; }
    /** @brief Monotonic successful-publication revision for derived render snapshots. */
    uint64_t revision() const { return revision_; }
    /** @brief Return an owning, priority-ordered element snapshot for revision-safe editor previews.
     * @return Detached elements, or Failed if allocation fails. Worker-safe with immutable field.
     */
    [[nodiscard]] Result<std::vector<VegetationElement>> snapshotElements() const;
    /** @brief Decode eve.graphics.vegetation-field version 1; rejects unknown fields/versions.
     * Single-writer, no callbacks. Fully validates before publication; failure preserves state.
     */
    [[nodiscard]] Result<void> restore(const Value& document);
    /** @brief Owning versioned snapshot; thread-safe while no mutation occurs. No callbacks. */
    [[nodiscard]] Result<Value> snapshot() const;
    /** @brief Validate and atomically replace the entire field; failure preserves prior state.
     * @param globals Finite channel defaults; season in [0,4].
     * @param elements Borrowed only during call; copied, validated and stably ordered.
     * @return InvalidArgument for invalid definitions or Failed for allocation failure.
     */
    [[nodiscard]] Result<void> replace(const VegetationGlobals& globals, std::span<const VegetationElement> elements);
    /** @brief Evaluate four independently selected layers (0..8) at a finite world position.
     * @return Owning sample or InvalidArgument. Worker-safe with immutable field.
     */
    [[nodiscard]] Result<VegetationSample> sample(glm::vec3 position, std::array<uint8_t, 4> layers = {}) const;
    /** @brief Bake an X/Z slice. Width/height in [1,2048]; positive extent; one layer per channel.
     * Texel snapping uses full width/height = 2*extent. Output center records the snapped origin.
     * @return Owning atlas or InvalidArgument/Failed; no partial output.
     */
    [[nodiscard]] Result<VegetationAtlas> bake(glm::vec3 center, glm::vec3 extent, uint32_t width, uint32_t height,
                                               std::array<uint8_t, 4> layers = {}, bool snapToTexel = true) const;

private:
    VegetationSample               evaluate(glm::vec3 position, std::array<uint8_t, 4> layers) const;
    VegetationGlobals              globals_;
    std::vector<VegetationElement> elements_;
    uint64_t                       revision_ = 0;
};
}  // namespace eve::graphics
