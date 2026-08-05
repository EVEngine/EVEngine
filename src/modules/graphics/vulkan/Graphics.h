#pragma once
#include "graphics/Graphics.h"
#include "graphics/Batcher.h"
#include "graphics/Texture.h"
#include "graphics/Mesh.h"
#include "vkbuilder.hpp"
#include <memory>
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
    glm::mat4 mvp{1.f};
    glm::mat4 model{1.f};
    glm::vec4 lightDir{0.4f, 1.f, 0.3f, 0.f};
    glm::vec4 lightColor{1.f, 1.f, 1.f, 1.f};
    glm::vec4 tint{1.f, 1.f, 1.f, 1.f};
};

struct GpuTexture {
    vkb::TextureImage2D image;
    vk::Sampler sampler;
    vk::DescriptorSet descriptorSet;
    int width = 0;
    int height = 0;
};

struct GpuMesh {
    vkb::HostVertexBuffer vertices;
    vkb::GenericBuffer indices;
    uint32_t indexCount = 0;
};

class Graphics final : public eve::graphics::Graphics {
public:
    ~Graphics() override;

    void initWithWindow(void *nativeWindow) override;
    void present() override;
    void setViewportSize(int width, int height, int pixelwidth, int pixelheight) override;
    void drawSolidRect(float x, float y, float w, float h, const Color &color) override;
    Texture *newTexture(int width, int height, const uint8_t *rgba) override;
    Texture *newTexture(image::ImageData *data) override;
    Texture *newTextureFromFile(const std::string &filename) override;
    void drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) override;
    Mesh *newMeshFromAssimp(const ::aiMesh &mesh) override;
    void begin3DFrame() override;
    void setMesh3DViewProj(const glm::mat4 &viewProj) override;
    void drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) override;
    void setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) override;

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

    friend class OffscreenCanvas;

private:
    void createSwapchainAndPipeline();
    void createTexturedPipeline();
    void createMesh3DPipeline();
    void ensureOffscreenPipelines();
    void destroySwapchainResources();
    void flushBatch();
    void flushToSwapchain();
    void flushToOffscreen(OffscreenCanvas *canvas);
    void captureSwapchainImage(uint32_t imageIndex);
    void ensurePresentCaptureHook();

    bool initialized = false;
    bool hasPresentedFrame = false;
    std::vector<uint8_t> lastFrameRgba;
    Canvas *activeCanvas = nullptr;
    bool swapchainDirty = false;
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
    vk::CommandPool uploadPool;

    vk::RenderPass offscreenRenderPass;
    vk::Pipeline offscreenSolidPipeline;
    vk::Pipeline offscreenTexPipeline;

    vk::DescriptorSetLayout mesh3dSetLayout;
    vk::UniqueDescriptorSetLayout mesh3dSetLayoutUnique;
    vk::PipelineLayout mesh3dPipelineLayout;
    vk::Pipeline mesh3dPipeline;
    vkb::GenericBuffer mesh3dUbo;
    vk::DescriptorSet mesh3dDescriptorSet{};
    Texture *whiteTexture = nullptr;

    vkb::Present presentModel;
    vkb::DepthStencilImage depthImage;
    vk::Format depthFormat = vk::Format::eD32Sfloat;

    Batcher solidBatch;
    struct TexturedBatch {
        Texture *texture = nullptr;
        Batcher batch;
    };
    std::vector<TexturedBatch> texturedBatches;

    Color clearColor{0.1f, 0.1f, 0.12f, 1.0f};
    bool hasPendingClear = true;

    std::vector<std::unique_ptr<Texture>> ownedTextures;
    std::vector<std::unique_ptr<GpuTexture>> ownedGpuTextures;
    std::vector<std::unique_ptr<Mesh>> ownedMeshes;
    std::vector<std::unique_ptr<GpuMesh>> ownedGpuMeshes;
    std::vector<std::unique_ptr<eve::graphics::Canvas>> ownedCanvases;

    bool swapchainPassOpen = false;
    Mesh3DUBO mesh3dFrameUbo{};
};

}  // namespace eve::graphics::vulkan
