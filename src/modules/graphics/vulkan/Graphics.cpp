#define VKB_IMPL
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <array>
#include <stdexcept>
#include <vector>

#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <memory>

#include "graphics/shaders/color_vert_spv.inc"
#include "graphics/shaders/color_frag_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"
#include "graphics/shaders/textured_frag_spv.inc"

namespace eve::graphics::vulkan {

Graphics::~Graphics() {
    if (!initialized) return;
    device->waitIdle();
    ownedCanvases.clear();
    ownedTextures.clear();
    for (auto &g : ownedGpuTextures) {
        if (g->sampler) device->destroySampler(g->sampler);
    }
    ownedGpuTextures.clear();
    destroySwapchainResources();
    if (pipeline) device->destroyPipeline(pipeline);
    if (pipelineLayout) device->destroyPipelineLayout(pipelineLayout);
    if (texPipeline) device->destroyPipeline(texPipeline);
    if (texPipelineLayout) device->destroyPipelineLayout(texPipelineLayout);
    if (offscreenSolidPipeline) device->destroyPipeline(offscreenSolidPipeline);
    if (offscreenTexPipeline) device->destroyPipeline(offscreenTexPipeline);
    if (offscreenRenderPass) device->destroyRenderPass(offscreenRenderPass);
    texSetLayoutUnique.reset();
    if (descriptorPool) device->destroyDescriptorPool(descriptorPool);
    if (uploadPool) device->destroyCommandPool(uploadPool);
    if (renderpass) device->destroyRenderPass(renderpass);
    if (surface) inst.instance.destroySurfaceKHR(surface);
}

void Graphics::initWithWindow(void *nativeWindow) {
    if (initialized) return;
    sdlWindow = nativeWindow;
    auto *window = static_cast<SDL_Window *>(nativeWindow);
    ASSERT(window != nullptr);
    if (!window) throw Exception("Graphics::initWithWindow: null SDL_Window");

    unsigned int count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr))
        throw Exception("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

    std::vector<const char *> extNames(count);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &count, extNames.data()))
        throw Exception("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

    vkb::InstanceBuilder builder;
    builder.require_api_version(1, 0).request_validation_layers().use_default_debug_messenger();
    for (auto *name : extNames) builder.enable_extension(name);
    inst = builder.build();

    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
        throw Exception("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
    surface = rawSurface;

    vkb::PhysicalDeviceSelector selector{inst};
    auto phys = selector.set_surface(surface).set_minimum_version(1, 0).select();
    vkb::DeviceBuilder deviceBuilder{phys};
    device = deviceBuilder.build();

    uploadPool = device.createCommandPool();

    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
    int lw = 0, lh = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    setViewportSize(lw, lh, pw, ph);

    createSwapchainAndPipeline();
    createTexturedPipeline();
    initialized = true;
}

void Graphics::destroySwapchainResources() {
    presentModel = vkb::Present{};
    depthImage = vkb::DepthStencilImage{};
}

void Graphics::createSwapchainAndPipeline() {
    vkb::SwapchainBuilder swapchainBuilder{device};
    if (pixelWidth > 0 && pixelHeight > 0)
        swapchainBuilder.set_desired_extent(uint32_t(pixelWidth), uint32_t(pixelHeight));
    auto swapRet = swapchainBuilder.set_old_swapchain(swapchain).build();
    swapchain.destroy();
    swapchain = swapRet;

    depthImage = vkb::DepthStencilImage{device, swapchain.extent.width, swapchain.extent.height, depthFormat};

    if (!renderpass) {
        vkb::RenderPassBuilder rpBuilder{device};
        renderpass =
            rpBuilder.addPresentAttachment(swapchain.image_format, vk::AttachmentLoadOp::eClear)
                .addDepthAttachment(depthFormat, vk::AttachmentLoadOp::eClear,
                                    vk::AttachmentStoreOp::eDontCare)
                .addSubpass(vkb::SubpassBuilder()
                                .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                .setDepthStencilAttachment(
                                    1, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                .addDependency(VK_SUBPASS_EXTERNAL, 0,
                               vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                   vk::PipelineStageFlagBits::eEarlyFragmentTests,
                               vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                   vk::PipelineStageFlagBits::eEarlyFragmentTests,
                               {},
                               vk::AccessFlagBits::eColorAttachmentRead |
                                   vk::AccessFlagBits::eColorAttachmentWrite |
                                   vk::AccessFlagBits::eDepthStencilAttachmentWrite)
                .build();
    }

    if (!pipeline) {
        std::vector<uint32_t> vert(color_vert_spv, color_vert_spv + color_vert_spv_count);
        std::vector<uint32_t> frag(color_frag_spv, color_frag_spv + color_frag_spv_count);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        pipelineLayout = device->createPipelineLayout(layoutInfo);

        vkb::PipelineBuilder pipelineBuilder{device, swapchain};
        pipeline = pipelineBuilder.useClassicPipeline(vert, frag)
                       .setVertexInputState(vkb::VertexInputStateBuilder()
                                                .addInputBinding<ColorVertex>()
                                                .addAttributeDescription<ColorVertex>())
                       .setDynamicStatesViewportScissor()
                       .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                      vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
                       .build(renderpass);
    }

    vkb::PresentBuilder presentBuilder{device, swapchain};
    presentModel = presentBuilder.build(renderpass, depthImage.imageView());
    swapchainDirty = false;
}

void Graphics::createTexturedPipeline() {
    if (texPipeline) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    texSetLayoutUnique = layoutBuilder
                             .image(0, vk::DescriptorType::eCombinedImageSampler,
                                    vk::ShaderStageFlagBits::eFragment, 1)
                             .createUnique(device.instance);
    texSetLayout = *texSetLayoutUnique;

    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 64};
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    descriptorPool = device->createDescriptorPool(poolInfo);

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &texSetLayout;
    texPipelineLayout = device->createPipelineLayout(plInfo);

    std::vector<uint32_t> vert(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    std::vector<uint32_t> frag(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    vk::ShaderModule vertModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, frag);

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto binding = TexturedVertex::getBindingDescription(0);
    auto attrs = TexturedVertex::getAttributeDescription(0);
    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo vp{};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rs{};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable = false;
    ds.depthWriteEnable = false;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAtt.blendEnable = true;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAtt;

    vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = texPipelineLayout;
    pci.renderPass = renderpass;
    pci.subpass = 0;

    auto result = device->createGraphicsPipeline(nullptr, pci);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create textured pipeline");
    texPipeline = result.value;

    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
}

void Graphics::ensureOffscreenPipelines() {
    if (offscreenRenderPass) return;

    vkb::RenderPassBuilder rpBuilder{device};
    vk::AttachmentDescription ad{};
    ad.format = vk::Format::eR8G8B8A8Unorm;
    ad.samples = vk::SampleCountFlagBits::e1;
    ad.loadOp = vk::AttachmentLoadOp::eClear;
    ad.storeOp = vk::AttachmentStoreOp::eStore;
    ad.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    ad.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    ad.initialLayout = vk::ImageLayout::eUndefined;
    ad.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    offscreenRenderPass =
        rpBuilder.addAttachment(ad)
            .addSubpass(vkb::SubpassBuilder().addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal))
            .addDependency(VK_SUBPASS_EXTERNAL, 0)
            .build();

    std::vector<uint32_t> vert(color_vert_spv, color_vert_spv + color_vert_spv_count);
    std::vector<uint32_t> frag(color_frag_spv, color_frag_spv + color_frag_spv_count);
    vkb::PipelineBuilder solidBuilder{device, swapchain};
    offscreenSolidPipeline =
        solidBuilder.useClassicPipeline(vert, frag)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<ColorVertex>()
                                     .addAttributeDescription<ColorVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eCounterClockwise)
            .build(offscreenRenderPass);

    // Textured offscreen pipeline mirrors createTexturedPipeline but with offscreen RP.
    std::vector<uint32_t> tvert(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    std::vector<uint32_t> tfrag(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, tvert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, tfrag);

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto binding = TexturedVertex::getBindingDescription(0);
    auto attrs = TexturedVertex::getAttributeDescription(0);
    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;
    vk::PipelineViewportStateCreateInfo vp{};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    vk::PipelineRasterizationStateCreateInfo rs{};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;
    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;
    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAtt.blendEnable = true;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.alphaBlendOp = vk::BlendOp::eAdd;
    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAtt;
    vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = texPipelineLayout;
    pci.renderPass = offscreenRenderPass;
    pci.subpass = 0;
    auto result = device->createGraphicsPipeline(nullptr, pci);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create offscreen textured pipeline");
    offscreenTexPipeline = result.value;
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
}

Texture *Graphics::getTexture() { return nullptr; }

image::ImageData *Graphics::newImageData() {
    throw Exception("Graphics::newImageData: screen readback not implemented");
}

Color Graphics::getPixel(int, int) {
    throw Exception("Graphics::getPixel: screen readback not implemented");
}

Canvas *Graphics::newCanvas(int w, int h) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    if (!initialized) throw Exception("newCanvas: graphics not initialized");
    if (w <= 0 || h <= 0) throw Exception("newCanvas: invalid size");
    ensureOffscreenPipelines();
    auto c = std::make_unique<OffscreenCanvas>(this, w, h);
    Canvas *raw = c.get();
    ownedCanvases.push_back(std::move(c));
    return raw;
}

void Graphics::setCanvas(Canvas *canvas) {
    Canvas *next = canvas;
    if (next == static_cast<Canvas *>(this)) next = nullptr;
    if (next == activeCanvas) return;
    if (!solidBatch.empty() || !texturedBatches.empty()) flushBatch();
    activeCanvas = next;
}

bool Graphics::isCanvasActive() const {
    return activeCanvas != nullptr;
}

Canvas *Graphics::getCanvas() const {
    return activeCanvas ? activeCanvas : const_cast<Graphics *>(this);
}

void Graphics::setViewportSize(int newW, int newH, int newPw, int newPh) {
    bool changed = (newW != width) || (newH != height) || (newPw != pixelWidth) || (newPh != pixelHeight);
    width = newW;
    height = newH;
    pixelWidth = newPw;
    pixelHeight = newPh;
    if (initialized && changed) swapchainDirty = true;
}

void Graphics::clear(std::optional<Color> color, std::optional<int>, std::optional<double>) {
    clearColor = color.value_or(backgroundColor);
    hasPendingClear = true;
    solidBatch.clear();
    texturedBatches.clear();
    if (auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas)) {
        oc->clear(clearColor, std::nullopt, std::nullopt);
    }
}

void Graphics::drawSolidRect(float x, float y, float w, float h, const Color &color) {
    solidBatch.addRect(x, y, w, h, color);
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    ASSERT(rgba != nullptr);
    if (!initialized) throw Exception("newTexture: graphics not initialized");
    if (w <= 0 || h <= 0 || !rgba) throw Exception("newTexture: invalid args");

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h));
    std::vector<uint8_t> bytes(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    vkb::SamplerBuilder sb;
    gpu->sampler = sb.magFilter(vk::Filter::eNearest)
                       .minFilter(vk::Filter::eNearest)
                       .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                       .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                       .build(device.instance);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
    gpu->descriptorSet = sets[0];

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(gpu->descriptorSet)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(gpu->sampler, gpu->image.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .update(device.instance);

    auto tex = std::make_unique<Texture>();
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    tex->gpuHandle = gpu.get();

    Texture *raw = tex.get();
    ownedTextures.push_back(std::move(tex));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

Texture *Graphics::newTexture(image::ImageData *data) {
    ASSERT(data != nullptr);
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 ImageData supported for now");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()));
}

Texture *Graphics::newTextureFromFile(const std::string &filename) {
    ASSERT(!filename.empty());
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");

    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fileData(fs->read(filename));
    if (!fileData) throw Exception("newTextureFromFile: failed to read '%s'", filename.c_str());

    auto *imgMod = image::Image::create();
    std::unique_ptr<image::ImageData> data(imgMod->newImageData(fileData.get()));
    return newTexture(data.get());
}

void Graphics::drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture) {
        texturedBatches.push_back(TexturedBatch{texture, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, 0, 0, 1, 1);
}

void Graphics::flushBatch() {
    if (!initialized) return;
    if (isCanvasActive()) {
        auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas);
        if (!oc) throw Exception("flushBatch: active canvas is not an OffscreenCanvas");
        flushToOffscreen(oc);
    } else {
        flushToSwapchain();
    }
}

void Graphics::flushToOffscreen(OffscreenCanvas *canvas) {
    Batcher solid = solidBatch;
    auto textured = std::move(texturedBatches);
    solidBatch.clear();
    texturedBatches.clear();

    const Color cc = canvas->pendingClearColor();
    const bool needClear = canvas->takePendingClear();
    if (solid.empty() && textured.empty() && !needClear) return;

    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                canvas->colorImage().setLayout(cb, vk::ImageLayout::eColorAttachmentOptimal);

                                vk::ClearValue cv{
                                    vk::ClearColorValue(std::array<float, 4>{cc.r, cc.g, cc.b, cc.a})};
                                vk::RenderPassBeginInfo rpBegin{};
                                rpBegin.renderPass = offscreenRenderPass;
                                rpBegin.framebuffer = canvas->framebuffer();
                                rpBegin.renderArea.extent =
                                    vk::Extent2D{uint32_t(canvas->getWidth()), uint32_t(canvas->getHeight())};
                                rpBegin.clearValueCount = 1;
                                rpBegin.pClearValues = &cv;
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

                                vk::Viewport vp{0.f, 0.f, float(canvas->getWidth()), float(canvas->getHeight()),
                                                0.f, 1.f};
                                vk::Rect2D scissor{{0, 0},
                                                   {uint32_t(canvas->getWidth()), uint32_t(canvas->getHeight())}};
                                cb.setViewport(0, 1, &vp);
                                cb.setScissor(0, 1, &scissor);

                                vkb::HostVertexBuffer solidBuf;
                                std::vector<vkb::HostVertexBuffer> texBufs;
                                texBufs.reserve(textured.size());

                                if (!solid.empty() && offscreenSolidPipeline) {
                                    Batcher ndc = solid;
                                    ndc.toNDC(canvas->getWidth(), canvas->getHeight());
                                    std::vector<ColorVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(ColorVertex{v.pos, v.color});
                                    solidBuf.allocate<ColorVertex>(device, gpuVerts);
                                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenSolidPipeline);
                                    vk::DeviceSize offset = 0;
                                    cb.bindVertexBuffers(0, 1, solidBuf, &offset);
                                    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                }

                                if (offscreenTexPipeline) {
                                    for (auto &tb : textured) {
                                        if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) continue;
                                        auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
                                        Batcher ndc = tb.batch;
                                        ndc.toNDC(canvas->getWidth(), canvas->getHeight());
                                        std::vector<TexturedVertex> gpuVerts;
                                        gpuVerts.reserve(ndc.vertices().size());
                                        for (const auto &v : ndc.vertices())
                                            gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});
                                        texBufs.emplace_back();
                                        texBufs.back().allocate<TexturedVertex>(device, gpuVerts);
                                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenTexPipeline);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                                              &gpu->descriptorSet, 0, nullptr);
                                        vk::DeviceSize offset = 0;
                                        cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
                                        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                    }
                                }

                                cb.endRenderPass();
                                canvas->colorImage().setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                            });
}

void Graphics::flushToSwapchain() {
    if (swapchainDirty) {
        device->waitIdle();
        createSwapchainAndPipeline();
    }

    Batcher solid = solidBatch;
    auto textured = std::move(texturedBatches);
    solidBatch.clear();
    texturedBatches.clear();

    presentModel.begin();
    std::array<vk::ClearValue, 2> clears{};
    clears[0].color =
        vk::ClearColorValue(std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
    clears[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    presentModel.beginRenderPass(renderpass, clears.data(), uint32_t(clears.size()));

    auto &cb = presentModel.getCurrentCommandBuffer();
    vk::Viewport vp{0.f, 0.f, float(swapchain.extent.width), float(swapchain.extent.height), 0.f, 1.f};
    vk::Rect2D scissor{{0, 0}, swapchain.extent};
    cb.setViewport(0, 1, &vp);
    cb.setScissor(0, 1, &scissor);

    vkb::HostVertexBuffer solidBuf;
    std::vector<vkb::HostVertexBuffer> texBufs;
    texBufs.reserve(textured.size());

    if (!solid.empty() && pipeline) {
        Batcher ndc = solid;
        ndc.toNDC(width, height);
        std::vector<ColorVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(ColorVertex{v.pos, v.color});
        solidBuf.allocate<ColorVertex>(device, gpuVerts);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, solidBuf, &offset);
        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
    }

    if (texPipeline) {
        for (auto &tb : textured) {
            if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) continue;
            auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
            Batcher ndc = tb.batch;
            ndc.toNDC(width, height);
            std::vector<TexturedVertex> gpuVerts;
            gpuVerts.reserve(ndc.vertices().size());
            for (const auto &v : ndc.vertices())
                gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

            texBufs.emplace_back();
            texBufs.back().allocate<TexturedVertex>(device, gpuVerts);
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, texPipeline);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                  &gpu->descriptorSet, 0, nullptr);
            vk::DeviceSize offset = 0;
            cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
            cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
        }
    }

    presentModel.endRenderPass();
    presentModel.end();
    presentModel.drawFrame();
    hasPendingClear = false;
}

void Graphics::present() {
    if (!initialized) return;
    if (isCanvasActive()) throw Exception("present: cannot present while a Canvas is active");
    flushBatch();
}

void Graphics::draw(eve::graphics::Graphics *, const glm::mat4 &) const {}
void Graphics::draw(Canvas *, const glm::mat4 &) const {}

}  // namespace eve::graphics::vulkan
