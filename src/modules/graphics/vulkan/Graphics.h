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
#include "graphics/vulkan/GpuDriven.h"
#include "graphics/vulkan/FrameArena.h"
#include "graphics/vulkan/ComputePass.h"
#include <atomic>
#include <array>
#include <memory>
#include <optional>
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
    // Dynamic cloud shadows on ground. cloud.x=strength (0=off), y=world cell size,
    // z=time, w=unused; cloudWind.xy=wind velocity (world/s), z=coverage, w=detail.
    glm::vec4 cloud{0.f, 1.5f, 0.f, 0.f};
    glm::vec4 cloudWind{4.f, 0.f, 0.55f, 0.5f};
    // GPU-driven only: x = bindless env cubemap slot, y = envIntensity.
    // Appended after the legacy prefix so legacy shaders are unaffected.
    glm::vec4 bindlessEnv{0.f, 0.f, 0.f, 0.f};
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
    /** @brief Bindless texture-array slot (stage 0 GPU-driven path). */
    uint32_t bindlessIndex2D = kInvalidBindlessSlot;
    uint32_t bindlessIndexCube = kInvalidBindlessSlot;

    vk::ImageView imageView() const {
        if (viewOverride) return viewOverride;
        return isCube ? cubeImage.imageView() : image.imageView();
    }
};

struct GpuMesh {
    vkb::HostVertexBuffer vertices;
    vkb::GenericBuffer indices;
    uint32_t indexCount = 0;
    vk::IndexType indexType = vk::IndexType::eUint32;
    /** @brief Index into the GPU mesh table (GpuMeshRecord). */
    uint32_t gpuRecordIndex = kInvalidBindlessSlot;
    /** @brief CPU-side record for this mesh (bounds/ranges); uploaded by registerMeshRecord. */
    GpuMeshRecord record;
};

struct GpuShader {
    vk::Pipeline swapchainPipeline;
    vk::Pipeline offscreenPipeline;
    vk::Pipeline mesh3dPipeline;
    vk::Pipeline mesh3dXrayPipeline;
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

    // ---- GPU-driven rendering (stage 0: bindless + resource tables) ----

    /** @brief Capabilities probed at device creation; empty when unavailable. */
    const GpuDrivenCaps &gpuDrivenCaps() const { return gpuDrivenCaps_; }

    /** @brief Per-frame arena for the current swapchain frame slot. */
    FrameArena &currentFrameArena();

    /** @brief Get (or lazily create) the GPU material-table slot for a material. */
    uint32_t materialTableGetOrCreate(eve::graphics::Material *material);

    /** @brief Upload all registered material records to the GPU table. */
    void syncMaterialTable();

    uint32_t bindlessSlot2D(const GpuTexture *tex) const {
        return tex ? tex->bindlessIndex2D : kInvalidBindlessSlot;
    }
    uint32_t bindlessSlotCube(const GpuTexture *tex) const {
        return tex ? tex->bindlessIndexCube : kInvalidBindlessSlot;
    }
    bool supportsGpuDriven3D() const override {
        return gpuDrivenCaps_.gpuDrivenAvailable();
    }
    bool gpuDrivenEnabled() const override {
        return gpuDrivenEnabled_ && gpuDrivenCaps_.gpuDrivenAvailable();
    }
    void gpuDrivenSetEnabled(bool enabled) override { gpuDrivenEnabled_ = enabled; }
    uint32_t gpuDrivenMeshRecord(Mesh *mesh) override;
    uint32_t gpuDrivenMaterialRecord(Material *material) override {
        return materialTableGetOrCreate(material);
    }
    bool gpuDrivenMaterialUsable(Material *material) override;
    bool gpuDrivenSubmitOpaque(const GpuInstance *instances, uint32_t instanceCount) override;
    /** @brief Test/debug helpers (valid when the GPU-driven path is live). */
    uint32_t debugBindlessIndex(Texture *tex) const;
    uint32_t debugMeshRecordIndex(Mesh *mesh) const;
    /** @brief Indirect draws emitted by the last successful GPU-driven submit. */
    uint32_t debugLastGpuDrivenDrawCount() const { return lastGpuDrivenDrawCount_; }
    /** @brief Block until all in-flight GPU work (all frames) has completed. */
    void waitForSharedGpuResources();
    /** @brief Stage 2 cull is live for this frame (GPU-written commands). */
    bool gpuDrivenCullEnabled() const {
        return gpuDrivenEnabled_ && gpuDrivenCaps_.gpuDrivenCullAvailable();
    }
    /** @brief Scene color pass is deferred until after the compute cull section. */
    bool gpuDrivenScenePassPending() const { return gpuDrivenScenePassPending_; }
    bool gpuDrivenCullBegin(const GpuInstance *instances, uint32_t instanceCount);
    void gpuDrivenCullEmit(const glm::mat4 &viewProj, const glm::vec3 &eye, float fovYDeg,
                           float nearZ, float farZ);
    void gpuDrivenOpenScenePass();
    void gpuDrivenDrawOpaque();
    /** @brief Debug readback: visible instances / non-empty buckets from the last cull. */
    uint32_t debugGpuDrivenVisibleCount() const;
    uint32_t debugGpuDrivenCulledDrawCount() const;

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
    bool replaceTexturePixels(Texture *tex, image::ImageData *data);
    void drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) override;
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
    image::ImageData *renderEntityIdMask(
        const std::vector<eve::graphics::Graphics::EntityIdDraw> &draws, const glm::mat4 &viewProj,
        int width, int height) override;
    image::ImageData *readGBufferToImageData(const std::string &name) override;
    void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) override;
    void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                        Shader *shader) override;
      void drawVoxelFaceInstances(const uint32_t *packed, int count, float originX, float originY,
                                  float originZ, const std::string &faceDir, Texture *atlas,
                                  int tilesPerRow = 16, const uint32_t *ao = nullptr) override;
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
    void drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                              float farZ, Texture *albedo = nullptr, float tintR = 1.f,
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
    void clear(std::optional<Color> color, std::optional<int> stencil, std::optional<double> depth) override;
    Color getPixel(int x, int y) override;

    vkb::Device &getDevice() { return device; }
    vk::CommandPool getUploadPool() const { return uploadPool; }
    vk::DescriptorSetLayout getTexSetLayout() const { return texSetLayout; }
    vk::DescriptorPool getDescriptorPool() const { return descriptorPool; }
    const vkb::BuiltRenderPass &getOffscreenRenderPass() const { return offscreenRenderPass; }
    const vkb::BuiltRenderPass &getOffscreen3DRenderPass() { return offscreen3DRenderPass; }
    vk::Format getDepthFormat() const { return depthFormat; }
    vk::RenderPass getSwapchainRenderPass() const { return renderpass; }
    vk::RenderPass getUiMsaaRenderPass() const { return uiRenderPass; }
    /** @brief Sample count the UI MSAA pass runs with (matches getUiMsaaRenderPass). */
    vk::SampleCountFlagBits getUiMsaaSamples() const { return uiColorSamples; }
    /** @brief Create the UI MSAA render pass (once) and its color targets for ImGui init. */
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
    /** @brief Render the present overlay (ImGui) into the UI MSAA pass and queue resolve. */
    bool renderUiOverlayPass();
    void recordPendingShadowPasses();
    void recordPendingGBufferPass();
    void dropPendingOffscreenPasses();
    /** @brief acquire + record deferred shadow/gbuffer + begin swapchain RP. */
    bool beginSwapchainRenderPass();
    /** @brief Begin the swapchain color+depth RP on an already-acquired present CB. */
    void beginSwapchainColorPass();
    bool beginSceneColorRenderPass();
    void endSceneColorRenderPass();
    void ensureOffscreen3DResources();
    void destroyOffscreen3DResources();
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
                                             const vkb::BuiltRenderPass &rp,
                                             vk::PipelineLayout layout,
                                             BlendMode mode = BlendMode::Alpha);
    vk::Pipeline createMesh3DStylePipeline(const std::vector<uint32_t> &vert,
                                           const std::vector<uint32_t> &frag,
                                           vk::PipelineLayout layout,
                                           const vkb::BuiltRenderPass &rp,
                                           vk::SampleCountFlagBits samples);
    /** @brief X-ray overlay variant: depth test/write off + alpha blend (occluded silhouettes). */
    vk::Pipeline createMesh3DXrayPipeline(const std::vector<uint32_t> &vert,
                                          const std::vector<uint32_t> &frag,
                                          vk::PipelineLayout layout,
                                          const vkb::BuiltRenderPass &rp,
                                          vk::SampleCountFlagBits samples);
    vk::Pipeline createMesh3DHairPipeline(const std::vector<uint32_t> &vert,
                                          const std::vector<uint32_t> &frag,
                                          vk::PipelineLayout layout,
                                          const vkb::BuiltRenderPass &rp,
                                          vk::SampleCountFlagBits samples);
    /** @brief Rebuild scene-pass pipelines against the given render pass / sample count. */
    void ensureScenePassPipelines(const vkb::BuiltRenderPass &target,
                                  vk::SampleCountFlagBits samples);
    /** @brief Clamp a requested sample count to the device-supported set (0/1/2/4/8). */
    int clampMsaaSamples(int requested) const;
    /** @brief Render pass a scene-pass pipeline should be built against right now. */
    const vkb::BuiltRenderPass &activeScenePass() const {
        return sceneColorRenderPass ? sceneColorRenderPass : renderpass;
    }
    /** @brief Sample count the scene pass is currently running with (e1 when no MSAA). */
    vk::SampleCountFlagBits activeSceneSamples() const {
        return sceneColorRenderPass ? sceneColorSamples : vk::SampleCountFlagBits::e1;
    }
    void destroySwapchainResources();
    void flushBatch();
    void flushToSwapchain();
    void flushToOffscreen(OffscreenCanvas *canvas);
    void abortOpen3DFrame();
    void noteSolidOverlay();
    void noteTexturedOverlay(Texture *tex);
    void noteLitOverlay();
    void clear2DBatches();
    void drawLitBatches(vk::CommandBuffer cb, int viewW, int viewH, vk::Pipeline pipeline,
                        std::vector<LitBatch> &batches, std::vector<vkb::HostVertexBuffer> &texBufs,
                        size_t &texBufIndex, bool offscreen);
    vkb::BoundSet lit2dSetFor(GpuTexture *albedo, GpuTexture *normal, bool offscreen);
    vkb::BoundSet post2SetFor(GpuTexture *color, GpuTexture *depth);
    void ensureFlatNormalTexture();
    void captureSwapchainImage(uint32_t imageIndex);
    void ensurePresentCaptureHook();
    vkb::BoundSet mesh3dSetFor(GpuTexture *gpuTex, GpuTexture *normalTex, GpuTexture *envTex,
                              GpuTexture *heightTex, GpuTexture *depthTex,
                              Mesh3dFrameSlots &fslots, size_t uboSlot);
    void ensureDefaultEnvCubemap();
    void ensureFlatNormalTexture3D();
    void ensureFlatHeightTexture3D();
    vk::Sampler createVkSampler(const TextureSampler &sampler, uint32_t mipLevels) const;
    void writeCombinedImageDescriptor(GpuTexture *gpu);
    /** @brief Rebuild surface/swapchain when dirty. Returns false if surface not ready. */
    bool rebuildSwapchainIfNeeded();
    /** @brief acquire + begin command buffer; recreates swapchain and retries on failure. */
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
    vk::Pipeline solidAlphaPipeline;       // alpha-blended solid (BlendMode::Alpha)
    vk::Pipeline additiveSolidPipeline;    // additive solid (BlendMode::Additive)

    vk::DescriptorSetLayout texSetLayout;
    vk::UniqueDescriptorSetLayout texSetLayoutUnique;
    vk::DescriptorPool descriptorPool;
    vk::Pipeline texPipeline;
    vk::Pipeline additiveTexPipeline;
    vk::Pipeline opaqueTexPipeline;
    vk::PipelineLayout texPipelineLayout;
    vk::PipelineLayout shaderPipelineLayout;  // tex set + push constants
    vk::CommandPool uploadPool;

    vkb::BuiltRenderPass offscreenRenderPass;
    vk::Pipeline offscreenSolidPipeline;
    vk::Pipeline offscreenSolidAlphaPipeline;
    vk::Pipeline offscreenAdditiveSolidPipeline;
    vk::Pipeline offscreenTexPipeline;
    vk::Pipeline offscreenAdditiveTexPipeline;
    vk::Pipeline offscreenOpaqueTexPipeline;

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
        GpuTexture *depth = nullptr;
        bool operator==(const Mesh3dSetKey &o) const {
            return albedo == o.albedo && normal == o.normal && env == o.env && height == o.height &&
                   depth == o.depth;
        }
    };
    struct Mesh3dSetKeyHash {
        size_t operator()(const Mesh3dSetKey &k) const {
            return std::hash<GpuTexture *>()(k.albedo) ^
                   (std::hash<GpuTexture *>()(k.normal) << 1) ^
                   (std::hash<GpuTexture *>()(k.env) << 2) ^
                   (std::hash<GpuTexture *>()(k.height) << 3) ^
                   (std::hash<GpuTexture *>()(k.depth) << 4);
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
    /** @brief 1x1 white cubemap used as the bindless cubemap-array placeholder. */
    Texture *defaultBindlessCube = nullptr;
    Texture *flatNormalTexture3D = nullptr;
    Texture *flatHeightTexture3D = nullptr;
    Texture *defaultEnvCubemap = nullptr;
    Texture *mesh3dNormalTexture = nullptr;
    Texture *mesh3dHeightTexture = nullptr;
    Texture *mesh3dEnvTexture = nullptr;
    Texture *mesh3dSceneDepthTexture = nullptr;
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

    // ---- GPU-driven (stage 0): bindless set + per-frame arena + tables ----
    GpuDrivenCaps gpuDrivenCaps_{};
    std::vector<FrameArena> frameArenas_;

    vk::UniqueDescriptorSetLayout bindlessSetLayoutUnique_{};
    vk::DescriptorSetLayout bindlessSetLayout_ = nullptr;
    vk::DescriptorPool bindlessPool_ = nullptr;
    vk::DescriptorSet bindlessSet_ = nullptr;
    std::vector<GpuTexture *> bindlessTextures2D_;
    std::vector<uint32_t> bindlessFree2D_;
    std::vector<GpuTexture *> bindlessCubemaps_;
    std::vector<uint32_t> bindlessFreeCube_;

    vkb::GenericBuffer meshTableBuffer_;
    std::vector<GpuMeshRecord> meshTableRecords_;
    /** @brief Parallel to meshTableRecords_: the GpuMesh owning each record (for binding). */
    std::vector<GpuMesh *> meshRecordOwners_;
    uint32_t meshTableCapacity_ = 0;

    vkb::GenericBuffer materialTableBuffer_;
    std::vector<GpuMaterialRecord> materialTableRecords_;
    std::vector<uint32_t> materialTableFree_;
    std::unordered_map<const Material *, uint32_t> materialTableIndex_;
    uint32_t materialTableCapacity_ = 0;

    uint32_t registerBindlessTexture2D(GpuTexture *tex);
    uint32_t registerBindlessTextureCube(GpuTexture *tex);
    void unregisterBindlessTexture(GpuTexture *tex);
    uint32_t registerMeshRecord(GpuMesh *gpu);
    void syncMeshTable();
    GpuMaterialRecord buildMaterialRecord(Material *material);
    void createBindlessSet();

    // ---- GPU-driven (stage 1): opaque forward path ----
    bool gpuDrivenEnabled_ = false;
    uint32_t lastGpuDrivenDrawCount_ = 0;
    vk::PipelineLayout mesh3dGpuDrivenPipelineLayout = nullptr;
    vk::Pipeline mesh3dGpuDrivenPipeline = nullptr;
    void createMesh3DGpuDrivenPipeline();

    // ---- GPU-driven (stage 2): HZB + GPU cull ----
    struct GpuDrivenCullSlot {
        vkb::GenericBuffer visibleFlags;    // uint32 per instance (GPU-written)
        vkb::GenericBuffer compacted;       // GpuInstance per instance (GPU-written)
        vkb::GenericBuffer indirect;        // GpuIndirectCommand per bucket (GPU-written)
        vkb::GenericBuffer bucketCounters;  // uint32 per bucket (GPU atomic, CPU-reset)
        vkb::GenericBuffer hzb;             // header + R32F mip chain (GPU-written)
        vkb::GenericBuffer cullParams;      // GpuCullParams UBO (CPU-written)
    };
    std::vector<GpuDrivenCullSlot> gpuDrivenCullSlots_;
    vkb::GenericBuffer gpuDrivenCullParamsPlaceholder_;  // valid UBO target at set creation
    int gpuDrivenCullWidth = 0;
    int gpuDrivenCullHeight = 0;
    uint32_t gpuDrivenCullMaxMip = 0;
    vk::PipelineLayout gpuDrivenComputeLayout = nullptr;
    vk::DescriptorSetLayout gpuDrivenComputeEmptyLayout_ = nullptr;
    ComputePass hzbBuildPass_;
    ComputePass cullPass_;
    ComputePass emitPass_;
    bool gpuDrivenCullReady_ = false;
    bool gpuDrivenScenePassPending_ = false;
    // Per-frame CPU metadata for the cull chain (uploaded to the frame arena).
    std::vector<uint32_t> gpuDrivenBucketIds_;
    std::vector<uint32_t> gpuDrivenBucketOffsets_;
    std::vector<uint32_t> gpuDrivenBucketMeshIds_;
    uint32_t gpuDrivenBucketCount_ = 0;
    uint32_t gpuDrivenCullInstanceCount_ = 0;
    uint32_t gpuDrivenLastCullSlot_ = 0;
    FrameArena::Alloc gpuDrivenInstAlloc_{};
    FrameArena::Alloc gpuDrivenBucketIdAlloc_{};
    FrameArena::Alloc gpuDrivenBucketOffAlloc_{};
    void ensureGpuDrivenCullResources(int width, int height);
    void recordGpuDrivenHzbBuild();
    void destroyGpuDrivenCullResources();
    GpuDrivenCullSlot &currentGpuDrivenCullSlot();
    GpuDrivenCullSlot &gpuDrivenCullSlot(uint32_t frameSlot);

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
    vk::PipelineLayout shadowAlphaPipelineLayout{};
    vk::Pipeline shadowAlphaPipeline{};
    int shadowPassCascade = -1;
    struct ShadowDraw {
        Mesh *mesh = nullptr;
        glm::mat4 mvp{1.f};
        Texture *albedo = nullptr;
        bool alphaTest = false;  // use the alpha-cutout shadow pipeline
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
        bool alphaTest = false;  // use the alpha-cutout gbuffer pipeline
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
    vk::Pipeline gbufferAlphaPipeline{};
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

    // Offscreen 3D render-to-canvas (color + D32 depth) used for planar
    // reflection / render targets. Rendered on a dedicated command buffer and
    // submitted directly to the graphics queue (no swapchain / present).
    vkb::BuiltRenderPass offscreen3DRenderPass{};
    vk::Pipeline offscreen3DMeshPipeline = nullptr;
    vk::CommandPool offscreen3DPool = nullptr;
    vk::CommandBuffer offscreen3DCB = nullptr;
    vk::Fence offscreen3DFence = nullptr;
    bool offscreen3DPassOpen = false;
    OffscreenCanvas *offscreen3DCanvas = nullptr;

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

    struct SolidBatch {
        BlendMode blend = BlendMode::Alpha;
        Batcher batch;
    };
    std::vector<SolidBatch> solidBatches;
    struct TexturedBatch {
        Texture *texture = nullptr;
        Texture *depth = nullptr;
        Shader *shader = nullptr;
        BlendMode blend = BlendMode::Alpha;
        Batcher batch;
    };
    std::vector<TexturedBatch> texturedBatches;

    std::vector<LitBatch> litBatches;

    enum class OverlayKind : uint8_t { Solid, Textured, Lit };
    struct OverlaySpan {
        OverlayKind kind = OverlayKind::Solid;
        uint32_t index = 0;
        uint32_t vertBegin = 0;
        uint32_t vertCount = 0;
    };
    std::vector<OverlaySpan> overlaySpans;
    std::vector<OverlaySpan> engine3DSpans;
    std::optional<TexturedBatch> pendingSceneResolve;
    std::optional<TexturedBatch> pendingUiResolve;
    bool sceneColorComposited = false;

    // Persistent host-visible vertex buffers for 2D batching, reused across
    // frames. GenericBuffer now owns the Vulkan handles.
    struct Frame2DBuffers {
        std::vector<vkb::HostVertexBuffer> solidBufs;
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
    bool flushingSwapchain_ = false;
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
        vkb::GenericBuffer aoBuffer;
        size_t aoCapacityBytes = 0;
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
    void invalidateTextureBindings();
};

}  // namespace eve::graphics::vulkan
