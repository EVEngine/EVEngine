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
    virtual ~IPostFX() = default;

    virtual Shader *newGrassShader() = 0;
    virtual GrassField *newGrassField() = 0;
    virtual Waterfall *newWaterfall() = 0;
    virtual Water *newWater() = 0;
    virtual Volumetric *newVolumetric() = 0;
    virtual AmbientOcclusion *newAmbientOcclusion() = 0;
    virtual Outline *newOutline() = 0;
    virtual GlobalIllumination *newGlobalIllumination() = 0;
    virtual ScreenSpaceReflection *newScreenSpaceReflection() = 0;
    virtual AntiAliasing *newAntiAliasing() = 0;

    virtual AmbientOcclusion *pipelineAmbientOcclusion() = 0;
    virtual GlobalIllumination *pipelineGlobalIllumination() = 0;
    virtual AntiAliasing *pipelineAntiAliasing() = 0;
    virtual Outline *pipelineOutline() = 0;
};

}  // namespace eve::graphics
