#include "graphics/vulkan/Canvas.h"

#include "common/Exception.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <array>
#include <cstring>

namespace eve::graphics::vulkan {

OffscreenCanvas::OffscreenCanvas(Graphics *owner, int width, int height)
    : owner(owner), width(width), height(height) {
    ASSERT(owner != nullptr);
    ASSERT_GT(width, 0);
    ASSERT_GT(height, 0);
    if (!owner) throw Exception("OffscreenCanvas: null owner");
    if (width <= 0 || height <= 0) throw Exception("OffscreenCanvas: invalid size");

    auto &device = owner->getDevice();
    color = vkb::ColorAttachmentImage(device, uint32_t(width), uint32_t(height));

    vk::ImageView attachment = color.imageView();
    vk::FramebufferCreateInfo fbInfo{};
    fbInfo.renderPass = owner->getOffscreenRenderPass();
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &attachment;
    fbInfo.width = uint32_t(width);
    fbInfo.height = uint32_t(height);
    fbInfo.layers = 1;
    fb = device->createFramebuffer(fbInfo);

    vkb::SamplerBuilder sb;
    sampleGpu.sampler = sb.magFilter(vk::Filter::eNearest)
                            .minFilter(vk::Filter::eNearest)
                            .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                            .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                            .build(device.instance);

    auto sets = vkb::DescriptorSetBuilder()
                    .layout(owner->getTexSetLayout())
                    .build(device.instance, owner->getDescriptorPool());
    sampleGpu.descriptorSet = sets[0];
    sampleGpu.width = width;
    sampleGpu.height = height;

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(sampleGpu.descriptorSet)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(sampleGpu.sampler, color.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .update(device.instance);

    sampleTexture.width = width;
    sampleTexture.height = height;
    sampleTexture.pixelWidth = width;
    sampleTexture.pixelHeight = height;
    sampleTexture.gpuHandle = &sampleGpu;
}

OffscreenCanvas::~OffscreenCanvas() {
    if (!owner) return;
    auto &device = owner->getDevice();
    device->waitIdle();
    if (fb) {
        device->destroyFramebuffer(fb);
        fb = nullptr;
    }
    if (sampleGpu.sampler) {
        device->destroySampler(sampleGpu.sampler);
        sampleGpu.sampler = nullptr;
    }
    sampleTexture.gpuHandle = nullptr;
}

bool OffscreenCanvas::takePendingClear() {
    bool v = hasPendingClear;
    hasPendingClear = false;
    return v;
}

void OffscreenCanvas::clear(std::optional<Color> colorOpt, std::optional<int>, std::optional<double>) {
    clearColor = colorOpt.value_or(Color(0.f, 0.f, 0.f, 1.f));
    hasPendingClear = true;
}

void OffscreenCanvas::readAllPixels(std::vector<uint8_t> &outRgba) {
    auto &device = owner->getDevice();
    device->waitIdle();

    const vk::DeviceSize byteSize = vk::DeviceSize(width) * vk::DeviceSize(height) * 4;
    vkb::GenericBuffer staging(device, vk::BufferUsageFlagBits::eTransferDst, byteSize,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);

    vkb::executeImmediately(device.instance, owner->getUploadPool(),
                            device.getQueue(vkb::QueueType::graphics), [&](vk::CommandBuffer cb) {
                                color.setLayout(cb, vk::ImageLayout::eTransferSrcOptimal);
                                vk::BufferImageCopy region{};
                                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                                region.imageExtent = vk::Extent3D{uint32_t(width), uint32_t(height), 1};
                                cb.copyImageToBuffer(color.image(), vk::ImageLayout::eTransferSrcOptimal,
                                                     staging.buffer, region);
                                color.setLayout(cb, vk::ImageLayout::eShaderReadOnlyOptimal);
                            });

    outRgba.resize(size_t(byteSize));
    void *mapped = device->mapMemory(staging.memory, 0, byteSize);
    // Vulkan FB row 0 is NDC +Y (top). Batcher maps logical y=0 → NDC +Y, so logical
    // top-left matches FB top-left — but copyImageToBuffer on this path yields a
    // vertically mirrored buffer relative to draw coords. Flip rows so getPixel /
    // newImageData match the Y-down logical space used by drawSolidRect.
    const size_t rowBytes = size_t(width) * 4;
    auto *src = static_cast<const uint8_t *>(mapped);
    for (int y = 0; y < height; ++y) {
        std::memcpy(outRgba.data() + size_t(height - 1 - y) * rowBytes, src + size_t(y) * rowBytes,
                    rowBytes);
    }
    device->unmapMemory(staging.memory);
    staging.release();
}

Color OffscreenCanvas::getPixel(int x, int y) {
    ASSERT_GE(x, 0);
    ASSERT_GE(y, 0);
    ASSERT_LT(x, width);
    ASSERT_LT(y, height);
    if (x < 0 || y < 0 || x >= width || y >= height)
        throw Exception("OffscreenCanvas::getPixel: out of bounds (%d,%d)", x, y);
    std::vector<uint8_t> px;
    readAllPixels(px);
    size_t i = (size_t(y) * size_t(width) + size_t(x)) * 4;
    return Color(px[i + 0] / 255.f, px[i + 1] / 255.f, px[i + 2] / 255.f, px[i + 3] / 255.f);
}

image::ImageData *OffscreenCanvas::newImageData() {
    std::vector<uint8_t> px;
    readAllPixels(px);
    auto *img = new image::ImageData(width, height, "RGBA8");
    std::memcpy(img->getData(), px.data(), px.size());
    return img;
}

}  // namespace eve::graphics::vulkan
