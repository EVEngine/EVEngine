#pragma once
#include "common/Export.h"


#include "common/ECS.h"
#include "zeroerr/assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <glm/glm.hpp>

namespace eve::graphics {

class Canvas;

/**
 * @brief Convert spot angle/softness into GPU cosines (outer < inner).
 * Shared by Light2D and Light3D flashlight cones.
 * @param angleDeg Outer half-angle in degrees.
 * @param softness Penumbra in [0, 1].
 * @param[out] cosOuter Cosine of the outer half-angle.
 * @param[out] cosInner Cosine of the softened inner half-angle.
 */
inline void lightSpotCosines(float angleDeg, float softness, float &cosOuter, float &cosInner) {
    constexpr float kPi = 3.14159265358979323846f;
    const float outerDeg = std::clamp(angleDeg, 0.1f, 89.f);
    const float soft = std::clamp(softness, 0.f, 1.f);
    const float outerRad = outerDeg * (kPi / 180.f);
    const float innerRad = outerRad * (1.f - soft);
    cosOuter = std::cos(outerRad);
    cosInner = std::cos(innerRad);
    if (cosInner < cosOuter + 1e-4f) cosInner = cosOuter + 1e-4f;
}

/** @brief Backward-compatible alias for @ref lightSpotCosines. */
inline void light2dSpotCosines(float angleDeg, float softness, float &cosOuter, float &cosInner) {
    lightSpotCosines(angleDeg, softness, cosOuter, cosInner);
}

/** @brief GPU light packing for lit2d (std140-friendly). */
struct Light2DGpu {
    glm::vec4 posRadius{0.f};  // xy = point/spot pos or dir; w = radius (0 => directional)
    glm::vec4 color{0.f};      // rgb * intensity
    /** @brief Spot cone: xy = beam dir; z/w = cos(outer/inner half-angle). z <= -1.5 => off. */
    glm::vec4 spot{0.f, 0.f, -2.f, -2.f};
};

struct Lighting2DUBO {
    static constexpr int kMaxLights = 8;
    glm::vec4 ambient{0.15f, 0.15f, 0.18f, 0.f};
    glm::vec4 meta{0.f};  // x = count, y = viewW, z = viewH
    Light2DGpu lights[kMaxLights]{};
};

/**
 * @brief Declarative 2D light. Collected by RenderSystem (max 8 per canvas/frame).
 * type: "point" | "dir" | "spot" (≤15 chars).
 */
class EVENGINE_API_BACKENDS Light2D : public ecs::Entity {
public:
    ENTITY(Light2D, ecs::Entity)

    void release() override {}

    struct Data {
        std::string type = "point";
        float x = 0.f;
        float y = 0.f;
        float dx = 0.f;
        float dy = -1.f;
        float r = 1.f, g = 1.f, b = 1.f;
        float intensity = 1.f;
        float radius = 200.f;
        /** @brief Spot outer half-angle in degrees (flashlight cone aperture / 2). */
        float spotAngleDeg = 30.f;
        /** @brief Spot penumbra: 0 = hard edge, 1 = soft falloff from axis to rim. */
        float spotSoftness = 0.35f;
        bool enabled = true;
        /** @brief Contribute as volumetric shaft source when collecting occlusion maps. */
        bool volumetric = false;
        float volumetricIntensity = 1.f;
        /**
         * @brief When true with volumetric, skip lit2d surface packing and only drive
         * screenspace shafts (emissive-glow proxy for neon/sprites).
         */
        bool volumetricOnly = false;
        Canvas *canvas = nullptr;
        Light2D *entity = nullptr;
    };

    COMPONENT(Data, data)

    static Light2D *createLight(const std::string &type = "point");

    /**
     * @brief Create a volumetric-only glow proxy (skips surface lighting).
     * @param x World/canvas X of the glow core.
     * @param y World/canvas Y of the glow core.
     * @param r Linear HDR red.
     * @param g Linear HDR green.
     * @param b Linear HDR blue.
     * @param intensity Non-negative radiance scale.
     * @param radius Falloff radius in canvas units.
     * @ownership ECS owns the result. @lifetime Valid until ECS destroys the entity.
     */
    static Light2D *createEmissiveProxy(float x, float y, float r, float g, float b,
                                        float intensity = 1.f, float radius = 200.f);

    void setType(const std::string &type);
    std::string getType();

    void setPosition(float x, float y);
    float getX();
    float getY();

    void setDirection(float dx, float dy);
    float getDirX();
    float getDirY();

    void setColor(float r, float g, float b, float intensity = 1.f);
    void setRadius(float radius);
    float getRadius();

    /**
     * @brief Outer half-angle of a spot cone in degrees (clamped to (0, 89]).
     * @param degrees Half-angle; full flashlight aperture is 2× this value.
     */
    void setSpotAngle(float degrees);
    /** @brief Current outer half-angle in degrees. */
    float getSpotAngle();
    /**
     * @brief Softness of the spot penumbra in [0, 1].
     * @param softness 0 keeps a hard rim; 1 widens the inner falloff to the axis.
     */
    void setSpotSoftness(float softness);
    /** @brief Current spot softness in [0, 1]. */
    float getSpotSoftness();

    void setEnabled(bool enabled);
    bool isEnabled();

    void setVolumetric(bool enabled);
    bool getVolumetric();
    void setVolumetricIntensity(float intensity);
    float getVolumetricIntensity();
    void setVolumetricOnly(bool enabled);
    bool getVolumetricOnly();

    void setCanvas(Canvas *canvas);
};

/** @brief GPU light packing for mesh3d / PBR (std140-friendly). */
struct Light3DGpu {
    glm::vec4 posRadius{0.f};  // xyz = point/spot pos or dir; w = radius (0 => directional)
    glm::vec4 color{0.f};      // rgb * intensity; a = spotBias when spot else 1
    /**
     * @brief Spot cone: xyz = world beam direction; w = spotScale (1/(cosInner-cosOuter)).
     * w <= 0 means the light is not a spot (point or directional).
     */
    glm::vec4 spot{0.f, 0.f, 0.f, -1.f};
};

struct Lighting3DPack {
    static constexpr int kMaxLights = 8;
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f};
    Light3DGpu lights[kMaxLights]{};
    int count = 0;
    std::array<glm::vec4, 9> diffuseProbeSh{};  // xyz = RGB coefficient
    bool diffuseProbeShEnabled = false;
    static constexpr int kMaxDiffuseVolumeProbes = 8;
    std::array<glm::vec4, kMaxDiffuseVolumeProbes> diffuseVolumePosition{};
    std::array<glm::vec4, kMaxDiffuseVolumeProbes> diffuseVolumeExtent{};
    std::array<glm::vec4, kMaxDiffuseVolumeProbes * 9> diffuseVolumeSh{};
    int diffuseVolumeProbeCount = 0;
    bool diffuseVolumeTrilinearCell = false;
};

/**
 * @brief Declarative 3D light. Collected by RenderSystem3D (max 8 per frame).
 * type: "point" | "dir" | "spot" (≤15 chars).
 */
class EVENGINE_API_BACKENDS Light3D : public ecs::Entity {
public:
    ENTITY(Light3D, ecs::Entity)

    void release() override {}

    struct Data {
        std::string type = "point";
        float x = 0.f, y = 0.f, z = 0.f;
        float dx = 0.4f, dy = 1.f, dz = 0.3f;
        float r = 1.f, g = 1.f, b = 1.f;
        float intensity = 1.f;
        float radius = 8.f;
        /** @brief Spot outer half-angle in degrees (ignored for point/dir). */
        float spotAngleDeg = 30.f;
        /** @brief Spot penumbra in [0, 1] (ignored for point/dir). */
        float spotSoftness = 0.35f;
        bool enabled = true;
        bool castShadow = false;
        /**
         * @brief Shadow technique request. Stored as uint8_t to avoid pulling
         * Shadow.h into every Light consumer; values match ShadowMethod.
         * 0 = Auto, 1 = None, 2 = CascadedDirectional, 3 = PerspectiveSpot, 4 = CubePoint.
         */
        std::uint8_t shadowMethod = 0;
        /**
         * @brief Frame-transient local shadow atlas slot (0..kLocalSlots-1), or -1.
         * Written by selectShadowCasters; packed into Light3DGpu beam length for shaders.
         * Not authored / serialized.
         */
        int shadowLocalSlot = -1;
        float shadowBias = 0.f;  // 0 = auto NDC from cascade texel / Z range
        float shadowStrength = 1.f;
        bool volumetric = false;
        float volumetricIntensity = 1.f;
        /**
         * @brief When true with volumetric, skip mesh/PBR surface lighting and only
         * contribute to volumetric shafts / froxel media (emissive prop proxy).
         */
        bool volumetricOnly = false;
        Light3D *entity = nullptr;
    };

    COMPONENT(Data, data)

    static Light3D *createLight(const std::string &type = "point");

    /**
     * @brief Create a volumetric-only point glow proxy for emissive props.
     * Pair with a PBR emissive material + bloom for the surface look; this light
     * drives froxel / raymarch shafts without double-lighting the mesh.
     * @ownership ECS owns the result. @lifetime Valid until ECS destroys the entity.
     */
    static Light3D *createEmissiveProxy(float x, float y, float z, float r, float g, float b,
                                        float intensity = 1.f, float radius = 8.f);

    void setType(const std::string &type);
    std::string getType();

    void setPosition(float x, float y, float z);
    float getX();
    float getY();
    float getZ();

    void setDirection(float dx, float dy, float dz);
    float getDirX();
    float getDirY();
    float getDirZ();

    void setColor(float r, float g, float b, float intensity = 1.f);
    void setRadius(float radius);
    float getRadius();

    /**
     * @brief Outer half-angle of a 3D spot cone in degrees (clamped to (0, 89]).
     * @param degrees Half-angle; full cone aperture is 2× this value.
     */
    void setSpotAngle(float degrees);
    /** @brief Current spot outer half-angle in degrees. */
    float getSpotAngle();
    /**
     * @brief Softness of the spot penumbra in [0, 1].
     * @param softness 0 keeps a hard rim; 1 widens the inner falloff to the axis.
     */
    void setSpotSoftness(float softness);
    /** @brief Current spot softness in [0, 1]. */
    float getSpotSoftness();

    void setEnabled(bool enabled);
    bool isEnabled();

    void setCastShadow(bool cast);
    bool getCastShadow();
    /**
     * @brief Request a shadow technique: `auto`, `none`, `csm`, `perspective`, `cube`.
     * Invalid names leave the previous method unchanged.
     */
    void setShadowMethod(const std::string &method);
    /** @brief Current shadow method name (`auto` / `none` / `csm` / `perspective` / `cube`). */
    std::string getShadowMethod();
    void setShadowBias(float bias);
    float getShadowBias();
    void setShadowStrength(float strength);
    float getShadowStrength();

    void setVolumetric(bool enabled);
    bool getVolumetric();
    void setVolumetricIntensity(float intensity);
    float getVolumetricIntensity();
    void setVolumetricOnly(bool enabled);
    bool getVolumetricOnly();
};

}  // namespace eve::graphics
