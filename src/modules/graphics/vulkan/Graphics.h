#pragma once
#include "graphics/Graphics.h"
#include "graphics/Batcher.h"
#include "graphics/Texture.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Light.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"
#include "vkbuilder.hpp"
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace eve::graphics::vulkan {

class OffscreenCanvas;

struct ColorVertex {
    glm::vec2 pos;
    glm::vec4 color;

    static vk::VertexInputBindingDescription getBindingDescription(uint32_t binding) {
        vk::VertexInputBindingDescription b{};
        b.binding = binding;
        b.stride = sizeof(ColorVertex);
        b.inputRate = vk::VertexInputRate::eVertex;
        return b;
    }
    static std::vector<vk::VertexInputAttributeDescription> getAttributeDescription(uint32_t binding) {
        return {
            {0, binding, vk::Format::eR32G32Sfloat, offsetof(ColorVertex, pos)},
            {1, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(ColorVertex, color)},
        };
    }
};

struct TexturedVertex {
    glm::vec2 pos;
    glm::vec4 color;
    glm::vec2 uv;

    static vk::VertexInputBindingDescription getBindingDescription(uint32_t binding) {
        vk::VertexInputBindingDescription b{};
        b.binding = binding;
        b.stride = sizeof(TexturedVertex);
        b.inputRate = vk::VertexInputRate::eVertex;
        return b;
    }
    static std::vector<vk::VertexInputAttributeDescription> getAttributeDescription(uint32_t binding) {
        return {
            {0, binding, vk::Format::eR32G32Sfloat, offsetof(TexturedVertex, pos)},
            {1, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(TexturedVertex, color)},
            {2, binding, vk::Format::eR32G32Sfloat, offsetof(TexturedVertex, uv)},
        };
    }
};

struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;

    static vk::VertexInputBindingDescription getBindingDescription(uint32_t binding) {
        vk::VertexInputBindingDescription b{};
        b.binding = binding;
        b.stride = sizeof(MeshVertex);
        b.inputRate = vk::VertexInputRate::eVertex;
        return b;
    }
    static std::vector<vk::VertexInputAttributeDescription> getAttributeDescription(uint32_t binding) {
        return {
            {0, binding, vk::Format::eR32G32B32Sfloat, offsetof(MeshVertex, pos)},
            {1, binding, vk::Format::eR32G32B32Sfloat, offsetof(MeshVertex, normal)},
            {2, binding, vk::Format::eR32G32Sfloat, offsetof(MeshVertex, uv)},
        };
    }
};

struct Mesh3DUBO {
    // Layout prefix matches legacy custom Mesh3D shaders (toon etc.).
    glm::mat4 mvp{1.f};
    glm::mat4 model{1.f};
    glm::vec4 lightDir{0.4f, 1.f, 0.3f, 0.f};   // xyz = primary dir; w = lightCount
    glm::vec4 lightColor{1.f, 1.f, 1.f, 0.f};    // rgb = primary color; w = envIntensity (IBL)
    glm::vec4 tint{1.f, 1.f, 1.f, 1.f};
    glm::vec4 cameraPos{0.f, 0.f, 3.f, 0.45f};    // xyz = eye; w = roughness
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f}; // rgb = ambient; w = metallic
    Light3DGpu lights[Lighting3DPack::kMaxLights]{};
    // Appended: custom shaders that only read the prefix stay valid (buffer may be larger).
    glm::vec4 texBomb{4.f, 0.f, 1.f, 0.f}; // x=cellScale, y=strength (0=off), z=rotAmount
    glm::vec4 parallax{0.f, 8.f, 32.f, 0.f}; // x=scale (0=off), y=minLayers, z=maxLayers
    glm::mat4 view{1.f};                     // camera view (CSM / view-space depth)
    glm::vec4 clipInfo{0.1f, 100.f, 0.f, 0.f}; // x=near, y=far (linear depth in scene color A)
};

struct Mesh3DClusteredUBO {
    glm::mat4 mvp{1.f};
    glm::mat4 model{1.f};
    glm::mat4 view{1.f};
    glm::vec4 lightDir{0.4f, 1.f, 0.3f, 0.f};
    glm::vec4 lightColor{1.f, 1.f, 1.f, 0.f};  // rgb = primary; w = envIntensity
    glm::vec4 tint{1.f, 1.f, 1.f, 1.f};
    glm::vec4 cameraPos{0.f, 0.f, 3.f, 0.45f};
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f};
    glm::vec4 gridInfo{16.f, 9.f, 24.f, 0.f};
    glm::vec4 clipInfo{0.1f, 100.f, 1.f, 1.f};
    glm::vec4 texBomb{4.f, 0.f, 1.f, 0.f}; // x=cellScale, y=strength (0=off), z=rotAmount
    glm::vec4 parallax{0.f, 8.f, 32.f, 0.f}; // x=scale (0=off), y=minLayers, z=maxLayers
};

struct GpuTexture {
    vkb::TextureImage2D image;
    vkb::TextureImageCube cubeImage;
    bool isCube = false;
    vk::Sampler sampler;
    vkb::BoundSet descriptorSet;
    vk::ImageView viewOverride{};
    int width = 0;
    int height = 0;
    uint32_t mipLevels = 1;
    TextureSampler samplerState{};

    vk::ImageView imageView() const {
        if (viewOverride) return viewOverride;
        return isCube ? cubeImage.imageView() : image.imageView();
    }
};

struct GpuMesh {
    vkb::HostVertexBuffer vertices;
    vkb::GenericBuffer indices;
    uint32_t indexCount = 0;
};

struct GpuShader {
    vk::Pipeline swapchainPipeline;
    vk::Pipeline offscreenPipeline;
    vk::Pipeline mesh3dPipeline;
    vk::PipelineLayout pipelineLayout;
    bool isMesh3D = false;
    bool isHair3D = false;
    Shader *owner = nullptr;
};

class Graphics final : public eve::graphics::Graphics {
public:
    ~Graphics() override;

    std::string getBackendName() const override;

    void initWithWindow(void *nativeWindow) override;
    void present() override;
    void requestSurfaceRecreate() override { surfaceNeedsRecreate = true; }
    void onNativeWindowDestroyed() override;
    void setScreenReadbackEnabled(bool enabled) override {
        eve::graphics::Graphics::setScreenReadbackEnabled(enabled);
        ensurePresentCaptureHook();
    }
    void setVSync(bool enabled) override {
        if (vsyncEnabled == enabled) return;
        eve::graphics::Graphics::setVSync(enabled);
        markSwapchainDirty();
    }
    void setMsaaSamples(int samples) override {
        const int raw = samples > 1 ? samples : 0;
        if (raw == msaaSamples) return;
        eve::graphics::Graphics::setMsaaSamples(raw);
    }
    int getMsaaSamples() const override { return msaaSamples; }
    void setViewportSize(int width, int height, int pixelwidth, int pixelheight) override;
    void drawSolidRect(float x, float y, float w, float h, const Color &color) override;
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
    bool replaceTexturePixels(Texture *tex, image::ImageData *data);
    void drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) override;
    void drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w, float h,
                                const Color &color) override;
    void drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0, float v0,
                            float u1, float v1, const Color &color) override;
    void drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y, float w,
                                  float h, float u0, float v0, float u1, float v1,
                                  const Color &color, bool rotatedUV = false) override;
    void drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx, float cy,
                                         float w, float h, float degrees, float u0, float v0,
                                         float u1, float v1, const Color &color,
                                         bool rotatedUV = false) override;
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
    Shader *newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) override;
    Shader *newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                 const std::vector<uint32_t> &fragSpv) override;
    Mesh *newMeshFromAssimp(const ::aiMesh &mesh) override;
    Mesh *newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) override;
    Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                            int vertexCount, const uint32_t *indices, int indexCount) override;
    bool bakeMeshMorph(Mesh *mesh) override;
    Mesh *newMeshSphere(int slices = 32, int stacks = 16) override;
    Mesh *newMeshCylinder(int slices = 32, int stacks = 1, bool caps = true) override;
    void begin3DFrame() override;
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
    void setMesh3DMaterial(float metallic, float roughness) override;
    void setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) override;
    void setMesh3DParallax(float scale, float minLayers = 8.f, float maxLayers = 32.f) override;
    void setMesh3DLighting(const Lighting3DPack &pack) override;
    void setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) override;
    void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) override;
    void setMesh3DCameraPos(const glm::vec3 &eye) override;
    void setMesh3DEnv(Texture *cube, float intensity) override;
    void setMesh3DShadows(const ShadowUpload &upload) override;
    void setMesh3DShadowReceive(bool receive) override;
    void beginShadowPass(int cascadeIndex) override;
    void drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) override;
    void endShadowPass() override;

    void beginGBufferPass(int width, int height) override;
    void drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                         float farZ, Texture *albedo = nullptr, float tintR = 1.f, float tintG = 1.f,
                         float tintB = 1.f) override;
    void endGBufferPass() override;

    Canvas *newCanvas(int width, int height) override;
    void setCanvas(Canvas *canvas) override;
    bool isCanvasActive() const override;
    Canvas *getCanvas() const override;

    Texture *getTexture() override;
    image::ImageData *newImageData() override;

    void draw(eve::graphics::Graphics *gfx, const glm::mat4 &matrix) const override;
    void draw(Canvas *C, const glm::mat4 &matrix) const override;
    void clear(std::optional<Color> color, std::optional<int> stencil, std::optional<double> depth) override;
    Color getPixel(int x, int y) override;

    vkb::Device &getDevice() { return device; }
    vk::CommandPool getUploadPool() const { return uploadPool; }
    vk::DescriptorSetLayout getTexSetLayout() const { return texSetLayout; }
    vk::DescriptorPool getDescriptorPool() const { return descriptorPool; }
    const vkb::BuiltRenderPass &getOffscreenRenderPass() const { return offscreenRenderPass; }
    vk::RenderPass getSwapchainRenderPass() const { return renderpass; }
    vk::RenderPass getUiMsaaRenderPass() const { return uiRenderPass; }
    /** Sample count the UI MSAA pass runs with (matches getUiMsaaRenderPass). */
    vk::SampleCountFlagBits getUiMsaaSamples() const { return uiColorSamples; }
    /** Create the UI MSAA render pass (once) and its color targets for ImGui init. */
    void ensureUiColorResources() {
        createUiColorResources(int(swapchain.extent.width), int(swapchain.extent.height));
    }
    vkb::Instance &getInstance() { return inst; }
    vkb::Swapchain &getSwapchain() { return swapchain; }
    void *getSdlWindow() const { return sdlWindow; }
    uint32_t getSwapchainImageCount() const { return swapchain.image_count; }

    friend class OffscreenCanvas;

    struct LitBatch {
        Texture *albedo = nullptr;
        Texture *normal = nullptr;
        Batcher batch;
    };

private:
    struct Mesh3dFrameSlots;
    struct Mesh3dClusteredFrameSlots;
    void createSwapchainAndPipeline();
    void createTexturedPipeline();
    void createLit2DPipeline();
    void createMesh3DPipeline();
    void createMesh3DClusteredPipeline();
    void createVoxelRectPipeline();
    vk::Pipeline buildVoxelRectPipeline(const vkb::BuiltRenderPass &rp,
                                        vk::SampleCountFlagBits samples);
    void destroyVoxelRectResources();
    void ensureVoxelUnitQuad();
    vkb::BoundSet voxelRectSetFor(GpuTexture *gpuTex);
    void createShadowResources();
    void destroyShadowResources();
    void createGBufferResources(int width, int height);
    void destroyGBufferResources();
    void createSceneColorResources(int width, int height);
    void destroySceneColorResources();
    void createUiColorResources(int width, int height);
    void destroyUiColorTargets();
    void destroyUiColorResources();
    void queueUiResolve();
    /** Render the present overlay (ImGui) into the UI MSAA pass and queue resolve. */
    bool renderUiOverlayPass();
    void recordPendingShadowPasses();
    void recordPendingGBufferPass();
    void dropPendingOffscreenPasses();
    /** acquire + record deferred shadow/gbuffer + begin swapchain RP. */
    bool beginSwapchainRenderPass();
    /** Begin the swapchain color+depth RP on an already-acquired present CB. */
    void beginSwapchainColorPass();
    bool beginSceneColorRenderPass();
    void endSceneColorRenderPass();
    void queueSceneColorResolve();
    void ensureClusteredBuffers(size_t lightsBytes, size_t tableBytes, size_t indicesBytes);
    void uploadClusteredLighting(const ClusteredLightingUpload &upload);
    vkb::BoundSet mesh3dClusteredSetFor(GpuTexture *gpuTex, GpuTexture *normalTex,
                                       GpuTexture *envTex, GpuTexture *heightTex,
                                       Mesh3dClusteredFrameSlots &fslots, size_t uboSlot);
    void ensureOffscreenPipelines();
    void ensureShaderOffscreenPipeline(Shader *shader);
    vk::Pipeline createTexturedStylePipeline(const std::vector<uint32_t> &vert,
                                             const std::vector<uint32_t> &frag,
                                             const vkb::BuiltRenderPass &rp, vk::PipelineLayout layout);
    vk::Pipeline createMesh3DStylePipeline(const std::vector<uint32_t> &vert,
                                           const std::vector<uint32_t> &frag,
                                           vk::PipelineLayout layout,
                                           const vkb::BuiltRenderPass &rp,
                                           vk::SampleCountFlagBits samples);
    vk::Pipeline createMesh3DHairPipeline(const std::vector<uint32_t> &vert,
                                          const std::vector<uint32_t> &frag,
                                          vk::PipelineLayout layout,
                                          const vkb::BuiltRenderPass &rp,
                                          vk::SampleCountFlagBits samples);
    /** Rebuild scene-pass pipelines against the given render pass / sample count. */
    void ensureScenePassPipelines(const vkb::BuiltRenderPass &target,
                                  vk::SampleCountFlagBits samples);
    /** Clamp a requested sample count to the device-supported set (0/1/2/4/8). */
    int clampMsaaSamples(int requested) const;
    /** Render pass a scene-pass pipeline should be built against right now. */
    const vkb::BuiltRenderPass &activeScenePass() const {
        return sceneColorRenderPass ? sceneColorRenderPass : renderpass;
    }
    /** Sample count the scene pass is currently running with (e1 when no MSAA). */
    vk::SampleCountFlagBits activeSceneSamples() const {
        return sceneColorRenderPass ? sceneColorSamples : vk::SampleCountFlagBits::e1;
    }
    void destroySwapchainResources();
    void flushBatch();
    void flushToSwapchain();
    void flushToOffscreen(OffscreenCanvas *canvas);
    void drawLitBatches(vk::CommandBuffer cb, int viewW, int viewH, vk::Pipeline pipeline,
                        std::vector<LitBatch> &batches, std::vector<vkb::HostVertexBuffer> &texBufs,
                        size_t &texBufIndex, bool offscreen);
    vkb::BoundSet lit2dSetFor(GpuTexture *albedo, GpuTexture *normal, bool offscreen);
    vkb::BoundSet post2SetFor(GpuTexture *color, GpuTexture *depth);
    void ensureFlatNormalTexture();
    void captureSwapchainImage(uint32_t imageIndex);
    void ensurePresentCaptureHook();
    vkb::BoundSet mesh3dSetFor(GpuTexture *gpuTex, GpuTexture *normalTex, GpuTexture *envTex,
                              GpuTexture *heightTex, Mesh3dFrameSlots &fslots, size_t uboSlot);
    void ensureDefaultEnvCubemap();
    void ensureFlatNormalTexture3D();
    void ensureFlatHeightTexture3D();
    vk::Sampler createVkSampler(const TextureSampler &sampler, uint32_t mipLevels) const;
    void writeCombinedImageDescriptor(GpuTexture *gpu);
    /** Rebuild surface/swapchain when dirty. Returns false if surface not ready. */
    bool rebuildSwapchainIfNeeded();
    /** acquire + begin command buffer; recreates swapchain and retries on failure. */
    bool beginPresentCommandBuffer();

    bool initialized = false;
    bool hasPresentedFrame = false;
    float maxSamplerAnisotropy = 1.f;
    std::vector<uint8_t> lastFrameRgba;
    Canvas *activeCanvas = nullptr;
    bool swapchainDirty = false;
    void markSwapchainDirty() override { swapchainDirty = true; }
    // Set from the SDL event-watch thread on Android foreground; consumed on
    // the render thread inside flushToSwapchain().
    std::atomic<bool> surfaceNeedsRecreate{false};
    void recreateSurfaceForResume();
    // False while the native window is mid-(re)creation / rotation (Android),
    // when touching the swapchain or presenting would crash the GPU driver.
    bool isRenderSurfaceReady() const;
    // Returns true only once the drawable size has been non-zero and identical
    // across consecutive frames, i.e. the surface has settled after a
    // rotation/resume and is safe to (re)build a swapchain against.
    bool isRenderSurfaceStable();
    int pendingSurfaceW = 0;
    int pendingSurfaceH = 0;
    int surfaceStableFrames = 0;
    void *sdlWindow = nullptr;
    vk::SurfaceKHR surface;

    vkb::Instance inst;
    vkb::Device device;
    vkb::Swapchain swapchain;
    vkb::BuiltRenderPass renderpass;

    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;

    vk::DescriptorSetLayout texSetLayout;
    vk::UniqueDescriptorSetLayout texSetLayoutUnique;
    vk::DescriptorPool descriptorPool;
    vk::Pipeline texPipeline;
    vk::PipelineLayout texPipelineLayout;
    vk::PipelineLayout shaderPipelineLayout;  // tex set + push constants
    vk::CommandPool uploadPool;

    vkb::BuiltRenderPass offscreenRenderPass;
    vk::Pipeline offscreenSolidPipeline;
    vk::Pipeline offscreenTexPipeline;

    vk::DescriptorSetLayout mesh3dSetLayout;
    vk::UniqueDescriptorSetLayout mesh3dSetLayoutUnique;
    vk::PipelineLayout mesh3dPipelineLayout;
    vk::PipelineLayout mesh3dShaderPipelineLayout;  // + push constants for custom mesh shaders
    vk::Pipeline mesh3dPipeline;
    // One UBO (+ per-texture descriptor sets) per draw in the current 3D frame.
    // Avoids vkUpdateDescriptorSets on a set already bound in a recording /
    // executable command buffer (which invalidates the CB).
    struct Mesh3dSetKey {
        GpuTexture *albedo = nullptr;
        GpuTexture *normal = nullptr;
        GpuTexture *env = nullptr;
        GpuTexture *height = nullptr;
        bool operator==(const Mesh3dSetKey &o) const {
            return albedo == o.albedo && normal == o.normal && env == o.env && height == o.height;
        }
    };
    struct Mesh3dSetKeyHash {
        size_t operator()(const Mesh3dSetKey &k) const {
            return std::hash<GpuTexture *>()(k.albedo) ^
                   (std::hash<GpuTexture *>()(k.normal) << 1) ^
                   (std::hash<GpuTexture *>()(k.env) << 2) ^
                   (std::hash<GpuTexture *>()(k.height) << 3);
        }
    };
    struct Mesh3dUboSlot {
        vkb::GenericBuffer ubo;
        vkb::GenericBuffer shadowUbo;
        std::unordered_map<Mesh3dSetKey, vkb::BoundSet, Mesh3dSetKeyHash> sets;
    };
    // Per-frame-slot UBO arenas, keyed by Present::frames_in_flight so a frame
    // never overwrites a UBO that an in-flight frame is still reading.
    struct Mesh3dFrameSlots {
        std::vector<Mesh3dUboSlot> slots;
        size_t drawIndex = 0;
    };
    std::vector<Mesh3dFrameSlots> mesh3dFrameSlots;
    Texture *whiteTexture = nullptr;
    Texture *flatNormalTexture3D = nullptr;
    Texture *flatHeightTexture3D = nullptr;
    Texture *defaultEnvCubemap = nullptr;
    Texture *mesh3dNormalTexture = nullptr;
    Texture *mesh3dHeightTexture = nullptr;
    Texture *mesh3dEnvTexture = nullptr;
    float mesh3dEnvIntensity = 0.f;
    float mesh3dMetallic = 0.f;
    float mesh3dRoughness = 0.45f;
    float mesh3dTexBombScale = 4.f;
    float mesh3dTexBombStrength = 0.f;
    float mesh3dTexBombRot = 1.f;
    float mesh3dParallaxScale = 0.f;
    float mesh3dParallaxMinLayers = 8.f;
    float mesh3dParallaxMaxLayers = 32.f;
    Lighting3DPack mesh3dLighting{};
    ShadowUpload mesh3dShadows{};
    bool mesh3dShadowReceive = true;

    // Clustered forward (separate set layout / pipeline; default PBR only).
    bool mesh3dClusteredActive = false;
    ClusteredLightingUpload mesh3dClustered{};
    vk::DescriptorSetLayout mesh3dClusteredSetLayout;
    vk::UniqueDescriptorSetLayout mesh3dClusteredSetLayoutUnique;
    vk::PipelineLayout mesh3dClusteredPipelineLayout;
    vk::Pipeline mesh3dClusteredPipeline;
    // Per-frame clustered storage (lights / cluster table / light indices),
    // multi-buffered so an in-flight frame's forward pass never reads storage
    // that the next frame is overwriting.
    struct ClusteredStorage {
        vkb::GenericBuffer lightsBuf;
        vkb::GenericBuffer tableBuf;
        vkb::GenericBuffer indicesBuf;
        size_t lightsCap = 0;
        size_t tableCap = 0;
        size_t indicesCap = 0;
    };
    std::vector<ClusteredStorage> clusteredStorages;  // per swapchain frame slot
    struct Mesh3dClusteredUboSlot {
        vkb::GenericBuffer ubo;
        vkb::GenericBuffer shadowUbo;
        std::unordered_map<Mesh3dSetKey, vkb::BoundSet, Mesh3dSetKeyHash> sets;
    };
    struct Mesh3dClusteredFrameSlots {
        std::vector<Mesh3dClusteredUboSlot> slots;
        size_t drawIndex = 0;
    };
    std::vector<Mesh3dClusteredFrameSlots> mesh3dClusteredFrameSlots;

    // Ping-pong copies so frame N+1 can write while frame N still samples.
    static constexpr uint32_t kAsyncResourceCopies = 2;

    // CSM shadow map (3 cascade layers), one array per in-flight slot.
    struct ShadowMapSlot {
        vkb::DepthArrayImage image;
        vk::Framebuffer framebuffers[ShadowConfig::kCascades]{};
    };
    std::vector<ShadowMapSlot> shadowMaps;
    vkb::DepthSampler shadowSampler{};
    vkb::BuiltRenderPass shadowRenderPass{};
    vk::PipelineLayout shadowPipelineLayout{};
    vk::Pipeline shadowPipeline{};
    int shadowPassCascade = -1;
    struct ShadowDraw {
        Mesh *mesh = nullptr;
        glm::mat4 mvp{1.f};
    };
    std::vector<ShadowDraw> shadowPassDraws;
    std::vector<ShadowDraw> shadowCascadeDraws[ShadowConfig::kCascades];
    uint32_t shadowPendingMask = 0;
    ShadowMapSlot &currentShadowMap();
    vk::ImageView currentShadowArrayView();

    // Screen-space G-buffer (normal / linear-depth / albedo + HW depth).
    struct GBufferPush {
        glm::mat4 mvp{1.f};
        glm::vec4 modelR0{1.f, 0.f, 0.f, 0.f};
        glm::vec4 modelR1{0.f, 1.f, 0.f, 0.f};
        glm::vec4 modelR2{0.f, 0.f, 1.f, 0.f};
        glm::vec4 clip{0.1f, 100.f, 0.f, 0.f};
    };
    static_assert(sizeof(GBufferPush) == 128, "GBuffer push constants must be 128 bytes");
    struct GBufferDraw {
        Mesh *mesh = nullptr;
        Texture *albedo = nullptr;
        GBufferPush push{};
    };
    struct GBufferSlot {
        vkb::ColorTarget normal;
        vkb::ColorTarget depthColor;
        vkb::ColorTarget albedo;
        vkb::DepthTarget depth;
        vk::Framebuffer framebuffer{};
        GpuTexture normalGpu{};
        GpuTexture depthColorGpu{};
        GpuTexture albedoGpu{};
        GpuTexture depthGpu{};
        Texture normalTex{};
        Texture depthColorTex{};
        Texture albedoTex{};
        Texture depthTex{};
    };
    int gbufferWidth = 0;
    int gbufferHeight = 0;
    std::vector<GBufferSlot> gbufferSlots;
    vkb::BuiltRenderPass gbufferRenderPass{};
    vk::PipelineLayout gbufferPipelineLayout{};
    vk::Pipeline gbufferPipeline{};
    bool gbufferPassActive = false;
    bool gbufferPending = false;
    std::vector<GBufferDraw> gbufferPassDraws;
    GBufferSlot *currentGBufferSlot();

    struct SceneColorSlot {
        vkb::ColorTarget msaaColor;
        vkb::ColorTarget color;
        vkb::DepthTarget depth;
        vk::Framebuffer framebuffer{};
        GpuTexture colorGpu{};
        Texture colorTex{};
    };
    int sceneColorWidth = 0;
    int sceneColorHeight = 0;
    vk::Format sceneColorFormat = vk::Format::eUndefined;
    vk::SampleCountFlagBits sceneColorSamples = vk::SampleCountFlagBits::e1;
    std::vector<SceneColorSlot> sceneColorSlots;
    vkb::BuiltRenderPass sceneColorRenderPass{};
    bool sceneColorPassOpen = false;
    vk::RenderPass scenePassPipelineTarget = vk::RenderPass{};
    vk::SampleCountFlagBits scenePassPipelineSamples = vk::SampleCountFlagBits::e1;
    int appliedMsaa = -1;
    SceneColorSlot *currentSceneColorSlot();

    // Dedicated 4x-MSAA color target for the UI overlay (ImGui), resolved to a
    // single-sample texture that is composited as the top-most fullscreen quad.
    struct UiColorSlot {
        vkb::ColorTarget msaaColor;
        vkb::ColorTarget color;
        vk::Framebuffer framebuffer{};
        GpuTexture colorGpu{};
        Texture colorTex{};
    };
    int uiColorWidth = 0;
    int uiColorHeight = 0;
    vk::Format uiColorFormat = vk::Format::eUndefined;
    vk::SampleCountFlagBits uiColorSamples = vk::SampleCountFlagBits::e1;
    std::vector<UiColorSlot> uiColorSlots;
    vkb::BuiltRenderPass uiRenderPass{};
    UiColorSlot *currentUiColorSlot();

    vkb::Present presentModel;
    vkb::RecordingCmd presentRecording;
    vkb::InRenderPass swapchainPass;
    vkb::DepthStencilImage depthImage;
    vk::Format depthFormat = vk::Format::eD32Sfloat;

    Batcher solidBatch;
    struct TexturedBatch {
        Texture *texture = nullptr;
        Texture *depth = nullptr;
        Shader *shader = nullptr;
        Batcher batch;
    };
    std::vector<TexturedBatch> texturedBatches;

    std::vector<LitBatch> litBatches;

    // Persistent host-visible vertex buffers for 2D batching, reused across
    // frames. GenericBuffer now owns the Vulkan handles.
    struct Frame2DBuffers {
        vkb::HostVertexBuffer solidBuf;
        std::vector<vkb::HostVertexBuffer> texBufs;
    };
    std::vector<Frame2DBuffers> frame2dBuffers;  // per swapchain frame slot
    Frame2DBuffers offscreenBuffers;             // synchronous offscreen path

    vk::DescriptorSetLayout lit2dSetLayout;
    vk::UniqueDescriptorSetLayout lit2dSetLayoutUnique;
    vk::PipelineLayout lit2dPipelineLayout;
    vk::Pipeline lit2dPipeline;
    vk::Pipeline offscreenLitPipeline;
    std::vector<vkb::GenericBuffer> lighting2dUboSlots;  // per swapchain frame slot
    vkb::GenericBuffer offscreenLighting2dUbo;           // synchronous offscreen path
    Lighting2DUBO lighting2dFrame{};
    struct LitSetKey {
        GpuTexture *albedo = nullptr;
        GpuTexture *normal = nullptr;
        bool operator==(const LitSetKey &o) const {
            return albedo == o.albedo && normal == o.normal;
        }
    };
    struct LitSetKeyHash {
        size_t operator()(const LitSetKey &k) const {
            return std::hash<void *>()(k.albedo) ^ (std::hash<void *>()(k.normal) << 1);
        }
    };
    std::vector<std::unordered_map<LitSetKey, vkb::BoundSet, LitSetKeyHash>> lit2dSets;
    std::unordered_map<LitSetKey, vkb::BoundSet, LitSetKeyHash> offscreenLit2dSets;
    std::unordered_map<LitSetKey, vkb::BoundSet, LitSetKeyHash> post2Sets;
    Texture *flatNormalTexture = nullptr;

    Color clearColor{0.1f, 0.1f, 0.12f, 1.0f};
    bool hasPendingClear = true;

    std::vector<std::unique_ptr<Texture>> ownedTextures;
    std::vector<std::unique_ptr<GpuTexture>> ownedGpuTextures;
    /** Path-normalized → Texture* for hot reload (stable pointers). */
    std::unordered_map<std::string, Texture *> texturesByPath;
    std::vector<std::unique_ptr<Mesh>> ownedMeshes;
    std::vector<std::unique_ptr<GpuMesh>> ownedGpuMeshes;
    std::vector<std::unique_ptr<Shader>> ownedShaders;
    std::vector<std::unique_ptr<GpuShader>> ownedGpuShaders;
    std::vector<std::unique_ptr<eve::graphics::Canvas>> ownedCanvases;

    bool swapchainPassOpen = false;
    Mesh3DUBO mesh3dFrameUbo{};

    // Instanced voxel face rectangles (packed uint32 instances).
    struct VoxelRectPC {
        glm::mat4 viewProj{1.f};
        glm::vec4 chunkOrigin{0.f};  // xyz = origin, w = faceDir
        glm::vec4 atlasInfo{16.f, 0.f, 0.f, 0.f};
        glm::vec4 tint{1.f};
    };
    vk::DescriptorSetLayout voxelRectSetLayout{};
    vk::UniqueDescriptorSetLayout voxelRectSetLayoutUnique;
    vk::PipelineLayout voxelRectPipelineLayout{};
    vk::Pipeline voxelRectPipeline{};
    vkb::GenericBuffer voxelUnitQuadVerts;
    vkb::GenericBuffer voxelUnitQuadIndices;
    bool voxelUnitQuadReady = false;
    std::unordered_map<GpuTexture *, vkb::BoundSet> voxelRectSets;
    // Grow-only instance buffer pool (reset index each begin3DFrame), per
    // swapchain frame slot for async safety.
    struct VoxelInstanceSlot {
        vkb::GenericBuffer buffer;
        size_t capacityBytes = 0;
    };
    struct VoxelInstanceFrame {
        std::vector<VoxelInstanceSlot> slots;
        size_t drawIndex = 0;
    };
    std::vector<VoxelInstanceFrame> voxelInstanceFrames;  // per swapchain frame slot

    Frame2DBuffers &currentFrame2DBuffers();
    Mesh3dFrameSlots &currentMesh3dFrameSlots();
    Mesh3dClusteredFrameSlots &currentMesh3dClusteredFrameSlots();
    vkb::GenericBuffer &currentLighting2dUbo();
    std::unordered_map<LitSetKey, vkb::BoundSet, LitSetKeyHash> &currentLit2dSets();
    ClusteredStorage &currentClusteredStorage();
    VoxelInstanceFrame &currentVoxelInstanceFrame();
    uint32_t frameSlotCount() const;
    size_t currentFrameSlot() const;
    vk::CommandBuffer &currentPresentCb();
    vkb::FrameSlot frameToken() const;
    /** Drain in-flight frames before mutating a GPU object sampled/read by them. */
    void waitForSharedGpuResources();
    void invalidateTextureBindings();
};

}  // namespace eve::graphics::vulkan
