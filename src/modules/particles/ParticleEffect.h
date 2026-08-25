#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::particles {

class ParticleEmitter;

/**
 * @brief Runtime instance of a versioned, multi-emitter particle effect asset.
 *
 * The instance owns every emitter created by the asset. Its transform, playback,
 * visibility, layer, and exposed parameters are applied to the group as a unit.
 */
class ParticleEffect {
public:
    /** @brief Destroy the group and every emitter it owns. */
    ~ParticleEffect();

    ParticleEffect(const ParticleEffect&)            = delete;
    ParticleEffect& operator=(const ParticleEffect&) = delete;

    /** @brief Parse and instantiate an effect asset from JSON text. */
    static ParticleEffect* fromText(const std::string& json, const std::string& sourcePath,
                                    std::string* error = nullptr);
    /** @brief Read, parse, and instantiate an effect asset. */
    static ParticleEffect* fromFile(const std::string& path, std::string* error = nullptr);

    /** @brief Return the supported asset schema version. */
    int getVersion() const { return version_; }
    /** @brief Return the source file path, or empty text for an in-memory asset. */
    std::string getSourcePath() const { return sourcePath_; }
    /** @brief Return the number of named emitter layers. */
    int getEmitterCount() const;
    /** @brief Return a stable layer name by index, or empty text. */
    std::string getEmitterName(int index) const;
    /** @brief Return an emitter layer by index, or null. */
    ParticleEmitter* getEmitter(int index) const;
    /** @brief Return an emitter layer by name, or null. */
    ParticleEmitter* getEmitterByName(const std::string& name) const;

    /** @brief Set the group transform origin in world space. */
    void setPosition(float x, float y);
    /** @brief Return the group origin X coordinate. */
    float getX() const { return x_; }
    /** @brief Return the group origin Y coordinate. */
    float getY() const { return y_; }
    /** @brief Rotate local offsets and emitter directions in radians. */
    void setRotation(float radians);
    /** @brief Return the group rotation in radians. */
    float getRotation() const { return rotation_; }
    /** @brief Scale local emitter offsets. */
    void setScale(float scale);
    /** @brief Return the local-offset scale. */
    float getScale() const { return scale_; }
    /** @brief Offset all emitter render layers while preserving asset-local ordering. */
    void setLayer(int layer);
    /** @brief Return the group render-layer offset. */
    int getLayer() const { return layer_; }
    /** @brief Set visibility for every emitter layer. */
    void setVisible(bool visible);
    /** @brief Return the group visibility state. */
    bool isVisible() const { return visible_; }

    /** @brief Start every emitter timeline. */
    void start();
    /** @brief Stop every emitter timeline. */
    void stop();
    /** @brief Pause every emitter timeline. */
    void pause();
    /** @brief Reset every emitter timeline and clear live particles. */
    void reset();
    /** @brief Emit a manual burst from one named layer. */
    bool emit(const std::string& emitterName, int count);

    /** @brief Set one gameplay parameter on every layer that declares or binds it. */
    void setFloatParameter(const std::string& name, float value);
    /** @brief Return an effect parameter value, or zero when unknown. */
    float getFloatParameter(const std::string& name) const;
    /** @brief Return true when the asset declares the parameter. */
    bool hasFloatParameter(const std::string& name) const;

private:
    struct Layer {
        std::string      name;
        ParticleEmitter* emitter       = nullptr;
        float            offsetX       = 0.f;
        float            offsetY       = 0.f;
        float            baseDirection = 0.f;
        int              baseLayer     = 0;
        bool             enabled       = true;
    };

    ParticleEffect() = default;
    void syncTransform();
    void syncLayer();

    int                                    version_ = 1;
    std::string                            sourcePath_;
    std::vector<Layer>                     layers_;
    std::unordered_map<std::string, float> parameters_;
    float                                  x_        = 0.f;
    float                                  y_        = 0.f;
    float                                  rotation_ = 0.f;
    float                                  scale_    = 1.f;
    int                                    layer_    = 0;
    bool                                   visible_  = true;
};

}  // namespace eve::particles
