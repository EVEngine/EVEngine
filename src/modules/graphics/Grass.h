#pragma once

#include "graphics/Shader.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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

struct Point {
    glm::vec3 position{0.f};
    glm::vec3 normal{0.f, 1.f, 0.f};
    uint32_t id = 0;
    float scale = 1.f;
};

struct SampleParams {
    float radius = 0.14f;
    int maxPoints = 8192;
    uint32_t seed = 1;
    /** @brief Skip faces whose Y-up slope is below this (0 = keep walls). */
    float minSlopeDot = 0.25f;
};

struct BillboardMesh {
    std::vector<float> posXYZ;
    std::vector<float> nrmXYZ;
    std::vector<float> uvST;
    std::vector<uint32_t> indices;
};

/** @brief Fast Poisson-disk / dart-throwing blue noise on a triangle mesh. */
std::vector<Point> samplePoisson(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                                 const uint32_t *indices, int indexCount,
                                 const SampleParams &params = {});

/** @brief Area-weighted Halton samples (deterministic, evenly spread). */
std::vector<Point> sampleHalton(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                                const uint32_t *indices, int indexCount, int count,
                                uint32_t seed = 1, float minSlopeDot = 0.25f);

/**
 * @brief Expand a unit rectangle per point. Vertex layout:
 *   pos    = grass root
 *   uv     = quad corner in [0,1]^2  (0.5, 0) is the root
 *   normal.x = instance id, normal.y = scale, normal.z = alwaysDark (0/1)
 */
BillboardMesh buildBillboards(const std::vector<Point> &points, float width = 0.62f,
                              float height = 0.95f, bool alwaysDark = false);

/** @brief Discrete 4-frame index with a per-instance phase offset. */
int swayFrame(float time, float frameDuration, uint32_t instanceId, int frameCount = 4);

/**
 * @brief Procedural 4-frame fallback atlas (horizontal strip). GPU paths should load
 * authored 2x2 PNG masks via packSwayAtlasRGBA / createSwayAtlasFromFiles.
 */
void makeSwayAtlasRGBA(int frameW, int frameH, int frames, std::vector<uint8_t> &rgbaOut);
int swayAtlasWidth(int frameW, int frames);
int swayAtlasHeight(int frameH);

Texture *createSwayAtlas(Graphics *gfx, int frameW = 64, int frameH = 64, int frames = 4);

/** @brief Layout of a packed 2x2-per-variant sway atlas (4 grass + 2 leaf typical). */
struct PackedAtlasInfo {
    int width = 0;
    int height = 0;
    int atlasCols = 2;
    int atlasRows = 2;
    int grassVariants = 1;
    int leafVariants = 1;
    int leafRowOffset = 0;
    int frames = 4;
};

/** @brief Load white-on-black (or RGBA) 2x2 sway PNGs and pack them into one atlas. */
void packSwayAtlasRGBA(const std::vector<std::string> &grassFiles,
                       const std::vector<std::string> &leafFiles, std::vector<uint8_t> &rgbaOut,
                       PackedAtlasInfo &info);
Texture *createSwayAtlasFromFiles(Graphics *gfx, const std::vector<std::string> &grassFiles,
                                  const std::vector<std::string> &leafFiles,
                                  PackedAtlasInfo *infoOut = nullptr);

Shader *createShader(Graphics *gfx);
void bindDefaults(Shader *shader);
void bindLayer(Shader *shader, bool alwaysDark);
void bindAtlasLayout(Shader *shader, const PackedAtlasInfo &info);
void setTime(Shader *shader, float seconds);
void setFrameDuration(Shader *shader, float seconds);

int paramCount();
std::string paramName(int index);

/** @brief Unit XZ plane (Y-up) for tests / demos. */
void makePlane(float sizeX, float sizeZ, int segX, int segZ, std::vector<float> &posXYZ,
               std::vector<float> &nrmXYZ, std::vector<uint32_t> &indices);

}  // namespace grass

/**
 * @brief Dense grass + sparse dark tufts on a mesh. Caller owns GrassField*;
 * GPU Mesh / Shader / Texture are owned by Graphics.
 */
class GrassField {
public:
    struct BakeParams {
        /** @brief Poisson spacing. Keep this well below `width` so tufts overlap. */
        float denseRadius = 0.14f;
        float sparseRadius = 0.62f;
        int maxDense = 8192;
        int maxSparse = 384;
        uint32_t seed = 1;
        float width = 0.62f;
        float height = 0.95f;
        float minSlopeDot = 0.25f;
        int atlasFrameW = 64;
        int atlasFrameH = 64;
        int atlasFrames = 4;
        /** @brief Authored 2x2 sway masks (4 grass + 2 leaf). Empty = procedural strip. */
        std::vector<std::string> grassAtlasFiles;
        std::vector<std::string> leafAtlasFiles;
    };

    explicit GrassField(Graphics *gfx);
    ~GrassField() = default;

    GrassField(const GrassField &) = delete;
    GrassField &operator=(const GrassField &) = delete;

    void bake(const float *posXYZ, const float *nrmXYZ, int vertexCount, const uint32_t *indices,
              int indexCount, const BakeParams &params);
    void bakePlane(float sizeX, float sizeZ, int segX, int segZ);
    void bakePlane(float sizeX, float sizeZ, int segX, int segZ, const BakeParams &params);

    void update(float dt);
    void setTime(float seconds);
    float getTime() const { return time_; }
    void setFrameDuration(float seconds);
    float getFrameDuration() const { return frameDuration_; }

    void draw(const glm::mat4 &model);
    void draw();

    Mesh *getDenseMesh() const { return denseMesh_; }
    Mesh *getSparseMesh() const { return sparseMesh_; }
    Shader *getShader() const { return shader_; }
    Texture *getAtlas() const { return atlas_; }

    int getDenseCount() const { return denseCount_; }
    int getSparseCount() const { return sparseCount_; }

private:
    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    Texture *atlas_ = nullptr;
    Mesh *denseMesh_ = nullptr;
    Mesh *sparseMesh_ = nullptr;
    int denseCount_ = 0;
    int sparseCount_ = 0;
    float time_ = 0.f;
    float frameDuration_ = 0.12f;
};

}  // namespace eve::graphics
