#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "gpuagents/SurfaceField.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::gpuagents {

/** @brief Uniform block exposed to materials consuming Life Network surfaces. */
struct EVENGINE_API_DOMAINS LifeFieldMaterialUniforms {
    glm::vec3 origin{0.f};
    float     worldSize  = 1.f;
    int       resolution = 1;
    /** @brief Logical sampler name for trail/nutrient/danger/freshness RGBA. */
    std::string lifeFieldSampler = "LifeFieldTexture";
    /** @brief Logical sampler name for height(R) + normal(GBA encoded). */
    std::string surfaceDataSampler = "SurfaceDataTexture";
};

/**
 * @brief Packs SurfaceField channels into Graphics-uploadable RGBA8 textures.
 *
 * Materials sample `LifeFieldTexture` and `SurfaceDataTexture` with
 * Origin / WorldSize / FieldResolution uniforms from `uniforms()`.
 */
class EVENGINE_API_DOMAINS LifeFieldMaterialBinding {
public:
    LifeFieldMaterialBinding() = default;

    /** @brief Rebuild CPU pixel buffers from a surface field. */
    [[nodiscard("check life-field pack")]] Result<void> syncFrom(const SurfaceField& field);

    /** @brief Current material uniforms (valid after successful sync). */
    const LifeFieldMaterialUniforms& uniforms() const { return uniforms_; }

    /** @brief RGBA8 life-field pixels (R=trail, G=nutrient, B=danger, A=freshness). */
    const std::vector<std::uint8_t>& lifeFieldPixels() const { return lifePixels_; }

    /** @brief RGBA8 surface-data pixels (R=height01, GBA=normal*0.5+0.5). */
    const std::vector<std::uint8_t>& surfaceDataPixels() const { return surfacePixels_; }

    /**
     * @brief Upload or refresh Graphics textures from the packed buffers.
     * @param gfx Borrowed Graphics device.
     * @ownership Created textures are owned by the caller; previous pointers are not freed.
     * @lifetime Returned textures remain valid until Graphics tears them down or caller deletes them.
     * @nullable Yes when gfx is null or sync buffers are empty.
     */
    [[nodiscard("check life-field upload")]] Result<graphics::Texture*> uploadLifeField(graphics::Graphics* gfx);

    /**
     * @brief Upload surface-data texture.
     * @param gfx Borrowed Graphics device.
     * @ownership Caller owns the returned texture.
     * @lifetime Until Graphics teardown or caller delete.
     * @nullable Yes when gfx is null or buffers empty.
     */
    [[nodiscard("check surface-data upload")]] Result<graphics::Texture*> uploadSurfaceData(graphics::Graphics* gfx);

    /**
     * @brief Update an existing texture in place.
     * @param gfx Borrowed Graphics.
     * @param texture Borrowed existing texture owned by caller.
     * @param lifeField When true updates life-field pixels; otherwise surface-data.
     * @ownership Borrowed; does not take ownership of texture.
     * @lifetime texture must outlive the call.
     */
    [[nodiscard("check texture update")]] Result<void> updateTexture(graphics::Graphics* gfx,
                                                                     graphics::Texture* texture, bool lifeField) const;

private:
    LifeFieldMaterialUniforms uniforms_{};
    std::vector<std::uint8_t> lifePixels_;
    std::vector<std::uint8_t> surfacePixels_;
};

}  // namespace eve::gpuagents
