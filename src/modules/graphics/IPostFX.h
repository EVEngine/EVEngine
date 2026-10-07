#pragma once

// Narrow post-processing / effect-factory interface of the Graphics backend.
// Consumers (stylize, daynight, weather, editor previews) create effect
// objects without depending on the full graphics::Graphics god class.

namespace eve::graphics {

class AmbientOcclusion;
class AntiAliasing;
class GlobalIllumination;
class GrassField;
class Outline;
class ScreenSpaceReflection;
class Shader;
class Volumetric;
class Water;
class Waterfall;

/** @brief Post-process / special-effect object factory. */
class IPostFX {
public:
    /** @brief Releases IPostFX resources. */
    virtual ~IPostFX() = default;

    /** @brief Creates a grass shader. @ownership Caller deletes unless documented otherwise. */
    virtual Shader *newGrassShader() = 0;
    /** @brief Creates a grass field. @ownership Caller deletes unless documented otherwise. */
    virtual GrassField *newGrassField() = 0;
    /** @brief Creates a waterfall. @ownership Caller deletes unless documented otherwise. */
    virtual Waterfall *newWaterfall() = 0;
    /** @brief Creates a water. @ownership Caller deletes unless documented otherwise. */
    virtual Water *newWater() = 0;
    /** @brief Creates a volumetric. @ownership Caller deletes unless documented otherwise. */
    virtual Volumetric *newVolumetric() = 0;
    /** @brief Creates a ambient occlusion. @ownership Caller deletes unless documented otherwise. */
    virtual AmbientOcclusion *newAmbientOcclusion() = 0;
    /** @brief Creates a outline. @ownership Caller deletes unless documented otherwise. */
    virtual Outline *newOutline() = 0;
    /** @brief Creates a global illumination. @ownership Caller deletes unless documented otherwise. */
    virtual GlobalIllumination *newGlobalIllumination() = 0;
    /** @brief Creates a screen space reflection. @ownership Caller deletes unless documented otherwise. */
    virtual ScreenSpaceReflection *newScreenSpaceReflection() = 0;
    /** @brief Creates a anti aliasing. @ownership Caller deletes unless documented otherwise. */
    virtual AntiAliasing *newAntiAliasing() = 0;

    /** @brief Pipeline ambient occlusion. */
    virtual AmbientOcclusion *pipelineAmbientOcclusion() = 0;
    /** @brief Pipeline global illumination. */
    virtual GlobalIllumination *pipelineGlobalIllumination() = 0;
    /** @brief Pipeline anti aliasing. */
    virtual AntiAliasing *pipelineAntiAliasing() = 0;
    /** @brief Pipeline outline. */
    virtual Outline *pipelineOutline() = 0;
};

}  // namespace eve::graphics
