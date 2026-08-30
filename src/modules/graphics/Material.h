#pragma once

#include "graphics/Mesh.h"
#include "graphics/BlendMode.h"
#include "graphics/Shader.h"
#include "graphics/SurfaceMode.h"
#include "graphics/Texture.h"

#include <map>
#include <string>

namespace eve::graphics {

class Graphics;

/**
 * @brief Packages shading method + surface parameters into one attachable asset.
 *
 * Shading models (string, engine convention — no enums):
 *   "pbr"    — default Mesh3D metallic-roughness (optional custom Shader*)
 *   "unlit"  — forces receiveLight=false
 *   "hair"   — alpha-blended hair/fur cards (isHair=true)
 *   "custom" — requires an explicit Mesh3D Shader*
 *
 * Attach via Renderable3D::setMaterial / setPart. When a Material* is set,
 * RenderSystem3D prefers it over the scattered MeshRenderer fields.
 */
class Material {
public:
    static constexpr int kMaxPartsHint = 8;

    Material() = default;
    ~Material() = default;

    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;

    /** @brief "pbr" | "unlit" | "hair" | "custom" (unknown → pbr). */
    void setShadingModel(const std::string &model);
    std::string getShadingModel() const { return shadingModel_; }

    void setAlbedoTexture(Texture *texture) { albedo_ = texture; }
    Texture *getAlbedoTexture() const { return albedo_; }

    void setNormalTexture(Texture *texture) { normal_ = texture; }
    Texture *getNormalTexture() const { return normal_; }

    void setHeightTexture(Texture *texture) { height_ = texture; }
    Texture *getHeightTexture() const { return height_; }

    /** @brief Optional Mesh3D / hair Shader. nullptr → built-in path for the shading model. */
    void setShader(Shader *shader) { shader_ = shader; }
    Shader *getShader() const { return shader_; }

    void setTint(float r, float g, float b, float a = 1.f);
    float getTintR() const { return r_; }
    float getTintG() const { return g_; }
    float getTintB() const { return b_; }
    float getTintA() const { return a_; }

    void setMetallic(float metallic);
    float getMetallic() const { return metallic_; }

    void setRoughness(float roughness);
    float getRoughness() const { return roughness_; }

    void setTexCellBomb(float cellScale, float strength, float rotAmount = 1.f);
    float getTexCellBombScale() const { return texBombScale_; }
    float getTexCellBombStrength() const { return texBombStrength_; }
    float getTexCellBombRotation() const { return texBombRot_; }

    void setParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f);
    float getParallaxScale() const { return parallaxScale_; }
    float getParallaxMinLayers() const { return parallaxMinLayers_; }
    float getParallaxMaxLayers() const { return parallaxMaxLayers_; }

    void setReceiveLight(bool receive) { receiveLight_ = receive; }
    bool getReceiveLight() const { return receiveLight_; }

    void setCastShadow(bool cast) { castShadow_ = cast; }
    bool getCastShadow() const { return castShadow_; }

    void setReceiveShadow(bool receive) { receiveShadow_ = receive; }
    bool getReceiveShadow() const { return receiveShadow_; }

    void setCastOcclusion(bool cast) { castOcclusion_ = cast; }
    bool getCastOcclusion() const { return castOcclusion_; }

    void setHair(bool hair);
    bool getHair() const { return isHair_; }

    /** @brief Optional named float knobs (style / custom shader params). */
    bool hasParam(const std::string &name) const;
    void setFloat(const std::string &name, float value);
    float getFloat(const std::string &name) const;

    /**
     * @brief Push this material onto Graphics mesh3d state for the next draw.
     * Does not issue the draw itself.
     */
    void bind(Graphics &gfx) const;

    /** @brief Effective shader for Mesh3D draws (may be null → default PBR pipeline). */
    Shader *effectiveShader() const;

    /** @brief True when this material should go through the hair transparent pass. */
    bool isTransparentHair() const;

    /** @brief Set "opaque", "masked", or "transparent" surface classification. */
    void setSurfaceMode(const std::string &mode);
    std::string getSurfaceMode() const;
    SurfaceMode surfaceMode() const { return surfaceMode_; }
    void setAlphaCutoff(float cutoff);
    float getAlphaCutoff() const { return alphaCutoff_; }
    /** @brief Set "alpha", "premultiplied", "additive", or "multiply". */
    void setBlendMode(const std::string &mode);
    std::string getBlendMode() const;
    BlendMode blendMode() const { return blendMode_; }
    void setDepthWrite(bool enabled) { depthWrite_ = enabled; }
    bool getDepthWrite() const { return depthWrite_; }
    void setDoubleSided(bool enabled) { doubleSided_ = enabled; }
    bool getDoubleSided() const { return doubleSided_; }
    void setSortPriority(int priority) { sortPriority_ = priority; }
    int getSortPriority() const { return sortPriority_; }
    /** @brief Optional masked transparency quality: "cutoff", "dither", "coverage". */
    void setAlphaTechnique(const std::string &technique);
    std::string getAlphaTechnique() const { return alphaTechnique_; }
private:
    std::string shadingModel_ = "pbr";
    Texture *albedo_ = nullptr;
    Texture *normal_ = nullptr;
    Texture *height_ = nullptr;
    Shader *shader_ = nullptr;
    float r_ = 1.f, g_ = 1.f, b_ = 1.f, a_ = 1.f;
    float metallic_ = 0.f;
    float roughness_ = 0.45f;
    float texBombScale_ = 4.f;
    float texBombStrength_ = 0.f;
    float texBombRot_ = 1.f;
    float parallaxScale_ = 0.f;
    float parallaxMinLayers_ = 8.f;
    float parallaxMaxLayers_ = 32.f;
    bool receiveLight_ = true;
    bool castShadow_ = true;
    bool receiveShadow_ = true;
    bool castOcclusion_ = true;
    bool isHair_ = false;
    SurfaceMode surfaceMode_ = SurfaceMode::Opaque;
    BlendMode blendMode_ = BlendMode::Alpha;
    float alphaCutoff_ = 0.5f;
    bool depthWrite_ = false;
    bool doubleSided_ = false;
    int sortPriority_ = 0;
    std::string alphaTechnique_ = "cutoff";
    std::map<std::string, float> params_;
};

/**
 * @brief One mesh + material slot on a multi-part model (Assimp mesh / body region).
 * When Material* is null, the owning Renderable3D MeshRenderer fields are used.
 */
struct ModelPart {
    std::string name;
    Mesh *mesh = nullptr;
    Material *material = nullptr;
    /** @brief Optional per-instance transparent ordering override. */
    int  sortPriority = 0;
    bool hasSortPriority = false;
};

}  // namespace eve::graphics
