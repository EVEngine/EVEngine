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

class Texture;

struct ReflectionProbeUpload {
    static constexpr int kMaxProbes = 2;
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
    virtual ~IGraphics3D() = default;

    virtual void render3D() = 0;
    virtual void renderScene3DToCanvas(Canvas *canvas, Camera3D *camera) = 0;
    virtual void setDirectionalLight(float dx, float dy, float dz, float r = 1.f, float g = 1.f,
                                     float b = 1.f) = 0;
    virtual void drawScene3DRGBA(float x, float y, float w, float h, float r = 1.f, float g = 1.f,
                                 float b = 1.f, float a = 1.f) = 0;
    virtual void drawCanvasRGBA(Canvas *canvas, float x, float y, float w, float h, float r = 1.f,
                                float g = 1.f, float b = 1.f, float a = 1.f) = 0;

    virtual void begin3DFrame() = 0;
    virtual void begin3DFrameToCanvas(Canvas *canvas) = 0;
    virtual void end3DFrameToCanvas() = 0;

    /**
     * @brief Submits an owning frame-local primitive canvas into the active 3D pass.
     * @param canvas Synchronously consumed command snapshot; it is never retained.
     * @thread Render-thread affine and valid only between begin/end 3D frame calls.
     * @reentrancy Does not invoke scripts or caller callbacks.
     */
    virtual void drawPrimitiveScene(const PrimitiveSceneCanvas3D &canvas) = 0;

    virtual void setMesh3DViewProj(const glm::mat4 &viewProj) = 0;
    virtual void setMesh3DView(const glm::mat4 &view) = 0;
    virtual void setMesh3DClip(float nearZ, float farZ) = 0;

    virtual void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture,
                          const Color &tint) = 0;
    virtual void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture,
                                const Color &tint, Shader *shader) = 0;

    virtual void setMesh3DNormalTexture(Texture *normal) = 0;
    virtual void setMesh3DHeightTexture(Texture *height) = 0;
    virtual void setMesh3DSceneDepth(Texture *depth) = 0;
    virtual void setMesh3DSceneColor(Texture *color) = 0;
    virtual void setMesh3DMaterial(float metallic, float roughness) = 0;
    virtual void setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) = 0;
    virtual void setMesh3DParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f) = 0;
    virtual void setMesh3DLighting(const Lighting3DPack &pack) = 0;
    virtual void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) = 0;
    virtual void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) = 0;
    virtual void setMesh3DCameraPos(const glm::vec3 &eye) = 0;
    virtual void setCloudShadows(float strength, float worldCell, float time, float windSpeed,
                                 float windAngle, float coverage, float detail) = 0;
    virtual void setMesh3DEnv(Texture *cube, float intensity) = 0;
    virtual void setMesh3DEnvProbe(const glm::vec3 &center, const glm::vec3 &extent) = 0;
    virtual void setMesh3DReflectionProbes(const ReflectionProbeUpload &upload) = 0;
    virtual void setMesh3DShadows(const ShadowUpload &upload) = 0;
    virtual void setMesh3DShadowReceive(bool receive) = 0;

    virtual void beginShadowPass(int cascadeIndex) = 0;
    virtual void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) = 0;
    virtual void drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP,
                                     Texture *albedo = nullptr) = 0;
    virtual void endShadowPass() = 0;

    virtual void beginGBufferPass(int width, int height) = 0;
    virtual void drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                 float nearZ, float farZ, Texture *albedo = nullptr,
                                 float tintR = 1.f, float tintG = 1.f, float tintB = 1.f,
                                 float motionX = 0.f, float motionY = 0.f,
                                 float roughness = 0.45f, float metallic = 0.f) = 0;
    virtual void drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                      float nearZ, float farZ, Texture *albedo = nullptr,
                                      float tintR = 1.f, float tintG = 1.f,
                                      float tintB = 1.f, float motionX = 0.f,
                                      float motionY = 0.f, float roughness = 0.45f,
                                      float metallic = 0.f) = 0;
    virtual void endGBufferPass() = 0;

    virtual void drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                        float originY, float originZ, const std::string &faceDir,
                                        Texture *atlas, int tilesPerRow = 16,
                                        const uint32_t *ao = nullptr) = 0;
};

}  // namespace eve::graphics
