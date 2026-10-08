#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "graphics/BlendMode.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/Shader.h"
#include "graphics/SurfaceMode.h"
#include "graphics/Texture.h"

#include <map>
#include <string>

namespace eve::graphics {

class Graphics;

/** @brief Selects conventional or atlas/page-table material sampling. */
enum class MaterialVirtualTextureMode { Conventional, AtlasPageTable };

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
class EVENGINE_API_BACKENDS Material {
public:
    static constexpr int kMaxPartsHint = 8;

    /** @brief Material. */
    Material()  = default;
    /** @brief Material. */
    ~Material() = default;

    Material(const Material&)            = delete;
    Material& operator=(const Material&) = delete;

    /** @brief "pbr" | "unlit" | "hair" | "custom" (unknown → pbr). */
    void        setShadingModel(const std::string& model);
    /** @brief Returns the shading model. */
    std::string getShadingModel() const { return shadingModel_; }

    /** @brief Atomically replace validated PBR bindings and enable the extended forward path.
     * Texture pointers are borrowed from Graphics and must outlive this material/draws.
     * Render-thread only; no callbacks. Failure preserves the previous material.
     */
    [[nodiscard]] Result<void> setPbrSurface(const PbrSurface& surface);
    /** @brief Return an owning parameter snapshot; texture resources remain borrowed. */
    PbrSurface pbrSurface() const {
        auto s  = pbr_;
        s.unlit = shadingModel_ == "unlit";
        return s;
    }
    /** @brief Whether full PBR texture/extension rendering is requested. */
    bool hasPbrSurface() const { return pbrEnabled_; }
    /** @brief Set the borrowed material texture.
     * @ownership Graphics factory owns the resource.
     * @lifetime Keep alive through the material and every submitted draw; invalid after factory release.
     * @thread Render-thread affine; no callbacks or retained temporary data. */
    void setAlbedoTexture(Texture* texture) { pbr_.textures[0].texture = texture; }
    /** @brief Get the borrowed material texture.
     * @ownership Graphics factory owns the resource.
     * @lifetime Keep alive through the material and every submitted draw; invalid after factory release.
     * @thread Render-thread affine; no callbacks or retained temporary data. */
    Texture* getAlbedoTexture() const { return pbr_.textures[0].texture; }

    /** @brief Set the borrowed material texture.
     * @ownership Graphics factory owns the resource.
     * @lifetime Keep alive through the material and every submitted draw; invalid after factory release.
     * @thread Render-thread affine; no callbacks or retained temporary data. */
    void setNormalTexture(Texture* texture) { pbr_.textures[2].texture = texture; }
    /** @brief Get the borrowed material texture.
     * @ownership Graphics factory owns the resource.
     * @lifetime Keep alive through the material and every submitted draw; invalid after factory release.
     * @thread Render-thread affine; no callbacks or retained temporary data. */
    Texture* getNormalTexture() const { return pbr_.textures[2].texture; }

    /** @brief Sets the height texture. */
    void     setHeightTexture(Texture* texture) { height_ = texture; }
    /** @brief Returns the height texture. */
    Texture* getHeightTexture() const { return height_; }

    /**
     * @brief Opt this material into physical-atlas virtual-texture sampling.
     * @param albedoAtlas Physical albedo pages; borrowed from Graphics.
     * @param normalAtlas Physical tangent-space normal pages; borrowed from Graphics.
     * @param pageTable RGBA8 virtual-to-physical page table; borrowed from Graphics.
     * @param pageCountX Number of mip-zero virtual pages horizontally.
     * @param pageCountY Number of mip-zero virtual pages vertically.
     * @param atlasSlotsX Number of physical slots horizontally.
     * @param atlasSlotsY Number of physical slots vertically.
     * @param borderFraction Border texels divided by stored physical-page extent.
     * @return Success, or a validation error without changing this material.
     */
    [[nodiscard]] eve::Result<void> setVirtualTexture(Texture* albedoAtlas, Texture* normalAtlas, Texture* pageTable,
                                                      int pageCountX, int pageCountY, int atlasSlotsX, int atlasSlotsY,
                                                      float borderFraction);

    /** @brief Restore conventional albedo/normal/height interpretation. */
    void clearVirtualTexture();
    /** @brief Return the active virtual-texture sampling mode. */
    MaterialVirtualTextureMode virtualTextureMode() const {
        return virtualTextureEnabled_ ? MaterialVirtualTextureMode::AtlasPageTable
                                      : MaterialVirtualTextureMode::Conventional;
    }

    /**
     * @brief Set an optional Mesh3D / hair shader; nullptr selects the built-in shading path.
     * @param shader Borrowed shader owned by the creating Graphics instance; it must outlive this material.
     */
    void    setShader(Shader* shader) { shader_ = shader; }
    /** @brief Return the borrowed shader, or nullptr when the built-in path is active.
     * @lifetime The pointer remains valid only while its creating Graphics instance owns the shader. */
    Shader* getShader() const { return shader_; }

    /** @brief Sets the tint. */
    void  setTint(float r, float g, float b, float a = 1.f);
    /** @brief Returns the tint r. */
    float getTintR() const { return r_; }
    /** @brief Returns the tint g. */
    float getTintG() const { return g_; }
    /** @brief Returns the tint b. */
    float getTintB() const { return b_; }
    /** @brief Returns the tint a. */
    float getTintA() const { return a_; }

    /** @brief Sets the metallic. */
    void  setMetallic(float metallic);
    /** @brief Returns the metallic. */
    float getMetallic() const { return metallic_; }

    /** @brief Sets the roughness. */
    void  setRoughness(float roughness);
    /** @brief Returns the roughness. */
    float getRoughness() const { return roughness_; }

    /** @brief Sets the tex cell bomb. */
    void  setTexCellBomb(float cellScale, float strength, float rotAmount = 1.f);
    /** @brief Returns the tex cell bomb scale. */
    float getTexCellBombScale() const { return texBombScale_; }
    /** @brief Returns the tex cell bomb strength. */
    float getTexCellBombStrength() const { return texBombStrength_; }
    /** @brief Returns the tex cell bomb rotation. */
    float getTexCellBombRotation() const { return texBombRot_; }

    /** @brief Sets the parallax. */
    void  setParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f);
    /** @brief Returns the parallax scale. */
    float getParallaxScale() const { return parallaxScale_; }
    /** @brief Returns the parallax min layers. */
    float getParallaxMinLayers() const { return parallaxMinLayers_; }
    /** @brief Returns the parallax max layers. */
    float getParallaxMaxLayers() const { return parallaxMaxLayers_; }

    /** @brief Sets the receive light. */
    void setReceiveLight(bool receive) { receiveLight_ = receive; }
    /** @brief Returns the receive light. */
    bool getReceiveLight() const { return receiveLight_ && shadingModel_ != "unlit"; }

    /** @brief Sets the cast shadow. */
    void setCastShadow(bool cast) { castShadow_ = cast; }
    /** @brief Returns the cast shadow. */
    bool getCastShadow() const { return castShadow_; }

    /** @brief Sets the receive shadow. */
    void setReceiveShadow(bool receive) { receiveShadow_ = receive; }
    /** @brief Returns the receive shadow. */
    bool getReceiveShadow() const { return receiveShadow_; }

    /** @brief Sets the cast occlusion. */
    void setCastOcclusion(bool cast) { castOcclusion_ = cast; }
    /** @brief Returns the cast occlusion. */
    bool getCastOcclusion() const { return castOcclusion_; }

    /** @brief Sets the hair. */
    void setHair(bool hair);
    /** @brief Returns the hair. */
    bool getHair() const { return isHair_; }

    /** @brief Optional named float knobs (style / custom shader params). */
    bool  hasParam(const std::string& name) const;
    /** @brief Sets the float. */
    void  setFloat(const std::string& name, float value);
    /** @brief Returns the float. */
    float getFloat(const std::string& name) const;

    /**
     * @brief Push this material onto Graphics mesh3d state for the next draw.
     * Does not issue the draw itself.
     */
    [[nodiscard]] Result<void> bind(Graphics& gfx) const;

    /** @brief Return the borrowed effective shader, or nullptr for the default PBR pipeline.
     * @lifetime The pointer remains valid only while its creating Graphics instance owns the shader. */
    Shader* effectiveShader() const;

    /** @brief True when this material should go through the hair transparent pass. */
    bool isTransparentHair() const;

    /** @brief Set "opaque", "masked", or "transparent" surface classification. */
    void        setSurfaceMode(const std::string& mode);
    /** @brief Returns the surface mode. */
    std::string getSurfaceMode() const;
    /** @brief Surface mode. */
    SurfaceMode surfaceMode() const { return surfaceMode_; }
    /** @brief Sets the alpha cutoff. */
    void        setAlphaCutoff(float cutoff);
    /** @brief Returns the alpha cutoff. */
    float       getAlphaCutoff() const { return alphaCutoff_; }
    /** @brief Set "alpha", "premultiplied", "additive", or "multiply". */
    void        setBlendMode(const std::string& mode);
    /** @brief Returns the blend mode. */
    std::string getBlendMode() const;
    /** @brief Blend mode. */
    BlendMode   blendMode() const { return blendMode_; }
    /** @brief Sets the depth write. */
    void        setDepthWrite(bool enabled) { depthWrite_ = enabled; }
    /** @brief Returns the depth write. */
    bool        getDepthWrite() const { return depthWrite_; }
    /** @brief Sets the double sided. */
    void        setDoubleSided(bool enabled) { doubleSided_ = enabled; }
    /** @brief Returns the double sided. */
    bool        getDoubleSided() const { return doubleSided_; }
    /** @brief Keep a bottom-anchored card facing the active camera around world Y during GPU-driven draws. */
    void setCameraFacing(bool enabled) { cameraFacing_ = enabled; }
    /** @brief Return whether GPU-driven card vertices use cylindrical camera-facing orientation. */
    bool getCameraFacing() const { return cameraFacing_; }
    /** @brief Sets the sort priority. */
    void setSortPriority(int priority) { sortPriority_ = priority; }
    /** @brief Returns the sort priority. */
    int  getSortPriority() const { return sortPriority_; }
    /** @brief Optional masked transparency quality: "cutoff", "dither", "coverage". */
    void        setAlphaTechnique(const std::string& technique);
    /** @brief Returns the alpha technique. */
    std::string getAlphaTechnique() const { return alphaTechnique_; }

private:
    PbrSurface                   pbr_;
    bool                         pbrEnabled_   = false;
    std::string                  shadingModel_ = "pbr";
    Texture*                     height_       = nullptr;
    Shader*                      shader_       = nullptr;
    float                        r_ = 1.f, g_ = 1.f, b_ = 1.f, a_ = 1.f;
    float                        metallic_              = 0.f;
    float                        roughness_             = 0.45f;
    float                        texBombScale_          = 4.f;
    float                        texBombStrength_       = 0.f;
    float                        texBombRot_            = 1.f;
    float                        parallaxScale_         = 0.f;
    float                        parallaxMinLayers_     = 8.f;
    float                        parallaxMaxLayers_     = 32.f;
    bool                         receiveLight_          = true;
    bool                         castShadow_            = true;
    bool                         receiveShadow_         = true;
    bool                         castOcclusion_         = true;
    bool                         isHair_                = false;
    SurfaceMode                  surfaceMode_           = SurfaceMode::Opaque;
    BlendMode                    blendMode_             = BlendMode::Alpha;
    float                        alphaCutoff_           = 0.5f;
    bool                         depthWrite_            = false;
    bool                         doubleSided_           = false;
    bool                         cameraFacing_          = false;
    int                          sortPriority_          = 0;
    std::string                  alphaTechnique_        = "cutoff";
    bool                         virtualTextureEnabled_ = false;
    int                          virtualPageCountX_     = 0;
    int                          virtualPageCountY_     = 0;
    int                          virtualAtlasSlotsX_    = 0;
    int                          virtualAtlasSlotsY_    = 0;
    float                        virtualBorderFraction_ = 0.f;
    std::map<std::string, float> params_;
};

/**
 * @brief One mesh + material slot on a multi-part model (Assimp mesh / body region).
 * When Material* is null, the owning Renderable3D MeshRenderer fields are used.
 */
struct ModelPart {
    std::string name;
    Mesh*       mesh     = nullptr;
    Material*   material = nullptr;
    /** @brief Optional per-instance transparent ordering override. */
    int  sortPriority    = 0;
    bool hasSortPriority = false;
};

}  // namespace eve::graphics
