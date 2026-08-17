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

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <glm/glm.hpp>

namespace eve::graphics::webgpu {

class OffscreenCanvas;

/**
 * Frame UBO for the mesh3d pipeline. Mirrors the std140 layout of the WGSL
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
    glm::mat4 view{1.f};
    glm::vec4 clipInfo{0.1f, 100.f, 0.f, 0.f};   // x=near, y=far
    glm::vec4 cloud{0.f, 1.5f, 0.f, 0.f};        // x=strength(0=off), y=worldCell, z=time
    glm::vec4 cloudWind{4.f, 0.f, 0.55f, 0.5f};  // xy=wind vel, z=coverage, w=detail
};
static_assert(sizeof(Mesh3DUBO) == 608, "Mesh3DUBO layout must match the WGSL Frame block");

/**
 * Texture resources backed by a wgpu texture + view + sampler + bind groups.
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
 * Vertex/index buffers for one mesh.
 */
struct GpuMesh {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    uint32_t vertexStride = 0;
    wgpu::IndexFormat indexFormat = wgpu::IndexFormat::Uint32;
};

/**
 * A compiled shader: one WebGPU pipeline + layout. Also holds the WGSL
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
    Graphics();
    ~Graphics() override;

    std::string getBackendName() const override { return "webgpu"; }
    bool supportsGBufferPost() const override { return false; }

    void initWithWindow(void *nativeWindow) override;
    void present() override;
    void pushValidationScope() override;
    void popValidationScope() override;
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
    Shader *newShader(const std::string &vertGlsl, const std::string &fragGlsl) override;
    Shader *newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                 const std::vector<uint32_t> &fragSpv) override;
    Shader *newMeshShaderFromWgsl(const std::string &vertWgsl,
                                  const std::string &fragWgsl) override;
    Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) override;
    Shader *newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                 const std::vector<uint32_t> &fragSpv) override;

    Mesh *newMeshFromAssimp(const ::aiMesh &mesh) override;
    Mesh *newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) override;
    Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                            int vertexCount, const uint32_t *indices, int indexCount) override;
    bool bakeMeshMorph(Mesh *mesh) override;
    bool updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ, const float *uvST,
                            int vertexCount, const uint32_t *indices, int indexCount) override;
    Mesh *newMeshSphere(int slices = 32, int stacks = 16) override;
    Mesh *newMeshCylinder(int slices = 32, int stacks = 1, bool caps = true) override;

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
                                int tilesPerRow = 16) override;
    void setMesh3DNormalTexture(Texture *normal) override;
    void setMesh3DHeightTexture(Texture *height) override;
    void setMesh3DSceneDepth(Texture *depth) override;
    void setMesh3DMaterial(float metallic, float roughness) override;
    void setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) override;
    void setMesh3DParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f) override;
    void setMesh3DLighting(const Lighting3DPack &pack) override;
    void setCloudShadows(float strength, float worldCell, float time, float windSpeed,
                         float windAngle, float coverage, float detail) override;
    void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) override;
    void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) override;
    void setMesh3DCameraPos(const glm::vec3 &eye) override;
    void setMesh3DEnv(Texture *cube, float intensity) override;
    void setMesh3DShadows(const ShadowUpload &upload) override;
    void setMesh3DShadowReceive(bool receive) override;
    void beginShadowPass(int cascadeIndex) override;
    void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) override;
    void drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP,
                             Texture *albedo = nullptr) override;
    void endShadowPass() override;

    void beginGBufferPass(int width, int height) override;
    void drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                         float farZ, Texture *albedo = nullptr, float tintR = 1.f, float tintG = 1.f,
                         float tintB = 1.f) override;
    void drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                              float nearZ, float farZ, Texture *albedo = nullptr, float tintR = 1.f,
                              float tintG = 1.f, float tintB = 1.f) override;
    void endGBufferPass() override;

    Canvas *newCanvas(int width, int height) override;
    void setCanvas(Canvas *canvas) override;
    bool isCanvasActive() const override;
    Canvas *getCanvas() const override;

    Texture *getTexture() override;
    image::ImageData *newImageData() override;
    void draw(eve::graphics::Graphics *gfx, const glm::mat4 &matrix) const override;
    void draw(Canvas *C, const glm::mat4 &matrix) const override;
    void clear(std::optional<Color> color, std::optional<int> stencil,
               std::optional<double> depth) override;
    Color getPixel(int x, int y) override;

    /** Flush accumulated 2D batches into an offscreen canvas target. */
    void flush2DToCanvas(OffscreenCanvas *canvas);
    /** Blocking CPU readback of an offscreen canvas or scene color target. */
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
        uint32_t frameUboOffset = 0;
        uint32_t pushUboOffset = 0;
        uint32_t shadowUboOffset = 0;
    };
    struct ShadowDraw {
        Mesh *mesh = nullptr;
        glm::mat4 mvp{1.f};
    };
    struct GbufferDraw {
        Mesh *mesh = nullptr;
        Texture *albedo = nullptr;
        glm::mat4 mvp{1.f};
        glm::mat4 model{1.f};
        float nearZ = 0.1f;
        float farZ = 100.f;
        glm::vec4 tint{1.f};
        uint32_t pushUboOffset = 0;
    };
    struct VoxelDraw {
        uint32_t instanceBufferOffset = 0;
        uint32_t count = 0;
        GpuTexture *atlas = nullptr;
        glm::mat4 viewProj{1.f};
        glm::vec4 chunkOrigin{0.f};
        glm::vec4 atlasInfo{16.f, 0.f, 0.f, 0.f};
        glm::vec4 tint{1.f};
        uint32_t pushUboOffset = 0;
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
    void createShadowPipelines();
    void createGbufferPipelines();
    void createVoxelPipelines();
    void createSceneColorResources(int width, int height);
    void destroySceneColorResources();
    void createShadowResources();
    void destroyShadowResources();
    void createGbufferResources(int width, int height);
    void destroyGbufferResources();

    wgpu::RenderPipeline createPipelineForShader(GpuShader *gs, wgpu::TextureFormat format,
                                                 bool depth, bool mesh3d, bool hair,
                                                 bool shadow, bool gbuffer,
                                                 wgpu::PipelineLayout layout);
    wgpu::BindGroupLayout make2DBindGroupLayout();
    wgpu::BindGroupLayout makeMesh3DBindGroupLayout();
    wgpu::BindGroupLayout makeShadowBindGroupLayout();
    wgpu::BindGroupLayout makeGbufferBindGroupLayout();
    wgpu::BindGroupLayout makeVoxelBindGroupLayout();
    wgpu::PipelineLayout make2DPipelineLayout();
    wgpu::PipelineLayout makeMesh3DPipelineLayout();
    wgpu::PipelineLayout makeShadowPipelineLayout();
    wgpu::PipelineLayout makeGbufferPipelineLayout();
    wgpu::PipelineLayout makeVoxelPipelineLayout();

    GpuTexture *gpuForTexture(Texture *t) const;
    GpuTexture *gpuForTextureOrWhite(Texture *t) const;
    wgpu::BindGroup makeTex2DBindGroup(GpuTexture *color, GpuTexture *depth);
    wgpu::BindGroup makeMeshBindGroup(GpuTexture *albedo, GpuTexture *normal, GpuTexture *env,
                                      GpuTexture *height, GpuTexture *depth,
                                      uint32_t frameUboOffset, uint32_t shadowUboOffset,
                                      uint32_t pushUboOffset);
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
    void flushMesh3D(wgpu::RenderPassEncoder pass, WGPUTextureFormat format);
    void flushShadowPass(wgpu::RenderPassEncoder pass);
    void flushGbufferPass(wgpu::RenderPassEncoder pass);
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

    // ---- state ----
    bool initialized = false;
    bool deviceInitDone = false;
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

    // Pipelines / layouts.
    wgpu::PipelineLayout tex2DPipelineLayout;
    wgpu::PipelineLayout mesh3dPipelineLayout;
    wgpu::PipelineLayout shadowPipelineLayout;
    wgpu::PipelineLayout gbufferPipelineLayout;
    wgpu::PipelineLayout voxelPipelineLayout;
    wgpu::BindGroupLayout tex2DSetLayout;
    wgpu::BindGroupLayout mesh3dSetLayout;
    wgpu::BindGroupLayout shadowSetLayout;
    wgpu::BindGroupLayout gbufferSetLayout;
    wgpu::BindGroupLayout voxelSetLayout;
    // Shared filtering sampler for bindings declared as `sampler` in WGSL
    // (e.g. mesh3d's @binding(7) mainSamp).
    wgpu::Sampler mainSampler;
    wgpu::RenderPipeline colorPipeline;      // 2D solid
    wgpu::RenderPipeline texturedPipeline;   // 2D textured
    wgpu::RenderPipeline colorAdditivePipeline;
    wgpu::RenderPipeline texturedAdditivePipeline;
    wgpu::RenderPipeline colorOpaquePipeline;
    wgpu::RenderPipeline texturedOpaquePipeline;
    wgpu::RenderPipeline mesh3dPipeline;
    wgpu::RenderPipeline mesh3dShadowPipeline;
    wgpu::RenderPipeline mesh3dGbufferPipeline;
    wgpu::RenderPipeline voxelRectPipeline;
    wgpu::RenderPipeline lit2dPipeline;
    // RGBA8Unorm (offscreen canvas / scene) variants of the 2D pipelines.
    wgpu::RenderPipeline offscreenColorPipeline;
    wgpu::RenderPipeline offscreenTexturedPipeline;
    wgpu::RenderPipeline offscreenColorAdditivePipeline;
    wgpu::RenderPipeline offscreenTexturedAdditivePipeline;
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
    void noteSolidOverlay();
    void noteTexturedOverlay(Texture *tex);
    void clear2DBatches();

    // 3D frame state.
    bool frame3DStarted = false;
    bool sceneColorPassOpen = false;
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
    float mesh3dTexBombScale = 4.f, mesh3dTexBombStrength = 0.f, mesh3dTexBombRot = 1.f;
    float mesh3dParallaxScale = 0.f, mesh3dParallaxMin = 8.f, mesh3dParallaxMax = 32.f;
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
    VertexArena voxelInstanceArena;

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

    void markSwapchainDirty() override { swapchainConfigured = false; }
    void rebuildSwapchainIfNeeded();
    bool acquireSurfaceTexture(wgpu::TextureView &view, wgpu::Texture &texture);
};

}  // namespace eve::graphics::webgpu
