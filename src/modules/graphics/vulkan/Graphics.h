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
};

struct GpuTexture {
    vkb::TextureImage2D image;
    vkb::TextureImageCube cubeImage;
    bool isCube = false;
    vk::Sampler sampler;
    vk::DescriptorSet descriptorSet;
    int width = 0;
    int height = 0;
    uint32_t mipLevels = 1;
    TextureSampler samplerState{};

    vk::ImageView imageView() const {
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
};

class Graphics final : public eve::graphics::Graphics {
public:
    ~Graphics() override;

    std::string getBackendName() const override;

    void initWithWindow(void *nativeWindow) override;
    void present() override;
    void requestSurfaceRecreate() override { surfaceNeedsRecreate = true; }
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
                                  const Color &color) override;
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
    Mesh *newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                            int vertexCount, const uint32_t *indices, int indexCount) override;
    bool bakeMeshMorph(Mesh *mesh) override;
    Mesh *newMeshSphere(int slices = 32, int stacks = 16) override;
    Mesh *newMeshCylinder(int slices = 32, int stacks = 1, bool caps = true) override;
    void begin3DFrame() override;
    void setMesh3DViewProj(const glm::mat4 &viewProj) override;
    void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) override;
    void drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                        Shader *shader) override;
    void drawVoxelFaceInstances(const uint32_t *packed, int count, float originX, float originY,
                                float originZ, const std::string &faceDir, Texture *atlas,
                                int tilesPerRow = 16) override;
    void setMesh3DNormalTexture(Texture *normal) override;
    void setMesh3DMaterial(float metallic, float roughness) override;
    void setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount = 1.f) override;
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
    vk::RenderPass getOffscreenRenderPass() const { return offscreenRenderPass; }
    vk::RenderPass getSwapchainRenderPass() const { return renderpass; }
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
    void createSwapchainAndPipeline();
    void createTexturedPipeline();
    void createLit2DPipeline();
    void createMesh3DPipeline();
    void createMesh3DClusteredPipeline();
    void createVoxelRectPipeline();
    void destroyVoxelRectResources();
    void ensureVoxelUnitQuad();
    vk::DescriptorSet voxelRectSetFor(GpuTexture *gpuTex);
    void createShadowResources();
    void destroyShadowResources();
    void ensureClusteredBuffers(size_t lightsBytes, size_t tableBytes, size_t indicesBytes);
    void uploadClusteredLighting(const ClusteredLightingUpload &upload);
    vk::DescriptorSet mesh3dClusteredSetFor(GpuTexture *gpuTex, GpuTexture *normalTex,
                                            GpuTexture *envTex, size_t uboSlot);
    void ensureOffscreenPipelines();
    void ensureShaderOffscreenPipeline(Shader *shader);
    vk::Pipeline createTexturedStylePipeline(const std::vector<uint32_t> &vert,
                                             const std::vector<uint32_t> &frag,
                                             vk::RenderPass rp, vk::PipelineLayout layout);
    vk::Pipeline createMesh3DStylePipeline(const std::vector<uint32_t> &vert,
                                           const std::vector<uint32_t> &frag,
                                           vk::PipelineLayout layout);
    vk::Pipeline createMesh3DHairPipeline(const std::vector<uint32_t> &vert,
                                          const std::vector<uint32_t> &frag,
                                          vk::PipelineLayout layout);
    void destroySwapchainResources();
    void flushBatch();
    void flushToSwapchain();
    void flushToOffscreen(OffscreenCanvas *canvas);
    void drawLitBatches(vk::CommandBuffer cb, int viewW, int viewH, vk::Pipeline pipeline,
                        std::vector<LitBatch> &batches, std::vector<vkb::HostVertexBuffer> &texBufs);
    vk::DescriptorSet lit2dSetFor(GpuTexture *albedo, GpuTexture *normal);
    void ensureFlatNormalTexture();
    void captureSwapchainImage(uint32_t imageIndex);
    void ensurePresentCaptureHook();
    vk::DescriptorSet mesh3dSetFor(GpuTexture *gpuTex, GpuTexture *normalTex, GpuTexture *envTex,
                                   size_t uboSlot);
    void ensureDefaultEnvCubemap();
    void ensureFlatNormalTexture3D();
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
    vk::RenderPass renderpass;

    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;

    vk::DescriptorSetLayout texSetLayout;
    vk::UniqueDescriptorSetLayout texSetLayoutUnique;
    vk::DescriptorPool descriptorPool;
    vk::Pipeline texPipeline;
    vk::PipelineLayout texPipelineLayout;
    vk::PipelineLayout shaderPipelineLayout;  // tex set + push constants
    vk::CommandPool uploadPool;

    vk::RenderPass offscreenRenderPass;
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
        bool operator==(const Mesh3dSetKey &o) const {
            return albedo == o.albedo && normal == o.normal && env == o.env;
        }
    };
    struct Mesh3dSetKeyHash {
        size_t operator()(const Mesh3dSetKey &k) const {
            return std::hash<GpuTexture *>()(k.albedo) ^
                   (std::hash<GpuTexture *>()(k.normal) << 1) ^
                   (std::hash<GpuTexture *>()(k.env) << 2);
        }
    };
    struct Mesh3dUboSlot {
        vkb::GenericBuffer ubo;
        vkb::GenericBuffer shadowUbo;
        std::unordered_map<Mesh3dSetKey, vk::DescriptorSet, Mesh3dSetKeyHash> sets;
    };
    std::vector<Mesh3dUboSlot> mesh3dUboSlots;
    size_t mesh3dDrawIndex = 0;
    Texture *whiteTexture = nullptr;
    Texture *flatNormalTexture3D = nullptr;
    Texture *defaultEnvCubemap = nullptr;
    Texture *mesh3dNormalTexture = nullptr;
    Texture *mesh3dEnvTexture = nullptr;
    float mesh3dEnvIntensity = 0.f;
    float mesh3dMetallic = 0.f;
    float mesh3dRoughness = 0.45f;
    float mesh3dTexBombScale = 4.f;
    float mesh3dTexBombStrength = 0.f;
    float mesh3dTexBombRot = 1.f;
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
    vkb::GenericBuffer clusteredLightsBuf;
    vkb::GenericBuffer clusteredTableBuf;
    vkb::GenericBuffer clusteredIndicesBuf;
    size_t clusteredLightsCap = 0;
    size_t clusteredTableCap = 0;
    size_t clusteredIndicesCap = 0;
    struct Mesh3dClusteredUboSlot {
        vkb::GenericBuffer ubo;
        vkb::GenericBuffer shadowUbo;
        std::unordered_map<Mesh3dSetKey, vk::DescriptorSet, Mesh3dSetKeyHash> sets;
    };
    std::vector<Mesh3dClusteredUboSlot> mesh3dClusteredUboSlots;
    size_t mesh3dClusteredDrawIndex = 0;

    // CSM shadow map (3 cascade layers).
    vk::Image shadowImage{};
    vk::DeviceMemory shadowMemory{};
    vk::ImageView shadowArrayView{};
    vk::ImageView shadowLayerViews[ShadowConfig::kCascades]{};
    vk::Framebuffer shadowFramebuffers[ShadowConfig::kCascades]{};
    vk::Sampler shadowSampler{};
    vk::RenderPass shadowRenderPass{};
    vk::PipelineLayout shadowPipelineLayout{};
    vk::Pipeline shadowPipeline{};
    int shadowPassCascade = -1;
    struct ShadowDraw {
        Mesh *mesh = nullptr;
        glm::mat4 mvp{1.f};
    };
    std::vector<ShadowDraw> shadowPassDraws;

    vkb::Present presentModel;
    vkb::DepthStencilImage depthImage;
    vk::Format depthFormat = vk::Format::eD32Sfloat;

    Batcher solidBatch;
    struct TexturedBatch {
        Texture *texture = nullptr;
        Shader *shader = nullptr;
        Batcher batch;
    };
    std::vector<TexturedBatch> texturedBatches;

    std::vector<LitBatch> litBatches;

    vk::DescriptorSetLayout lit2dSetLayout;
    vk::UniqueDescriptorSetLayout lit2dSetLayoutUnique;
    vk::PipelineLayout lit2dPipelineLayout;
    vk::Pipeline lit2dPipeline;
    vk::Pipeline offscreenLitPipeline;
    vkb::GenericBuffer lighting2dUbo;
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
    std::unordered_map<LitSetKey, vk::DescriptorSet, LitSetKeyHash> lit2dSets;
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
    std::unordered_map<GpuTexture *, vk::DescriptorSet> voxelRectSets;
    // Grow-only instance buffer pool (reset index each begin3DFrame).
    struct VoxelInstanceSlot {
        vkb::GenericBuffer buffer;
        size_t capacityBytes = 0;
    };
    std::vector<VoxelInstanceSlot> voxelInstanceSlots;
    size_t voxelInstanceDrawIndex = 0;
};

}  // namespace eve::graphics::vulkan
