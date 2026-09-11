#pragma once

#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace eve::graphics {

/** @brief Versioned, backend-neutral configuration for the built-in stylized water surface. */
struct WaterStyleConfig {
    static constexpr std::string_view SchemaId      = "eve.graphics.stylized-water";
    static constexpr std::uint32_t    SchemaVersion = 1;

    glm::vec3 deepColor{0.02F, 0.16F, 0.24F};
    glm::vec3 shallowColor{0.10F, 0.48F, 0.58F};
    glm::vec3 foamColor{0.85F, 0.93F, 1.0F};
    glm::vec3 reflectionTint{0.70F, 0.85F, 1.0F};

    float waveSpeed       = 1.2F;
    float waveAmplitude   = 0.35F;
    float waveScale       = 14.0F;
    float waveSharpness   = 1.0F;
    float rippleAmplitude = 0.60F;
    int   rippleCount     = 6;
    float rippleInterval  = 1.6F;

    float depthDistance = 5.0F;
    float opacity       = 0.88F;

    float foamWidth    = 0.45F;
    float foamSoftness = 0.08F;
    float foamStrength = 0.85F;

    float fresnelPower       = 5.0F;
    float reflectionIntensity = 0.60F;
    float sunIntensity        = 0.90F;
    bool  screenSpaceReflection = false;
    float screenSpaceReflectionStrength = 0.85F;

    float refractionStrength = 0.025F;

    float causticsStrength = 0.35F;
    float causticsScale    = 3.5F;

    /** @brief Validate finite values and authored ranges without mutating runtime state. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Encode the canonical version-one representation. Unknown fields are rejected. */
    [[nodiscard]] Result<Value> toValue() const;
    /** @brief Decode version one transactionally; unknown fields and future versions are rejected. */
    [[nodiscard]] static Result<WaterStyleConfig> fromValue(const Value& value);
    /** @brief Parse JSON and decode version one transactionally. */
    [[nodiscard]] static Result<WaterStyleConfig> fromJson(std::string_view json);
    /** @brief Serialize canonical JSON after validation. */
    [[nodiscard]] Result<std::string> toJson() const;
};

}  // namespace eve::graphics
