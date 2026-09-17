#pragma once

#include "common/Result.h"
#include "common/PcgPhotoModeApply.h"
#include "graphics/Shader.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

class Graphics;
class Mesh;
class Texture;

/**
 * @brief t3ssel8r-style stylized grass.
 *
 * Pipeline:
 *   1. Blue-noise (Poisson disk) or Halton samples on a terrain mesh
 *   2. Expand a root-pivoted rectangle into a GPU mesh (one quad per sample)
 *   3. Vertex shader cylindrical-billboards each card around its root
 *   4. 4-frame sway atlas, phase-offset by instance id
 *   5. Shadow map → mix(light green, dark green)
 *   6. Sparse second pass, always dark, as decoration
 */
namespace grass {

/** @brief Pcg-style static foliage surface controls packed into the grass shader profile.
 * Values are copied during upload; colors are linear RGB. */
struct GrassFoliageSettings {
    float baseR = 1.f, baseG = 1.f, baseB = 1.f;
    float alphaCutoff = 0.05f;
    float normalStrength = 1.f;
    float renderDistance = 75.f;
    float fadeRange = 20.f;
    float hardRenderDistance = 105.f;
    float density = 1.f;
    float snowMinimumHeight = 100.f;
    float snowFadeDistance = 20.f;
    float snowProgress = 0.f;
    float snowR = 1.f, snowG = 1.f, snowB = 1.f;
};

/** @brief Runtime terrain-detail overrides kept distinct from foliage material settings. */
struct TerrainDetailOverwriteSettings {
    float pcgDetailDistance = 75.f;
    float pcgFadeoutDistance = 20.f;
    int unityDetailDistance = 105;
    float unityDetailDensity = 1.f;
    int detailResolutionPerPatch = 16;
};

/** @brief Pcg quality category derived from detailResolutionPerPatch. */
enum class TerrainDetailQuality { VeryLow64, Low32, Medium16, High8, VeryHigh4, Ultra2 };

/** @brief Classify a Unity terrain detail patch resolution with Pcg's exact default mapping. */
TerrainDetailQuality terrainDetailQuality(int resolutionPerPatch) noexcept;

/** @brief Validate and copy TerrainDetailOverwrite distances/density into a foliage profile atomically. */
[[nodiscard]] Result<int> applyTerrainDetailOverwrite(GrassFoliageSettings &foliage,
                                                      const TerrainDetailOverwriteSettings &settings);

struct Point {
    glm::vec3 position{0.f};
    glm::vec3 normal{0.f, 1.f, 0.f};
    uint32_t  id    = 0;
    float     scale = 1.f;
    /** @brief Independent horizontal scale; zero retains the uniform scale behavior. */
    float widthScale = 0.f;
    /** @brief Linear instance RGB; {-1,-1,-1} preserves legacy untinted encoding. */
    glm::vec3 tint{-1.f};
};

struct SampleParams {
    float    radius    = 0.14f;
    int      maxPoints = 8192;
    uint32_t seed      = 1;
    /** @brief Skip faces whose Y-up slope is below this (0 = keep walls). */
    float minSlopeDot = 0.25f;
};

struct BillboardMesh {
    std::vector<float>    posXYZ;
    std::vector<float>    nrmXYZ;
    std::vector<float>    uvST;
    std::vector<uint32_t> indices;
};

/** @brief Fast Poisson-disk / dart-throwing blue noise on a triangle mesh. */
std::vector<Point> samplePoisson(const float *posXYZ, const float *nrmXYZ, int vertexCount, const uint32_t *indices,
                                 int indexCount, const SampleParams &params = {});

/** @brief Area-weighted Halton samples (deterministic, evenly spread). */
std::vector<Point> sampleHalton(const float *posXYZ, const float *nrmXYZ, int vertexCount, const uint32_t *indices,
                                int indexCount, int count, uint32_t seed = 1, float minSlopeDot = 0.25f);

/**
 * @brief Expand a unit rectangle per point. Vertex layout:
 *   pos    = grass root
 *   uv     = quad corner in [0,1]^2  (0.5, 0) is the root
 *   normal.x = animation id, normal.y = height scale. normal.z uses legacy 0/1 for uniform
 *   width; independent
 * width w encodes -w for dense or 1+w for dark grass.
 *   A normalized tint encodes RGB8 as negative normal.x; legacy
 * tint {-1,-1,-1} retains id.
 *   The shader derives animation identity from the local root for tinted points, so model
 * transforms do not change the wind phase. Alpha remains texture-owned.
 */
BillboardMesh buildBillboards(const std::vector<Point> &points, float width = 0.62f, float height = 0.95f,
                              bool alwaysDark = false);

/** @brief Discrete 4-frame index with a per-instance phase offset. */
int swayFrame(float time, float frameDuration, uint32_t instanceId, int frameCount = 4);

/**
 * @brief Procedural 4-frame fallback atlas (horizontal strip). GPU paths should load
 * authored 2x2 PNG masks via packSwayAtlasRGBA / createSwayAtlasFromFiles.
 */
void makeSwayAtlasRGBA(int frameW, int frameH, int frames, std::vector<uint8_t> &rgbaOut);
int  swayAtlasWidth(int frameW, int frames);
int  swayAtlasHeight(int frameH);

Texture *createSwayAtlas(Graphics *gfx, int frameW = 64, int frameH = 64, int frames = 4);

/** @brief Layout of a packed 2x2-per-variant sway atlas (4 grass + 2 leaf typical). */
struct PackedAtlasInfo {
    int width         = 0;
    int height        = 0;
    int atlasCols     = 2;
    int atlasRows     = 2;
    int grassVariants = 1;
    int leafVariants  = 1;
    int leafRowOffset = 0;
    int frames        = 4;
};

/** @brief Load white-on-black (or RGBA) 2x2 sway PNGs and pack them into one atlas. */
void     packSwayAtlasRGBA(const std::vector<std::string> &grassFiles, const std::vector<std::string> &leafFiles,
                           std::vector<uint8_t> &rgbaOut, PackedAtlasInfo &info);
Texture *createSwayAtlasFromFiles(Graphics *gfx, const std::vector<std::string> &grassFiles,
                                  const std::vector<std::string> &leafFiles, PackedAtlasInfo *infoOut = nullptr);

Shader *createShader(Graphics *gfx);
void    bindDefaults(Shader *shader);
void    bindLayer(Shader *shader, bool alwaysDark);
void    bindAtlasLayout(Shader *shader, const PackedAtlasInfo &info);
/** @brief Select the static foliage profile and copy its validated surface parameters. */
void    bindFoliage(Shader *shader, const GrassFoliageSettings &settings);
void    setTime(Shader *shader, float seconds);
void    setFrameDuration(Shader *shader, float seconds);

int         paramCount();
std::string paramName(int index);

/** @brief Unit XZ plane (Y-up) for tests / demos. */
void makePlane(float sizeX, float sizeZ, int segX, int segZ, std::vector<float> &posXYZ, std::vector<float> &nrmXYZ,
               std::vector<uint32_t> &indices);

}  // namespace grass

/**
 * @brief Dense grass + sparse dark tufts on a mesh. Caller owns GrassField*;
 * GPU Mesh / Shader / Texture are owned by Graphics.
 */
class GrassField : public IPhotoModeFieldSink {
public:
    struct BakeParams {
        /** @brief Poisson spacing. Keep this well below `width` so tufts overlap. */
        float    denseRadius  = 0.14f;
        float    sparseRadius = 0.62f;
        int      maxDense     = 8192;
        int      maxSparse    = 384;
        uint32_t seed         = 1;
        float    width        = 0.62f;
        float    height       = 0.95f;
        float    minSlopeDot  = 0.25f;
        int      atlasFrameW  = 64;
        int      atlasFrameH  = 64;
        int      atlasFrames  = 4;
        /** @brief Authored 2x2 sway masks (4 grass + 2 leaf). Empty = procedural strip. */
        std::vector<std::string> grassAtlasFiles;
        std::vector<std::string> leafAtlasFiles;
    };

    explicit GrassField(Graphics *gfx);
    ~GrassField();

    GrassField(const GrassField &)            = delete;
    GrassField &operator=(const GrassField &) = delete;

    void bake(const float *posXYZ, const float *nrmXYZ, int vertexCount, const uint32_t *indices, int indexCount,
              const BakeParams &params);
    void bakePlane(float sizeX, float sizeZ, int segX, int segZ);
    void bakePlane(float sizeX, float sizeZ, int segX, int segZ, const BakeParams &params);

    /** @brief Upload explicit grass roots without resampling, using the built-in four-frame atlas.
     * @param points Borrowed finite world roots and height scales greater than 0.001 and nonnegative independent width
     * scales; IDs fit 24 bits.
     * @param width Positive finite billboard width.
     * @param height Positive finite billboard height.
     * @return Uploaded count or InvalidArgument; validation failure preserves the previous field.
     * GPU creation/allocation exceptions also preserve published field pointers. Graphics owns newly
     * allocated meshes, shaders and textures until its teardown, including interrupted uploads.
     * Called on the Graphics owner thread, outside draw/capture callbacks. Retains no input references.
     * Replaces both previous layers with one dense layer; empty input clears their published meshes.
     */
    [[nodiscard]] Result<int> bakePoints(const std::vector<grass::Point> &points, float width, float height);
    /** @brief Upload roots with one static RGBA8 image, preserving original RGB and alpha.
     * Image storage is borrowed only during upload; the resulting texture belongs to Graphics.
     * Same thread, atomic publication and point validation contract as bakePoints. Image dimensions
     * must be positive with complete RGBA8 storage. No atlas slicing or generated animation is applied.
     * @return Uploaded count or InvalidArgument; invalid image leaves the previous field unchanged. */
    [[nodiscard]] Result<int> bakeTexturedPoints(const std::vector<grass::Point> &points, float width, float height,
                                                 const image::ImageData &image);

    /** @brief Upload static foliage albedo, tangent normal and metallic/occlusion/thickness/smoothness masks.
     * All images must be complete, equally sized RGBA8 data. Validation and GPU publication are atomic.
     * Textures are copied during the call and then owned by Graphics. Called on the Graphics owner thread.
     * @return Uploaded point count or InvalidArgument while preserving the prior published field. */
    [[nodiscard]] Result<int> bakeFoliagePoints(const std::vector<grass::Point> &points, float width, float height,
                                                const image::ImageData &albedo, const image::ImageData &normal,
                                                const image::ImageData &mask,
                                                const grass::GrassFoliageSettings &settings);

    /**
     * @brief Apply Pcg terrain detail hard distance, shader fade and density to an uploaded foliage field.
     * @return Derived TerrainDetailQuality ordinal, or InvalidArgument without changing the live shader state.
     * @ownership Settings are copied; no caller data or callback is retained. Owner-thread only.
     */
    [[nodiscard]] Result<int> setTerrainDetailOverwrite(const grass::TerrainDetailOverwriteSettings &settings);
    /** @brief Return the currently applied Unity-style hard detail distance. */
    float getTerrainDetailHardDistance() const noexcept { return detailHardDistance_; }
    /** @brief Return the currently applied normalized detail density. */
    float getTerrainDetailDensity() const noexcept { return detailDensity_; }
    /** @brief Register or revoke this field as the unique Pcg photo-mode grass authority. */
    void setPhotoModeAuthority(bool enabled);
    /** @brief Return the Pcg global density multiplier. */ float getPhotoModeDensity() const noexcept{return photoModeDensity_;}
    /** @brief Return the Pcg global distance multiplier. */ float getPhotoModeDistance() const noexcept{return photoModeDistance_;}
    /** @brief Return the effective camera-cell distance multiplier. */ float getPhotoModeCellDistance() const noexcept{return photoModeCellDistance_;}
    /** @brief Return the signed camera-cell subdivision level. */ int getPhotoModeCellSubdivision() const noexcept{return photoModeCellSubdivision_;}
    PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept override;
    [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment) override;


    void  update(float dt);
    void  setTime(float seconds);
    float getTime() const { return time_; }
    void  setFrameDuration(float seconds);
    float getFrameDuration() const { return frameDuration_; }

    void draw(const glm::mat4 &model);
    void draw();

    /** @brief Enable or disable inclusion in reflection-probe captures. */
    void setReflectionCaptureEnabled(bool enabled) { reflectionCaptureEnabled_ = enabled; }
    /** @brief Return whether this field is included in reflection-probe captures. */
    bool getReflectionCaptureEnabled() const { return reflectionCaptureEnabled_; }
    /** @brief Set the reflection-capture visibility layer mask. */
    void setReflectionCaptureMask(uint32_t mask) { reflectionCaptureMask_ = mask; }
    /** @brief Return the reflection-capture visibility layer mask. */
    uint32_t getReflectionCaptureMask() const { return reflectionCaptureMask_; }

    /** @brief Borrow the dense mesh on the owner thread; valid until this field rebakes or is destroyed. */
    Mesh *getDenseMesh() const { return denseMesh_; }
    /** @brief Borrow the sparse mesh on the owner thread; valid until this field rebakes or is destroyed. */
    Mesh *getSparseMesh() const { return sparseMesh_; }
    /** @brief Borrow the shader on the owner thread; valid until this field is destroyed. */
    Shader *getShader() const { return shader_; }
    /** @brief Borrow the atlas on the owner thread; valid until this field uploads another atlas or is destroyed. */
    Texture *getAtlas() const { return atlas_; }

    int getDenseCount() const { return denseCount_; }
    int getSparseCount() const { return sparseCount_; }

private:
    void applyPhotoModeShaderState();
    [[nodiscard]] Result<int> bakePointData(const std::vector<grass::Point> &points, float width, float height,
                                            const image::ImageData *image, const image::ImageData *normal = nullptr,
                                            const image::ImageData *mask = nullptr,
                                            const grass::GrassFoliageSettings *foliage = nullptr);
    Graphics                 *gfx_           = nullptr;
    Shader                   *shader_        = nullptr;
    Texture                  *atlas_         = nullptr;
    Texture                  *normal_        = nullptr;
    Texture                  *mask_          = nullptr;
    Mesh                     *denseMesh_     = nullptr;
    Mesh                     *sparseMesh_    = nullptr;
    int                       denseCount_    = 0;
    int                       sparseCount_   = 0;
    float                     time_          = 0.f;
    float                     frameDuration_ = 0.12f;
    glm::mat4                 lastModel_{1.f};
    uint64_t                  captureDrawerToken_       = 0;
    uint32_t                  reflectionCaptureMask_    = 0xffffffffu;
    bool                      reflectionCaptureEnabled_ = true;
    bool                      foliageProfile_           = false;
    float                     detailHardDistance_       = 105.f;
    float                     detailDensity_            = 1.f;
    float                     baseDetailRenderDistance_ = 75.f;
    float                     baseDetailFadeRange_      = 20.f;
    float                     baseDetailHardDistance_   = 105.f;
    float                     baseDetailDensity_        = 1.f;
    bool                      photoModeAuthority_       = false;
    float                     photoModeDensity_         = 1.f;
    float                     photoModeDistance_        = 1.f;
    float                     photoModeCellDistance_    = 1.f;
    int                       photoModeCellSubdivision_ = 0;
};

}  // namespace eve::graphics
