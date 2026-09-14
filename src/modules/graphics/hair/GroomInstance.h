#pragma once

#include "common/Result.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/Procedural.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {

class Graphics;
class Mesh;
class Shader;
class Texture;

namespace hair {

/**
 * @brief Runtime groom drawable (UE `UGroomComponent` analogue).
 *
 * Phase 1 copies asset strands (or procedural scalp), expands ribbons, and
 * draws with the built-in Kajiya-Kay hair shader. Caller owns the instance;
 * Graphics owns Mesh / Shader / Texture.
 *
 * @thread Render-thread affine; not safe to share across threads.
 * @reentrancy `draw` must not re-enter Graphics resource creation.
 */
class GroomInstance {
public:
    explicit GroomInstance(Graphics *gfx);
    ~GroomInstance();

    GroomInstance(const GroomInstance &) = delete;
    GroomInstance &operator=(const GroomInstance &) = delete;

    /**
     * @brief Replace runtime strands from a shared asset (deep copy of groups).
     * @ownership Asset remains owned by the caller; instance stores a copy.
     */
    [[nodiscard]] Result<void> setAsset(const GroomAsset &asset);

    /** @brief Bake a procedural plane groom into a single default group. */
    [[nodiscard]] Result<void> bakeProceduralPlane(float sizeX, float sizeZ,
                                                   const ProceduralParams &params = {});

    /** @brief Bake procedural strands on an indexed triangle mesh. */
    [[nodiscard]] Result<void> bakeProceduralMesh(const float *posXYZ, const float *nrmXYZ,
                                                  int vertexCount, const uint32_t *indices,
                                                  int indexCount,
                                                  const ProceduralParams &params = {});

    /** @brief Force LOD index; -1 restores automatic selection. */
    void setForcedLod(int lod);
    [[nodiscard]] int getForcedLod() const { return forcedLod_; }

    /** @brief Screen-size hint in [0,1] used when forced LOD is disabled. */
    void setScreenSize(float screenSize);
    [[nodiscard]] float getScreenSize() const { return screenSize_; }

    void setWidthScale(float scale);
    [[nodiscard]] float getWidthScale() const { return widthScale_; }

    void setSideHint(float x, float y, float z);

    /** @brief Rebuild GPU ribbon mesh from the active LOD. */
    [[nodiscard]] Result<void> rebuild();

    void draw(const glm::mat4 &model);
    void draw();

    [[nodiscard]] Mesh *getMesh() const { return mesh_; }
    [[nodiscard]] Shader *getShader() const { return shader_; }
    [[nodiscard]] Texture *getTexture() const { return texture_; }
    [[nodiscard]] int getCurveCount() const;
    [[nodiscard]] int getPointCount() const;
    [[nodiscard]] size_t getGroupCount() const { return asset_.groupCount(); }

private:
    [[nodiscard]] Result<void> bakeFromStrands(StrandsDatas strands, const char *debugName);
    [[nodiscard]] const GroomGroup *primaryGroup() const;
    [[nodiscard]] StrandsDatas decimatedStrands(const StrandsDatas &src, float curveFraction) const;

    Graphics *gfx_ = nullptr;
    GroomAsset asset_;
    Mesh *mesh_ = nullptr;
    Shader *shader_ = nullptr;
    Texture *texture_ = nullptr;
    int forcedLod_ = -1;
    float screenSize_ = 1.f;
    float widthScale_ = 1.f;
    glm::vec3 sideHint_{1.f, 0.f, 0.f};
    glm::mat4 lastModel_{1.f};
};

}  // namespace hair
}  // namespace eve::graphics
