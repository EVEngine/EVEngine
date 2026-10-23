#pragma once

// Narrow 3D-rendering interface of the Graphics backend.
// Consumers (voxel, model3d render paths, scene preview) depend on this
// instead of the full graphics::Graphics god class. All methods are pure
// virtual; the concrete backend implements them.

#include "graphics/Color.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace eve::graphics {

/** @brief Number of strongest vertex influences retained by GPU skinning. */
enum class SkinInfluenceLimit : int { One = 1, Two = 2, Four = 4 };
struct PbrSurface;


class Texture;

/** @brief ReflectionProbeUpload public API. */
struct ReflectionProbeUpload {
    static constexpr int kMaxProbes = 2;
    /** @brief Probe public API. */
    struct Probe {
        Texture *cubemap = nullptr;
        glm::vec3 center{0.f};
        glm::vec3 extent{0.f};
        float intensity = 0.f;
        float blendDistance = 0.f;
        int priority = 0;
    };
    Probe probes[kMaxProbes]{};
    int count = 0;
};

class Canvas;
class Camera3D;
class Mesh;
class PrimitiveSceneCanvas3D;
class Shader;
struct ClusteredLightingUpload;
struct Lighting3DPack;
struct ShadowUpload;

/** @brief 3D frame / mesh / light / shadow rendering surface. */
class IGraphics3D {
public:
    /** @brief Releases IGraphics3D resources. */
    virtual ~IGraphics3D() = default;

    /** @brief Renders 3 d. */
    virtual void render3D() = 0;
    /** @brief Renders scene 3 d to canvas. */
    virtual void renderScene3DToCanvas(Canvas *canvas, Camera3D *camera) = 0;
    /** @brief Sets the directional light. */
    virtual void setDirectionalLight(float dx, float dy, float dz, float r = 1.f, float g = 1.f,
                                     float b = 1.f) = 0;
    /** @brief Draws scene 3 drgba. */
    virtual void drawScene3DRGBA(float x, float y, float w, float h, float r = 1.f, float g = 1.f,
                                 float b = 1.f, float a = 1.f) = 0;
    /** @brief Draws canvas rgba. */
    virtual void drawCanvasRGBA(Canvas *canvas, float x, float y, float w, float h, float r = 1.f,
                                float g = 1.f, float b = 1.f, float a = 1.f) = 0;

    /** @brief Begins 3 d frame. */
    virtual void begin3DFrame() = 0;
    /** @brief Begins 3 d frame to canvas. */
    virtual void begin3DFrameToCanvas(Canvas *canvas) = 0;
    /** @brief Ends 3 d frame to canvas. */
    virtual void end3DFrameToCanvas() = 0;

    /**
     * @brief Submits an owning frame-local primitive canvas into the active 3D pass.
     * @param canvas Synchronously consumed command snapshot; it is never retained.
     * @thread Render-thread affine and valid only between begin/end 3D frame calls.
     * @reentrancy Does not invoke scripts or caller callbacks.
     */
    virtual void drawPrimitiveScene(const PrimitiveSceneCanvas3D &canvas) = 0;

    /** @brief Sets the mesh 3 d view proj. */
    virtual void setMesh3DViewProj(const glm::mat4 &viewProj) = 0;
    /** @brief Sets the mesh 3 d view. */
    virtual void setMesh3DView(const glm::mat4 &view) = 0;
    /** @brief Sets the mesh 3 d clip. */
    virtual void setMesh3DClip(float nearZ, float farZ) = 0;

    /** @brief Draws mesh. */
    virtual void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture,
                          const Color &tint) = 0;
    /** @brief Draws mesh shader. */
    virtual void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture,
                                const Color &tint, Shader *shader) = 0;

    /** @brief Sets the mesh 3 d normal texture. */
    virtual void setMesh3DNormalTexture(Texture *normal) = 0;
    /** @brief Sets the mesh 3 d height texture. */
    virtual void setMesh3DHeightTexture(Texture *height) = 0;
    /** @brief Sets the mesh 3 d scene depth. */
    virtual void setMesh3DSceneDepth(Texture *depth) = 0;
    /** @brief Sets the mesh 3 d scene color. */
    virtual void setMesh3DSceneColor(Texture *color) = 0;
    /** @brief Sets the mesh 3 d material. */
    virtual void setMesh3DMaterial(float metallic, float roughness) = 0;
    /** @brief Copy a validated extended surface for subsequent draws; null resets to legacy shading.
     * @param surface Borrowed snapshot, consumed synchronously on the graphics thread.
     * @return Unsupported when a backend has no extended renderer; reset always succeeds.
     * No pointer to the snapshot is retained; its borrowed textures must outlive queued draws.
     */
    [[nodiscard]] virtual Result<void> setMesh3DPbrSurface(const PbrSurface* surface) {
        if (!surface) return Result<void>::success();
        /** @brief Failure. */
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "extended PBR material rendering is unavailable on this backend"));
    }

    /** @brief Sets the mesh 3 d tex cell bomb. */
    virtual void setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) = 0;
    /** @brief Sets the mesh 3 d parallax. */
    virtual void setMesh3DParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f) = 0;
    /** @brief Sets the mesh 3 d lighting. */
    virtual void setMesh3DLighting(const Lighting3DPack &pack) = 0;
    /** @brief Sets the mesh 3 d clustered lighting. */
    virtual void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) = 0;
    /** @brief Sets the mesh 3 d light. */
    virtual void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) = 0;
    /** @brief Sets the mesh 3 d camera pos. */
    virtual void setMesh3DCameraPos(const glm::vec3 &eye) = 0;
    /** @brief Sets the cloud shadows. */
    virtual void setCloudShadows(float strength, float worldCell, float time, float windSpeed,
                                 float windAngle, float coverage, float detail) = 0;
    /** @brief Sets the mesh 3 d env. */
    virtual void setMesh3DEnv(Texture *cube, float intensity) = 0;
    /** @brief Sets the mesh 3 d env probe. */
    virtual void setMesh3DEnvProbe(const glm::vec3 &center, const glm::vec3 &extent) = 0;
    /** @brief Sets the mesh 3 d reflection probes. */
    virtual void setMesh3DReflectionProbes(const ReflectionProbeUpload &upload) = 0;
    /** @brief Sets the mesh 3 d shadows. */
    virtual void setMesh3DShadows(const ShadowUpload &upload) = 0;
    /** @brief Sets the mesh 3 d shadow receive. */
    virtual void setMesh3DShadowReceive(bool receive) = 0;
    /** @brief Select the influence limit consumed by subsequent mesh draws. */
    virtual void setMesh3DSkinInfluenceLimit(SkinInfluenceLimit count) = 0;

    /** @brief Begins shadow pass. */
    virtual void beginShadowPass(int cascadeIndex) = 0;
    /** @brief Queue an opaque caster with explicit face-culling policy. */
    virtual void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP, bool doubleSided = true) = 0;
    /** @brief Queue an alpha-cutout caster with explicit face-culling policy. */
    virtual void drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP,
                                     Texture *albedo = nullptr, bool doubleSided = true,
                                     float lodWeight = 1.f, bool lodFadeReverse = false,
                                     bool lodDither = false) = 0;
    /** @brief Ends shadow pass. */
    virtual void endShadowPass() = 0;

    /** @brief Begins g buffer pass. */
    virtual void beginGBufferPass(int width, int height) = 0;
    /** @brief Draws mesh g buffer. */
    virtual void drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                 float nearZ, float farZ, Texture *albedo = nullptr,
                                 float tintR = 1.f, float tintG = 1.f, float tintB = 1.f,
                                 float motionX = 0.f, float motionY = 0.f,
                                 float roughness = 0.45f, float metallic = 0.f) = 0;
    /** @brief Draws mesh g buffer alpha. */
    virtual void drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                      float nearZ, float farZ, Texture *albedo = nullptr,
                                      float tintR = 1.f, float tintG = 1.f,
                                      float tintB = 1.f, float motionX = 0.f,
                                      float motionY = 0.f, float roughness = 0.45f,
                                      float metallic = 0.f) = 0;
    /** @brief Ends g buffer pass. */
    virtual void endGBufferPass() = 0;

    /** @brief Draws voxel face instances. */
    virtual void drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                        float originY, float originZ, const std::string &faceDir,
                                        Texture *atlas, int tilesPerRow = 16,
                                        const uint32_t *ao = nullptr) = 0;
};

}  // namespace eve::graphics
