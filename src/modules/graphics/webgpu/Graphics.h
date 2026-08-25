#pragma once
#include "graphics/Graphics.h"
#include "graphics/Batcher.h"
#include "graphics/Texture.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Light.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <glm/glm.hpp>

namespace eve::graphics::webgpu {

class OffscreenCanvas;

/**
 * @brief Frame UBO for the mesh3d pipeline. Mirrors the std140 layout of the WGSL
 * `Frame` block and the legacy Vulkan Mesh3DUBO (so custom shader prefixes stay
 * compatible). See wgsl_shaders.h.
 */
struct Mesh3DUBO {
    glm::mat4 mvp{1.f};
    glm::mat4 model{1.f};
    glm::vec4 lightDir{0.4f, 1.f, 0.3f, 0.f};   // xyz = primary dir; w = lightCount
    glm::vec4 lightColor{1.f, 1.f, 1.f, 0.f};    // rgb = primary color; w = envIntensity
    glm::vec4 tint{1.f, 1.f, 1.f, 1.f};
    glm::vec4 cameraPos{0.f, 0.f, 3.f, 0.45f};   // xyz = eye; w = roughness
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f}; // rgb = ambient; w = metallic
    Light3DGpu lights[Lighting3DPack::kMaxLights]{};
    glm::vec4 texBomb{4.f, 0.f, 1.f, 0.f};       // x=cellScale, y=strength, z=rotAmount
    glm::vec4 parallax{0.f, 8.f, 32.f, 0.f};     // x=scale, y=minLayers, z=maxLayers
    glm::vec4 surface{0.f, 0.5f, 0.f, 0.f};       // mode, alphaCutoff, ssao, reserved
    glm::mat4 view{1.f};
    glm::vec4 clipInfo{0.1f, 100.f, 0.f, 0.f};   // x=near, y=far
    glm::vec4 cloud{0.f, 1.5f, 0.f, 0.f};        // x=strength(0=off), y=worldCell, z=time
    glm::vec4 cloudWind{4.f, 0.f, 0.55f, 0.5f};  // xy=wind vel, z=coverage, w=detail
};
static_assert(sizeof(Mesh3DUBO) == 624, "Mesh3DUBO layout must match the WGSL Frame block");

/**
 * @brief Clustered-forward mesh UBO (matches the Vulkan Mesh3DClusteredUBO and
 * the WGSL clustered Frame block).
 */
struct Mesh3DClusteredUBO {
    glm::mat4 mvp{1.f};
    glm::mat4 model{1.f};
    glm::mat4 view{1.f};
    glm::vec4 lightDir{0.4f, 1.f, 0.3f, 0.f};   // xyz = primary dir; w = 1 if valid
    glm::vec4 lightColor{1.f, 1.f, 1.f, 0.f};    // rgb = primary; w = envIntensity
    glm::vec4 tint{1.f, 1.f, 1.f, 1.f};
    glm::vec4 cameraPos{0.f, 0.f, 3.f, 0.45f};   // xyz = eye; w = roughness
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f}; // rgb = ambient; w = metallic
    glm::vec4 gridInfo{16.f, 9.f, 24.f, 0.f};    // tilesX, tilesY, slices, pointCount
    glm::vec4 clipInfo{0.1f, 100.f, 1.f, 1.f};   // near, far, screenW, screenH
    glm::vec4 texBomb{4.f, 0.f, 1.f, 0.f};       // x=cellScale, y=strength, z=rotAmount, w=AO
    glm::vec4 parallax{0.f, 8.f, 32.f, 0.f};     // x=scale, y=minLayers, z=maxLayers
    glm::vec4 surface{0.f, 0.5f, 0.f, 0.f};       // mode, alphaCutoff, ssao, reserved
};
static_assert(sizeof(Mesh3DClusteredUBO) == 352,
              "Mesh3DClusteredUBO layout must match the WGSL clustered Frame block");

/**
 * @brief Texture resources backed by a wgpu texture + view + sampler + bind groups.
 */
struct GpuTexture {
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Sampler sampler;
    // Bind group for the unified 2D layout (color at 0, depth at 1).
    wgpu::BindGroup tex2DGroup;
    // Bind group for the mesh3d layout (albedo at 1, normal at 2, env at 3,
    // height at 6). Rebuilt when the mesh3d pipeline re-creates its layout.
    wgpu::BindGroup meshGroup;
    bool isCube = false;
    int width = 0;
    int height = 0;
    uint32_t mipLevels = 1;
    TextureSampler samplerState{};
};

/**
 * @brief Vertex/index buffers for one mesh.
 */
struct GpuMesh {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    uint64_t vertexCapacity = 0;
    uint64_t indexCapacity = 0;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    uint32_t vertexStride = 0;
    wgpu::IndexFormat indexFormat = wgpu::IndexFormat::Uint32;
};

/**
 * @brief A compiled shader: one WebGPU pipeline + layout. Also holds the WGSL
 * sources so custom shaders can be re-pipelined for offscreen targets.
 */
struct GpuShader {
    wgpu::RenderPipeline swapchainPipeline;   // 2D/offscreen color format
    wgpu::RenderPipeline offscreenPipeline;   // RGBA8Unorm canvas format
    wgpu::RenderPipeline mesh3dPipeline;      // scene color format
    wgpu::RenderPipeline mesh3dXrayPipeline;  // depth test/write off + alpha blend
    wgpu::RenderPipeline shadowPipeline;      // depth-only
    wgpu::RenderPipeline gbufferPipeline;     // MRT gbuffer
    wgpu::PipelineLayout pipelineLayout;
    wgpu::BindGroupLayout setLayout;
    bool isMesh3D = false;
    bool isHair3D = false;
    bool isShadow = false;
    bool isGbuffer = false;
    std::string wgslVert;
    std::string wgslFrag;
};

class Graphics final : public eve::graphics::Graphics {
public:
    // Keep the base draw(Drawable*, mat4) overload visible alongside the
    // canvas composite overloads below.
    using eve::graphics::Graphics::draw;

    Graphics();
    ~Graphics() override;

    std::string getBackendName() const override { return "webgpu"; }
    bool supportsGBufferPost() const override { return true; }
    bool supportsGpuDriven3D() const override { return true; }
    bool gpuDrivenEnabled() const override { return gpuDrivenEnabled_; }
    void gpuDrivenSetEnabled(bool enabled) override { gpuDrivenEnabled_ = enabled; }
    uint32_t gpuDrivenMeshRecord(Mesh *mesh) override;
    uint32_t gpuDrivenMaterialRecord(Material *material) override;
    bool gpuDrivenMaterialUsable(Material *material) override;
    bool gpuDrivenSubmitOpaque(const GpuInstance *instances, uint32_t instanceCount) override;
    bool gpuDrivenCullEnabled() const override { return gpuDrivenEnabled_; }
    bool gpuDrivenCullBegin(const GpuInstance *instances, uint32_t instanceCount) override;
    void gpuDrivenCullEmit(const glm::mat4 &viewProj, const glm::vec3 &eye, float fovYDeg,
                           float nearZ, float farZ) override;
    void gpuDrivenDrawOpaque() override;
    bool gpuDrivenResolveWanted() const override;
    void gpuDrivenRecordVisPass() override;
    void gpuDrivenResolve() override;
    uint32_t gpuDrivenVgUpload(const GpuVgAssetUpload &asset) override;
    uint32_t gpuDrivenVgAssetId(Mesh *mesh) const override;
    bool gpuDrivenVgAttachToMesh(Mesh *mesh, uint32_t vgAssetId) override;
    bool gpuDrivenVgSetInstance(uint32_t vgAssetId, const glm::mat4 &model,
                                uint32_t materialId) override;
    void gpuDrivenVgComputeSection(const glm::mat4 &viewProj, const glm::vec3 &eye,
                                   float fovYDeg, float nearZ, float farZ) override;
    /** @brief Return the last CPU compatibility-cull result for backend parity tests. */
    uint32_t debugGpuDrivenVisibleCount() const {
        return static_cast<uint32_t>(gpuDrivenVisible_.size());
    }
    /** @brief Return instances dispatched by the last WebGPU compute cull. */
    uint32_t debugGpuDrivenDispatchCount() const { return gpuDrivenDispatchCount_; }
    /** @brief Return indexed-indirect commands recorded by the last scene pass. */
    uint32_t debugGpuDrivenIndirectDrawCount() const { return gpuDrivenLastIndirectDrawCount_; }
    /** @brief Read back the last GPU-written indirect instance total (native tests). */
    uint32_t debugGpuDrivenGpuVisibleCount();
    /** @brief Return the active hierarchical-depth mip count for conformance tests. */
    uint32_t debugGpuDrivenHzbMipCount() const {
        return static_cast<uint32_t>(gpuDrivenHzbOffsets_.size());
    }
    /** @brief Return VG clusters dispatched by the last WebGPU compute section. */
    uint32_t debugGpuDrivenVgDispatchCount() const { return gpuDrivenVgVisibleDiagnostic_; }
    /** @brief Return per-cluster indirect commands recorded by the VG visibility pass. */
    uint32_t debugGpuDrivenVgIndirectDrawCount() const {
        return gpuDrivenVgLastIndirectDrawCount_;
    }
    /** @brief Read back the last VG indirect instance total (native tests). */
    uint32_t debugGpuDrivenVgGpuVisibleCount();

    void initHeadless(int width, int height) override;
    void initWithWindow(void *nativeWindow) override;
    void present() override;
    void pushValidationScope() override;
    void popValidationScope() override;
    void setMsaaSamples(int samples) override;
    void requestSurfaceRecreate() override { surfaceNeedsRecreate = true; }
    void setVSync(bool enabled) override;
    int getMsaaSamples() const override { return msaaSamples; }
    void setViewportSize(int width, int height, int pixelwidth, int pixelheight) override;
    void drawSolidRect(float x, float y, float w, float h, const Color &color,
                       BlendMode blend = BlendMode::Alpha) override;
    void drawSolidRectRotated(float cx, float cy, float w, float h, float degrees,
                              const Color &color,
                              BlendMode blend = BlendMode::Alpha) override;

    Texture *newTexture(int width, int height, const uint8_t *rgba, bool repeatU = false,
                        bool repeatV = false) override;
    Texture *newTexture(int width, int height, const uint8_t *rgba,
                        const TextureCreateInfo &info) override;
    Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces) override;
    Texture *newCubemap(int faceSize, const uint8_t *rgbaFaces,
                        const TextureCreateInfo &info) override;
    Texture *newTexture(image::ImageData *data) override;
    Texture *newTexture(image::ImageData *data, const TextureCreateInfo &info) override;
    void setTextureSampler(Texture *texture, const TextureSampler &sampler) override;
    float getMaxAnisotropy() const override;
    Texture *newTextureFromFile(const std::string &filename) override;
    bool reloadTextureFromFile(const std::string &filename) override;
    bool releaseTexture(Texture *texture) override;
    bool updateTexture(Texture *texture, int width, int height,
                       const uint8_t *rgba) override;

    void drawTexturedRect(Texture *texture, float x, float y, float w, float h,
                          const Color &color) override;
    void drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w, float h,
                                const Color &color) override;
    void drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0, float v0,
                            float u1, float v1, const Color &color) override;
    void drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y, float w,
                                  float h, float u0, float v0, float u1, float v1,
                                  const Color &color, bool rotatedUV = false,
                                  BlendMode blend = BlendMode::Alpha) override;
    void drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx, float cy,
                                         float w, float h, float degrees, float u0, float v0,
                                         float u1, float v1, const Color &color,
                                         bool rotatedUV = false,
                                         BlendMode blend = BlendMode::Alpha) override;
    void drawTexturedRectShaderDepth(Texture *color, Texture *depth, Shader *shader, float x, float y,
                                     float w, float h, const Color &tint) override;
    void drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w, float h,
                               float u0, float v0, float u1, float v1, const Color &color) override;
    void setLighting2D(const Lighting2DUBO &ubo) override;

    Shader *newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                             const std::vector<uint32_t> &fragSpv) override;
    Shader *newShaderFromSpvFile(const std::string &vertPath, const std::string &fragPath) override;
    Shader *newShaderFromWgsl(const std::string &vertWgsl,
                              const std::string &fragWgsl) override;
    Shader *newShader(const std::string &vertGlsl, const std::string &fragGlsl) override;
    Shader *newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                 const std::vector<uint32_t> &fragSpv) override;
    Shader *newMeshShaderFromWgsl(const std::string &vertWgsl,
                                  const std::string &fragWgsl) override;
    Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) override;
    Shader *newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                 const std::vector<uint32_t> &fragSpv) override;
    Shader *newHairShaderFromWgsl(const std::string &vertWgsl,
                                  const std::string &fragWgsl) override;
    bool releaseShader(Shader *shader) override;

    Mesh *newMeshFromAssimp(const ::aiMesh &mesh) override;
    Mesh *newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) override;
    Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                            int vertexCount, const uint32_t *indices, int indexCount) override;
    bool bakeMeshMorph(Mesh *mesh) override;
    bool updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ, const float *uvST,
                            int vertexCount, const uint32_t *indices, int indexCount) override;
    Mesh *newMeshSphere(int slices = 32, int stacks = 16) override;
    Mesh *newMeshCylinder(int slices = 32, int stacks = 1, bool caps = true) override;
    bool releaseMesh(Mesh *mesh) override;

    void begin3DFrame() override;
    void begin3DFrameToCanvas(Canvas *canvas) override;
    void end3DFrameToCanvas() override;
    void setMesh3DViewProj(const glm::mat4 &viewProj) override;
    void setMesh3DView(const glm::mat4 &view) override;
    void setMesh3DClip(float nearZ, float farZ) override;
    Texture *getSceneColorTexture() override;
    void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) override;
    void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                        Shader *shader) override;
    void drawVoxelFaceInstances(const uint32_t *packed, int count, float originX, float originY,
                                float originZ, const std::string &faceDir, Texture *atlas,
                                int tilesPerRow = 16, const uint32_t *ao = nullptr) override;
    void setMesh3DNormalTexture(Texture *normal) override;
    void setMesh3DHeightTexture(Texture *height) override;
    void     setMesh3DSceneDepth(Texture *depth) override;
    void     setMesh3DMaterial(float metallic, float roughness) override;
    void     setMesh3DSurface(SurfaceMode mode, BlendMode blend, bool depthWrite,
                              bool doubleSided, float alphaCutoff,
                              const std::string &alphaTechnique = "cutoff") override;
    void     setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) override;
    void     setMesh3DParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f) override;
    void     setMesh3DLighting(const Lighting3DPack &pack) override;
    void setCloudShadows(float strength, float worldCell, float time, float windSpeed, float windAngle, float coverage,
                         float detail) override;
    void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) override;
    void setMesh3DClusteredActive(bool active) override;
    void setMesh3DSSAO(float intensity) override;
    void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) override;
    void setMesh3DCameraPos(const glm::vec3 &eye) override;
    void setMesh3DEnv(Texture *cube, float intensity) override;
    void setMesh3DShadows(const ShadowUpload &upload) override;
    void setMesh3DShadowReceive(bool receive) override;
    void beginShadowPass(int cascadeIndex) override;
    void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) override;
    void drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP, Texture *albedo = nullptr) override;
    void endShadowPass() override;

    void beginGBufferPass(int width, int height) override;
    void drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                         float farZ, Texture *albedo = nullptr, float tintR = 1.f, float tintG = 1.f,
                         float tintB = 1.f) override;
    void drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                              float nearZ, float farZ, Texture *albedo = nullptr, float tintR = 1.f,
                              float tintG = 1.f, float tintB = 1.f) override;
    void endGBufferPass() override;
    image::ImageData *readGBufferToImageData(const std::string &attachment) override;

    bool supportsDecal() const override { return true; }
    void beginDecalPass(int width, int height) override;
    void setDecalCamera(const glm::mat4 &viewProj, float nearZ, float farZ) override;
    void drawDecal(const glm::mat4 &model, Texture *albedo, Texture *normal, Texture *params,
                   const float uvRect[4], float fade, float normalStrength, float roughnessStrength,
                   float metalStrength, float emissiveStrength, int blendMode = 0) override;
    void endDecalPass() override;
    image::ImageData *readDecalLayerToImageData(const std::string &attachment) override;

    Canvas *newCanvas(int width, int height) override;
    void setCanvas(Canvas *canvas) override;
    bool isCanvasActive() const override;
    Canvas *getCanvas() const override;

    Texture *getTexture() override;
    image::ImageData *newImageData() override;
    bool beginFrameReadback(const std::string &path) override;
    int frameReadbackStatus() const override;
    /** @brief Advances a pending frame readback; called every present(). */
    void pumpReadback();
    void draw(eve::graphics::Graphics *gfx, const glm::mat4 &matrix) const override;
    void draw(Canvas *C, const glm::mat4 &matrix) const override;
    void clear(std::optional<Color> color, std::optional<int> stencil,
               std::optional<double> depth) override;
    Color getPixel(int x, int y) override;

    /** @brief Flush accumulated 2D batches into an offscreen canvas target. */
    void flush2DToCanvas(OffscreenCanvas *canvas);
    /** @brief Blocking CPU readback of an offscreen canvas or scene color target. */
    Color getPixelImpl(OffscreenCanvas *canvas, int x, int y);
    image::ImageData *newImageDataImpl(OffscreenCanvas *canvas);

    wgpu::Instance &getInstance() { return instance; }
    wgpu::Device &getDevice() { return device; }
    wgpu::Queue &getQueue() { return queue; }
    wgpu::Surface &getSurface() { return surface; }
    WGPUTextureFormat getSurfaceFormat() const { return surfaceFormat; }
    void *getSdlWindow() const { return sdlWindow; }
    bool isReady() const { return initialized; }

    // The present overlay is rendered inside the swapchain render pass. The
    // void* payload is a WGPURenderPassEncoder* on the WebGPU backend.
    using PresentOverlayFn = eve::graphics::Graphics::PresentOverlayFn;

    friend class OffscreenCanvas;

    struct SolidBatch {
        BlendMode blend = BlendMode::Alpha;
        Batcher batch;
    };
    struct TexturedBatch {
        Texture *texture = nullptr;
        Texture *depth = nullptr;
        Shader *shader = nullptr;
        BlendMode blend = BlendMode::Alpha;
        Batcher batch;
    };
    struct LitBatch {
        Texture *albedo = nullptr;
        Texture *normal = nullptr;
        Batcher batch;
    };

private:
    struct Mesh3dDraw {
        Mesh *mesh = nullptr;
        Texture *texture = nullptr;
        glm::mat4 model{1.f};
        Color tint{1.f};
        Shader *shader = nullptr;
        SurfaceMode surfaceMode = SurfaceMode::Opaque;
        BlendMode surfaceBlend = BlendMode::Alpha;
        bool depthWrite = false;
        bool doubleSided = false;
        bool shadowReceive = true;
        float alphaCutoff = 0.5f;
        std::string alphaTechnique = "cutoff";
        uint32_t frameUboOffset = 0;
        uint32_t pushUboOffset = 0;
        uint32_t shadowUboOffset = 0;
        uint32_t clusteredUboOffset = 0;
    };
    struct ShadowDraw {
        Mesh *mesh = nullptr;
        Texture *albedo = nullptr;
        glm::mat4 mvp{1.f};
        bool alphaTest = false;
    };
    struct GbufferDraw {
        Mesh *mesh = nullptr;
        Texture *albedo = nullptr;
        glm::mat4 mvp{1.f};
        glm::mat4 model{1.f};
        float nearZ = 0.1f;
        float farZ = 100.f;
        glm::vec4 tint{1.f};
        bool alphaTest = false;
        uint32_t pushUboOffset = 0;
    };
    struct VoxelDraw {
        uint32_t instanceBufferOffset = 0;
        uint32_t aoBufferOffset = 0;
        uint32_t count = 0;
        GpuTexture *atlas = nullptr;
        glm::mat4 viewProj{1.f};
        glm::vec4 chunkOrigin{0.f};
        glm::vec4 atlasInfo{16.f, 0.f, 0.f, 0.f};
        glm::vec4 tint{1.f};
        uint32_t pushUboOffset = 0;
    };
    struct DecalDraw {
        glm::mat4 model{1.f};
        Texture *albedo = nullptr;
        Texture *normal = nullptr;
        Texture *params = nullptr;
        glm::vec4 uvRect{0.f, 0.f, 1.f, 1.f};
        glm::vec4 fadeParams{1.f, 0.f, 0.f, 0.f};
        glm::vec4 extraParams{0.f};
    };

    void createInstanceAndAdapter();
    void requestDevice();
    void configureSurface(int width, int height);
    void waitForAdapter();
    void waitForDevice();
    void createDefaultTextures();
    void createPipelineResources();
    void create2DPipelines();
    void createMesh3DPipelines();
    void createMesh3DClusteredPipeline();
    void createShadowPipelines();
    void createGbufferPipelines();
    void createDecalPipeline();
    void createVoxelPipelines();
    void ensureGpuDrivenResources(uint32_t instanceCount, uint32_t bucketCount);
    void recordGpuDrivenCompute(wgpu::CommandEncoder encoder);
    void flushGpuDrivenDraws(wgpu::RenderPassEncoder pass, bool canvasTarget);
    void ensureGpuDrivenVisibilityResources();
    void recordGpuDrivenVisibility(wgpu::CommandEncoder encoder);
    void flushGpuDrivenResolve(wgpu::RenderPassEncoder pass);
    void ensureGpuDrivenVgResources();
    void recordGpuDrivenVgCompute(wgpu::CommandEncoder encoder);
    void recordGpuDrivenVgVisibility(wgpu::CommandEncoder encoder);
    void flushGpuDrivenVgResolve(wgpu::RenderPassEncoder pass);
    void createSceneColorResources(int width, int height);
    void destroySceneColorResources();
    void createShadowResources();
    void destroyShadowResources();
    void createGbufferResources(int width, int height);
    void destroyGbufferResources();
    void createDecalResources(int width, int height);
    void destroyDecalResources();

    wgpu::RenderPipeline createPipelineForShader(GpuShader *gs, wgpu::TextureFormat format,
                                                 bool depth, bool mesh3d, bool hair,
                                                 bool shadow, bool gbuffer,
                                                 wgpu::PipelineLayout layout);
    wgpu::BindGroupLayout make2DBindGroupLayout();
    wgpu::BindGroupLayout makeMesh3DBindGroupLayout();
    wgpu::BindGroupLayout makeMesh3DClusteredBindGroupLayout();
    wgpu::BindGroupLayout makeShadowBindGroupLayout();
    wgpu::BindGroupLayout makeGbufferBindGroupLayout();
    wgpu::BindGroupLayout makeDecalBindGroupLayout();
    wgpu::BindGroupLayout makeVoxelBindGroupLayout();
    wgpu::PipelineLayout make2DPipelineLayout();
    wgpu::PipelineLayout makeMesh3DPipelineLayout();
    wgpu::PipelineLayout makeMesh3DClusteredPipelineLayout();
    wgpu::PipelineLayout makeShadowPipelineLayout();
    wgpu::PipelineLayout makeGbufferPipelineLayout();
    wgpu::PipelineLayout makeDecalPipelineLayout();
    wgpu::PipelineLayout makeVoxelPipelineLayout();

    GpuTexture *gpuForTexture(Texture *t) const;
    GpuTexture *gpuForTextureOrWhite(Texture *t) const;
    wgpu::BindGroup makeTex2DBindGroup(GpuTexture *color, GpuTexture *depth);
    wgpu::BindGroup makeMeshBindGroup(GpuTexture *albedo, GpuTexture *normal, GpuTexture *env,
                                      GpuTexture *height, GpuTexture *depth,
                                      uint32_t frameUboOffset, uint32_t shadowUboOffset,
                                      uint32_t pushUboOffset);
    wgpu::BindGroup makeMesh3DClusteredBindGroup(GpuTexture *albedo, GpuTexture *normal,
                                                 GpuTexture *env, GpuTexture *height,
                                                 GpuTexture *depth, wgpu::TextureView aoView,
                                                 uint32_t frameUboOffset, uint32_t shadowUboOffset);
    void uploadClusteredLighting(const ClusteredLightingUpload &upload);
    void ensureMeshBindGroupsForDraw(Mesh3dDraw &d);
    wgpu::Sampler makeSampler(const TextureSampler &sampler, uint32_t mipLevels) const;

    void uploadTexturePixels(GpuTexture *gt, const uint8_t *rgba, int w, int h,
                             const TextureCreateInfo &info);
    void uploadTexturePixelsMips(GpuTexture *gt, const uint8_t *rgba, int w, int h);
    void flush2D(wgpu::RenderPassEncoder pass, int viewW, int viewH, WGPUTextureFormat format);
    void drawTexturedBatch(wgpu::RenderPassEncoder pass, TexturedBatch &tb, int viewW, int viewH,
                           WGPUTextureFormat format, bool offscreen);
    void drawLitBatch(wgpu::RenderPassEncoder pass, LitBatch &lb, int viewW, int viewH,
                      WGPUTextureFormat format);
    void flushMesh3D(wgpu::RenderPassEncoder pass, WGPUTextureFormat format,
                     bool canvasTarget = false);
    void flushShadowPass(wgpu::RenderPassEncoder pass, int cascade);
    void flushGbufferPass(wgpu::RenderPassEncoder pass);
    void flushDecalPass(wgpu::RenderPassEncoder pass);
    void submitPendingDeferredPasses();
    void flushVoxelDraws(wgpu::RenderPassEncoder pass, WGPUTextureFormat format);

    // UBO arena: one growable uniform buffer per in-flight frame slot.
    struct UboArena {
        wgpu::Buffer buffer;
        uint64_t capacity = 0;
        uint64_t used = 0;
        uint32_t alloc(uint64_t size, uint64_t alignment);
        void reset() { used = 0; }
    };
    UboArena &currentUboArena();
    void ensureUboArena(UboArena &arena, uint64_t bytes);

    // Vertex arena for batched 2D vertices (one per frame slot).
    struct VertexArena {
        wgpu::Buffer buffer;
        uint64_t capacity = 0;
        uint64_t used = 0;
        uint64_t alloc(uint64_t bytes);
        void reset() { used = 0; }
    };
    VertexArena &currentVertexArena();
    void ensureVertexArena(VertexArena &arena, uint64_t bytes);

    uint32_t frameSlotCount() const { return kFramesInFlight; }
    uint32_t currentFrameSlot() const { return frameIndex % kFramesInFlight; }

    // Per-frame instance arena for voxel faces (packed rect words).
    VertexArena voxelInstanceArena;
    // Parallel per-frame arena for the AO word (2 bits per corner).
    VertexArena voxelAoArena;

    // ---- state ----
    bool initialized = false;
    bool deviceInitDone = false;
    bool headless_ = false;
    void *sdlWindow = nullptr;
    int logicalW = 0, logicalH = 0;
    int pixelW = 0, pixelH = 0;
    float maxSamplerAnisotropy = 1.f;

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    std::atomic<bool> surfaceNeedsRecreate{false};
    bool swapchainConfigured = false;
    std::atomic<bool> adapterReceived{false};
    std::atomic<bool> deviceReceived{false};
    std::string adapterError;
    std::string deviceError;

    // Per-frame command state (single command buffer per frame).
    uint32_t frameIndex = 0;
    // Slot rendered by the most recent present (the readback source).
    uint32_t lastPresentSlot = 0;
    static constexpr uint32_t kFramesInFlight = 2;
    std::vector<UboArena> uboArenas;
    std::vector<VertexArena> vertexArenas;
    wgpu::Buffer mesh3dFrameUboPool;
    uint32_t mesh3dFrameUboSlots[kFramesInFlight]{};
    wgpu::Buffer shadowUboPool;
    uint32_t shadowUboSlots[kFramesInFlight]{};
    wgpu::Buffer pushUboPool;
    uint32_t pushUboSlots[kFramesInFlight]{};

    // Default / placeholder resources.
    GpuTexture *whiteTexture = nullptr;
    GpuTexture *flatNormalTexture = nullptr;
    GpuTexture *flatNormalTexture3D = nullptr;
    GpuTexture *flatHeightTexture3D = nullptr;
    GpuTexture *flatDepthTexture3D = nullptr;
    GpuTexture *defaultEnvCubemap = nullptr;
    GpuTexture *shadowDepthArray = nullptr;
    // 1x1x3 depth-array + comparison sampler used for the mesh3d shadow
    // bindings (5/8) when no shadow map exists yet — the bind group layout
    // requires those bindings on every draw.
    GpuTexture *defaultShadowTex = nullptr;

    // Pipelines / layouts.
    wgpu::PipelineLayout tex2DPipelineLayout;
    wgpu::PipelineLayout mesh3dPipelineLayout;
    wgpu::RenderPipeline mesh3dCanvasPipeline;
    wgpu::BindGroupLayout mesh3dClusteredSetLayout;
    wgpu::PipelineLayout mesh3dClusteredPipelineLayout;
    wgpu::RenderPipeline mesh3dClusteredPipeline;
    // Double-buffered storage ring for the clustered-forward SSBOs (lights /
    // cluster table / light indices), one slot per frame in flight.
    struct ClusteredStorage {
        wgpu::Buffer lights;
        wgpu::Buffer table;
        wgpu::Buffer indices;
        uint64_t lightsCap = 0;
        uint64_t tableCap = 0;
        uint64_t indicesCap = 0;
    } clusteredStorage[kFramesInFlight];
    wgpu::PipelineLayout shadowPipelineLayout;
    wgpu::PipelineLayout gbufferPipelineLayout;
    wgpu::PipelineLayout decalPipelineLayout;
    wgpu::PipelineLayout voxelPipelineLayout;
    wgpu::BindGroupLayout tex2DSetLayout;
    wgpu::BindGroupLayout mesh3dSetLayout;
    wgpu::BindGroupLayout shadowSetLayout;
    wgpu::BindGroupLayout gbufferSetLayout;
    wgpu::BindGroupLayout decalSetLayout;
    wgpu::BindGroupLayout voxelSetLayout;

    // SSAO (screen-space ambient occlusion) resources. The AO pass runs after
    // the G-buffer fill and writes aoTex[aoWriteIndex]; the forward mesh pass
    // samples the other slot (one frame of latency).
    wgpu::PipelineLayout aoPipelineLayout;
    wgpu::RenderPipeline aoPipeline;
    wgpu::BindGroupLayout aoSetLayout;
    wgpu::Texture aoTex[2];
    wgpu::TextureView aoView[2];
    uint32_t aoWriteIndex = 0;
    bool aoReady = false;
    wgpu::Buffer aoUbo;
    void ensureAOResources(int width, int height);
    wgpu::BindGroup makeAOBindGroup(wgpu::TextureView depthView);
    // Shared filtering sampler for bindings declared as `sampler` in WGSL
    // (e.g. mesh3d's @binding(7) mainSamp).
    wgpu::Sampler mainSampler;
    wgpu::RenderPipeline colorPipeline;      // 2D solid
    wgpu::RenderPipeline texturedPipeline;   // 2D textured
    wgpu::RenderPipeline colorAdditivePipeline;
    wgpu::RenderPipeline texturedAdditivePipeline;
    wgpu::RenderPipeline colorPremultipliedPipeline;
    wgpu::RenderPipeline texturedPremultipliedPipeline;
    wgpu::RenderPipeline colorMultiplyPipeline;
    wgpu::RenderPipeline texturedMultiplyPipeline;
    wgpu::RenderPipeline colorOpaquePipeline;
    wgpu::RenderPipeline texturedOpaquePipeline;
    wgpu::RenderPipeline mesh3dPipeline;
    wgpu::RenderPipeline mesh3dTransparentPipeline;
    static constexpr size_t kMeshPipelineVariants = 20;
    std::array<wgpu::RenderPipeline, kMeshPipelineVariants> mesh3dPipelines;
    std::array<wgpu::RenderPipeline, kMeshPipelineVariants> mesh3dCanvasPipelines;
    wgpu::RenderPipeline mesh3dShadowPipeline;
    wgpu::RenderPipeline mesh3dShadowAlphaPipeline;
    wgpu::RenderPipeline mesh3dGbufferPipeline;
    wgpu::RenderPipeline mesh3dGbufferAlphaPipeline;
    wgpu::RenderPipeline decalPipeline;
    wgpu::RenderPipeline voxelRectPipeline;
    wgpu::RenderPipeline lit2dPipeline;
    // RGBA8Unorm (offscreen canvas / scene) variants of the 2D pipelines.
    wgpu::RenderPipeline offscreenColorPipeline;
    wgpu::RenderPipeline offscreenTexturedPipeline;
    wgpu::RenderPipeline offscreenColorAdditivePipeline;
    wgpu::RenderPipeline offscreenTexturedAdditivePipeline;
    wgpu::RenderPipeline offscreenColorPremultipliedPipeline;
    wgpu::RenderPipeline offscreenTexturedPremultipliedPipeline;
    wgpu::RenderPipeline offscreenColorMultiplyPipeline;
    wgpu::RenderPipeline offscreenTexturedMultiplyPipeline;
    wgpu::RenderPipeline offscreenColorOpaquePipeline;
    wgpu::RenderPipeline offscreenTexturedOpaquePipeline;
    wgpu::RenderPipeline offscreenLitPipeline;
    // Fullscreen quad used to composite the scene color into the swapchain.
    wgpu::Buffer fullscreenQuadVb;
    wgpu::Buffer fullscreenQuadIb;
    bool fullscreenQuadReady = false;

    // 2D batch state.
    std::vector<SolidBatch> solidBatches;
    std::vector<TexturedBatch> texturedBatches;
    std::vector<LitBatch> litBatches;
    Lighting2DUBO lighting2dFrame{};
    enum class OverlayKind : uint8_t { Solid, Textured, Lit };
    struct OverlaySpan {
        OverlayKind kind = OverlayKind::Solid;
        uint32_t index = 0;
        uint32_t vertBegin = 0;
        uint32_t vertCount = 0;
    };
    std::vector<OverlaySpan> overlaySpans;
    bool sceneColorComposited = false;
    void noteSolidOverlay(uint32_t batchIndex);
    void noteTexturedOverlay(Texture *tex, uint32_t batchIndex);
    void noteLitOverlay(uint32_t batchIndex);
    void clear2DBatches();

    // 3D frame state.
    bool frame3DStarted = false;
    bool sceneColorPassOpen = false;
    // Non-null between begin3DFrameToCanvas and the frame's present: the 3D
    // scene pass renders into this canvas instead of the scene color target.
    OffscreenCanvas *active3DCanvas = nullptr;
    // Most recent 3D render target (scene color or canvas), used by the async
    // frame readback.
    wgpu::Texture lastReadbackTex;
    int lastReadbackW = 0;
    int lastReadbackH = 0;
    glm::mat4 mesh3dViewProj{1.f};
    glm::mat4 mesh3dView{1.f};
    float mesh3dNear = 0.1f, mesh3dFar = 100.f;
    Texture *sceneColorTexture = nullptr;
    Texture *mesh3dNormalTexture = nullptr;
    Texture *mesh3dHeightTexture = nullptr;
    Texture *mesh3dEnvTexture = nullptr;
    Texture *mesh3dSceneDepthTexture = nullptr;
    float mesh3dEnvIntensity = 0.f;
    float mesh3dMetallic = 0.f;
    float mesh3dRoughness = 0.45f;
    SurfaceMode mesh3dSurfaceMode = SurfaceMode::Opaque;
    BlendMode mesh3dSurfaceBlend = BlendMode::Alpha;
    bool mesh3dSurfaceDepthWrite = false;
    bool mesh3dSurfaceDoubleSided = false;
    float mesh3dAlphaCutoff = 0.5f;
    std::string mesh3dAlphaTechnique = "cutoff";
    float mesh3dTexBombScale = 4.f, mesh3dTexBombStrength = 0.f, mesh3dTexBombRot = 1.f;
    float mesh3dParallaxScale = 0.f, mesh3dParallaxMin = 8.f, mesh3dParallaxMax = 32.f;
    float mesh3dSsaoIntensity = 1.f;
    glm::vec4 mesh3dCloud{0.f, 1.5f, 0.f, 0.f};
    glm::vec4 mesh3dCloudWind{4.f, 0.f, 0.55f, 0.5f};
    Lighting3DPack mesh3dLighting{};
    ShadowUpload mesh3dShadows{};
    bool mesh3dShadowReceive = true;
    bool mesh3dClusteredActive = false;
    ClusteredLightingUpload mesh3dClustered{};
    glm::vec3 mesh3dCameraPos{0.f, 0.f, 3.f};
    bool frameHad3DThisFrame = false;
    std::vector<Mesh3dDraw> mesh3dDraws;
    Color clearColor{0.1f, 0.1f, 0.12f, 1.f};
    bool hasPendingClear = true;

    // Shadow pass state.
    int shadowPassCascade = -1;
    std::vector<ShadowDraw> shadowPassDraws;
    std::vector<ShadowDraw> shadowCascadeDraws[ShadowConfig::kCascades];

    // GBuffer pass state.
    bool gbufferPassActive = false;
    bool gbufferPassPending = false;
    std::vector<GbufferDraw> gbufferPassDraws;

    // Voxel state.
    std::vector<VoxelDraw> voxelDraws;
    wgpu::Buffer voxelUnitQuadVerts;
    wgpu::Buffer voxelUnitQuadIndices;
    // Scene color (offscreen 3D) target.
    struct SceneColorSlot {
        wgpu::Texture msaaColor;
        wgpu::TextureView msaaView;
        wgpu::Texture color;
        wgpu::TextureView colorView;
        wgpu::Texture depth;
        wgpu::TextureView depthView;
        GpuTexture colorGpu;
        Texture colorTex;
        uint32_t sampleCount = 1;
    };
    int sceneColorWidth = 0, sceneColorHeight = 0;
    WGPUTextureFormat sceneColorFormat = WGPUTextureFormat_RGBA8Unorm;
    uint32_t sceneColorSamples = 1;
    std::vector<SceneColorSlot> sceneColorSlots;

    // Shadow map state (CSM, 3 cascade layers).
    struct ShadowMapSlot {
        wgpu::Texture texture;
        wgpu::TextureView view;
    };
    std::vector<ShadowMapSlot> shadowMaps;
    int shadowMapSize = ShadowConfig::kMapSize;

    // GBuffer targets.
    struct GbufferSlot {
        wgpu::Texture normal;
        wgpu::TextureView normalView;
        wgpu::Texture depthColor;
        wgpu::TextureView depthColorView;
        wgpu::Texture albedo;
        wgpu::TextureView albedoView;
        wgpu::Texture depth;
        wgpu::TextureView depthView;
        wgpu::Texture visID;
        wgpu::TextureView visIDView;
        wgpu::Texture visBary;
        wgpu::TextureView visBaryView;
        GpuTexture normalGpu;
        GpuTexture depthColorGpu;
        GpuTexture albedoGpu;
        GpuTexture depthGpu;
        Texture normalTex;
        Texture depthColorTex;
        Texture albedoTex;
        Texture depthTex;
    };
    int gbufferWidth = 0, gbufferHeight = 0;
    std::vector<GbufferSlot> gbufferSlots;
    uint32_t lastGbufferSlot = 0;
    bool gbufferDepthValid_ = false;

    struct DecalSlot {
        wgpu::Texture albedo;
        wgpu::TextureView albedoView;
        wgpu::Texture normal;
        wgpu::TextureView normalView;
        wgpu::Texture params;
        wgpu::TextureView paramsView;
    };
    int decalWidth = 0, decalHeight = 0;
    std::vector<DecalSlot> decalSlots;
    std::vector<DecalDraw> decalPassDraws;
    glm::mat4 decalViewProj{1.f};
    bool decalPassActive = false;
    bool decalPassPending = false;
    bool decalReady = false;
    uint32_t lastDecalSlot = 0;
    Texture *decalFlatAlbedo = nullptr;
    Texture *decalFlatNormal = nullptr;
    Texture *decalFlatParams = nullptr;

    // Canvas state.
    Canvas *activeCanvas = nullptr;
    std::vector<std::unique_ptr<eve::graphics::Canvas>> ownedCanvases;

    // Owned resources.
    std::vector<std::unique_ptr<Texture>> ownedTextures;
    std::vector<std::unique_ptr<GpuTexture>> ownedGpuTextures;
    std::unordered_map<std::string, Texture *> texturesByPath;
    std::vector<std::unique_ptr<Mesh>> ownedMeshes;
    std::vector<std::unique_ptr<GpuMesh>> ownedGpuMeshes;
    std::vector<std::unique_ptr<Shader>> ownedShaders;
    std::vector<std::unique_ptr<GpuShader>> ownedGpuShaders;

    // GPU-driven resource tables. WebGPU groups instances into conventional
    // mesh/material buckets (portable replacement for Vulkan descriptor indexing),
    // then compute-compacts visible transforms and emits indirect commands.
    bool gpuDrivenEnabled_ = false;
    std::vector<Mesh *> gpuDrivenMeshes_;
    std::vector<Material *> gpuDrivenMaterials_;
    std::unordered_map<Mesh *, uint32_t> gpuDrivenMeshIds_;
    std::unordered_map<Material *, uint32_t> gpuDrivenMaterialIds_;
    std::vector<GpuInstance> gpuDrivenPending_;
    // CPU mirror is diagnostic-only; rendering consumes the compute output.
    std::vector<GpuInstance> gpuDrivenVisible_;
    struct GpuDrivenBucket {
        Mesh *mesh = nullptr;
        Material *material = nullptr;
        uint32_t outputBase = 0;
        uint32_t inputCount = 0;
    };
    std::vector<GpuDrivenBucket> gpuDrivenBuckets_;
    wgpu::BindGroupLayout gpuDrivenComputeSetLayout_;
    wgpu::BindGroupLayout gpuDrivenRenderSetLayout_;
    wgpu::PipelineLayout gpuDrivenComputePipelineLayout_;
    wgpu::PipelineLayout gpuDrivenRenderPipelineLayout_;
    wgpu::ComputePipeline gpuDrivenCullPipeline_;
    wgpu::BindGroupLayout gpuDrivenHzbSetLayout_;
    wgpu::PipelineLayout gpuDrivenHzbPipelineLayout_;
    wgpu::ComputePipeline gpuDrivenHzbPipeline_;
    wgpu::Buffer gpuDrivenHzbBuffer_;
    wgpu::Buffer gpuDrivenHzbParamsBuffer_;
    wgpu::BindGroup gpuDrivenHzbBindGroup_;
    std::vector<uint32_t> gpuDrivenHzbOffsets_;
    uint32_t gpuDrivenHzbWidth_ = 0;
    uint32_t gpuDrivenHzbHeight_ = 0;
    uint64_t gpuDrivenHzbCapacity_ = 0;
    wgpu::RenderPipeline gpuDrivenRenderPipeline_;
    wgpu::RenderPipeline gpuDrivenCanvasPipeline_;
    wgpu::Buffer gpuDrivenParamsBuffer_;
    wgpu::Buffer gpuDrivenInputBuffer_;
    wgpu::Buffer gpuDrivenVisibleBuffer_;
    wgpu::Buffer gpuDrivenIndirectBuffer_;
    wgpu::Buffer gpuDrivenVisIndirectBuffer_;
    wgpu::BindGroup gpuDrivenComputeBindGroup_;
    wgpu::BindGroup gpuDrivenRenderBindGroup_;
    uint64_t gpuDrivenInputCapacity_ = 0;
    uint64_t gpuDrivenVisibleCapacity_ = 0;
    uint64_t gpuDrivenIndirectCapacity_ = 0;
    uint64_t gpuDrivenVisIndirectCapacity_ = 0;
    uint32_t gpuDrivenDispatchCount_ = 0;
    uint32_t gpuDrivenLastIndirectDrawCount_ = 0;
    uint32_t gpuDrivenLastBucketCount_ = 0;
    bool gpuDrivenComputePending_ = false;
    bool gpuDrivenDrawPending_ = false;
    bool gpuDrivenVisPending_ = false;
    bool gpuDrivenResolvePending_ = false;
    wgpu::BindGroupLayout gpuDrivenVisSetLayout_;
    wgpu::BindGroupLayout gpuDrivenResolveSetLayout_;
    wgpu::PipelineLayout gpuDrivenVisPipelineLayout_;
    wgpu::PipelineLayout gpuDrivenResolvePipelineLayout_;
    wgpu::RenderPipeline gpuDrivenVisPipeline_;
    wgpu::RenderPipeline gpuDrivenResolvePipeline_;

    struct GpuDrivenVgAsset {
        wgpu::Buffer positions;
        wgpu::Buffer triangles;
        wgpu::Buffer clusters;
        wgpu::Buffer indirect;
        wgpu::Buffer params;
        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        uint32_t clusterCount = 0;
        Mesh *mesh = nullptr;
        glm::mat4 model{1.f};
        uint32_t materialId = kInvalidGpuDrivenSlot;
        bool active = false;
    };
    std::vector<GpuDrivenVgAsset> gpuDrivenVgAssets_;
    std::unordered_map<Mesh *, uint32_t> gpuDrivenVgMeshIds_;
    wgpu::BindGroupLayout gpuDrivenVgComputeSetLayout_;
    wgpu::BindGroupLayout gpuDrivenVgVisSetLayout_;
    wgpu::BindGroupLayout gpuDrivenVgResolveSetLayout_;
    wgpu::PipelineLayout gpuDrivenVgComputePipelineLayout_;
    wgpu::PipelineLayout gpuDrivenVgVisPipelineLayout_;
    wgpu::PipelineLayout gpuDrivenVgResolvePipelineLayout_;
    wgpu::ComputePipeline gpuDrivenVgCullPipeline_;
    wgpu::RenderPipeline gpuDrivenVgVisPipeline_;
    wgpu::RenderPipeline gpuDrivenVgResolvePipeline_;
    glm::mat4 gpuDrivenVgViewProj_{1.f};
    glm::vec3 gpuDrivenVgCameraPos_{0.f};
    float gpuDrivenVgFovYDeg_ = 60.f;
    float gpuDrivenVgNear_ = 0.1f;
    float gpuDrivenVgFar_ = 100.f;
    glm::vec4 gpuDrivenVgPlanes_[6]{};
    uint32_t gpuDrivenVgVisibleDiagnostic_ = 0;
    uint32_t gpuDrivenVgLastIndirectDrawCount_ = 0;
    bool gpuDrivenVgComputePending_ = false;
    bool gpuDrivenVgVisPending_ = false;
    bool gpuDrivenVgResolvePending_ = false;

    // Browser async frame readback (avoids ASYNCIFY sleep inside deep
    // JS->Squirrel->Graphics call chains).
    struct PendingReadback;
    std::unique_ptr<PendingReadback> pendingReadback_;

    // Cached mesh3d bind groups keyed by the texture views + shadow resources.
    // Dynamic UBO offsets are passed at SetBindGroup time, so one bind group
    // serves every draw that uses the same texture set (per-draw creation was
    // a hot path: makeMeshBindGroup ran once per mesh draw per frame).
    using MeshBindGroupKey = std::tuple<uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                        uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                        uintptr_t, uintptr_t, uintptr_t>;
    std::map<MeshBindGroupKey, wgpu::BindGroup> meshBindGroupCache_;
    static constexpr size_t kMaxMeshBindGroupCache = 128;
    void clearMeshBindGroupCache() { meshBindGroupCache_.clear(); }

    void markSwapchainDirty() override { swapchainConfigured = false; }
    void rebuildSwapchainIfNeeded();
    bool acquireSurfaceTexture(wgpu::TextureView &view, wgpu::Texture &texture);
};

}  // namespace eve::graphics::webgpu
