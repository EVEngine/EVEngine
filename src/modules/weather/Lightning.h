#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::graphics {
class Graphics;
}
namespace eve::weather {
/** @brief Copied preparation settings. Width and displacement are world units, independent of endpoint distance. */
struct LightningSettings {
    float width        = 0.08f;
    float displacement = 0.45f;
    float duration     = 0.5f;
    int   subdivisions = 5;
    int   branches     = 4;
    int   variants     = 4;
    int   seed         = 1;
};
/** @brief Owned CPU crossed ribbons along local +Y in [0,1]; no graphics dependency. */
struct LightningGeometry {
    std::vector<float>         positions, normals, uv;
    std::vector<std::uint32_t> indices;
};
/** @brief Validate bounded finite settings without modifying live instances. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<void> validateLightning(const LightningSettings& settings);
/** @brief Deterministic CPU preparation, explicit variant stream; identical inputs give identical geometry on one
 * platform.
 * @cost Linear in 2^subdivisions plus branch count; prepare once per pooled variant, never per frame.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<LightningGeometry> buildLightningGeometry(
    const LightningSettings& settings, unsigned variant);
/** @brief Weather return-stroke envelope, normalized age [0,1]; side-effect free. */
EVENGINE_API_ORCHESTRATION float lightningEnvelope(float age);
/** @brief Independently owned, pooled visual effect; main/render thread only, no callbacks or gameplay damage.
 * Graphics resources and an internal generation-checked render entity are retired in either destruction order.
 * Endpoint positions are copied; no weapon pointer is retained. Recreate after reload/provider retirement.
 */
class EVENGINE_API_ORCHESTRATION Lightning {
public:
    /** @brief Validate and prepare hidden geometry; Graphics must be live during this call.
     * @return Owned instance, or failure with no published visible entity.
     * @cost GPU uploads proportional to variants * geometry size; amortize over repeated triggers.
     */
    [[nodiscard]] static Result<std::unique_ptr<Lightning>> prepare(graphics::Graphics&      graphics,
                                                                    const LightningSettings& settings);
    /** @brief Release the internal entity and meshes on the main thread. */
    ~Lightning();
    Lightning(const Lightning&)            = delete;
    Lightning& operator=(const Lightning&) = delete;
    /** @brief Copy finite, distinct world endpoints; cheap transform update, width stays constant. Invalid input
     * preserves state. */
    [[nodiscard]] Result<void> setEndpoints(float ax, float ay, float az, float bx, float by, float bz);
    /** @brief Set nonnegative finite RGB tint; cheap material update, no environment mutation. */
    [[nodiscard]] Result<void> setColor(float r, float g, float b);
    /** @brief Restart the pulse, selecting the next prebuilt variant; no allocation. */
    [[nodiscard]] Result<void> trigger();
    /** @brief Advance with caller-supplied seconds before render3D; finite nonnegative dt required. */
    [[nodiscard]] Result<void> update(float dt);
    /** @brief Hide immediately; safe after provider/entity retirement. */
    void stop();
    /** @brief Current visual amplitude; zero after stop or retirement. */
    float amplitude() const;

private:
    struct Impl;
    explicit Lightning(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Register preparation/settings and owned Lightning methods on Weather. */
void exposeLightning(ssq::Table& table, ssq::Class& weather);
}  // namespace eve::weather
