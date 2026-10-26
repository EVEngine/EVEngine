#include "graphics/vulkan/FrameReadback.h"
#include "graphics/vulkan/Graphics.h"

#include <algorithm>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace eve::graphics::vulkan {

void Graphics::recordFrameCapture(vk::CommandBuffer cb) {
    ++captureFrameId_;
    if (!captureToken_.empty()) return;
    auto queued = frameCaptures().queued();
    if (!queued) return;
    const auto& [token, request] = *queued;
    std::vector<CaptureCopy> copies;
    size_t                   total = 0;
    auto*                    gb    = currentGBufferSlot();
    for (auto attachment : request.attachments) {
        CaptureCopy copy;
        copy.metadata.attachment      = attachment;
        copy.metadata.producerFrameId = captureFrameId_;
        copy.metadata.format          = FramePixelFormat::Rgba8;
        copy.metadata.colorSpace      = "data-unorm-storage";
        copy.metadata.encoding        = "unorm8";
        copy.layout                   = vk::ImageLayout::eShaderReadOnlyOptimal;
        copy.aspect                   = vk::ImageAspectFlagBits::eColor;
        vkb::GenericImage* source     = nullptr;
        if (attachment == FrameAttachment::Final) {
            const auto format = swapchain.image_format;
            const bool bgra   = format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb;
            const bool rgba   = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eR8G8B8A8Srgb;
            if (!bgra && !rgba) copy.metadata.unavailableReason = "UnsupportedFormat";
            if (presentModel.has_acquired_image && (bgra || rgba)) {
                copy.source              = swapchain.get_images()[presentModel.acquired_image_index];
                copy.metadata.width      = static_cast<uint32_t>(pixelWidth);
                copy.metadata.height     = static_cast<uint32_t>(pixelHeight);
                copy.metadata.format     = bgra ? FramePixelFormat::Bgra8 : FramePixelFormat::Rgba8;
                copy.metadata.colorSpace = (format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eR8G8B8A8Srgb)
                                               ? "srgb"
                                               : "swapchain-unorm";
                copy.metadata.encoding   = "presented-including-overlay";
                copy.layout              = vk::ImageLayout::ePresentSrcKHR;
            }
        } else if (attachment >= FrameAttachment::Shadow0) {
            // Do not present a previous slot's cached shadow as this frame's producer.
            if (deferredGraphRecorded_ && deferredGraphRecordedSlot_ == currentFrameSlot() && !shadowMaps.empty()) {
                source      = &currentShadowMap().image;
                copy.layer  = static_cast<uint32_t>(attachment) - static_cast<uint32_t>(FrameAttachment::Shadow0);
                copy.aspect = vk::ImageAspectFlagBits::eDepth;
                copy.metadata.format   = FramePixelFormat::Depth32Float;
                copy.metadata.encoding = "device-depth-0-to-1";
                std::copy_n(glm::value_ptr(mesh3dShadows.ubo.lightVP[copy.layer]), 16,
                            copy.metadata.producerMatrix.begin());
            }
        } else if (gb && deferredGraphRecorded_ && deferredGraphRecordedSlot_ == currentFrameSlot()) {
            switch (attachment) {
                case FrameAttachment::Depth:
                case FrameAttachment::Velocity: source = &gb->depthColor; break;
                case FrameAttachment::HwDepth: source = &gb->depth; break;
                case FrameAttachment::Normal: source = &gb->normal; break;
                case FrameAttachment::Albedo: source = &gb->albedo; break;
                case FrameAttachment::PbrParams: source = &gb->pbrParams; break;
                case FrameAttachment::Emissive: source = &gb->emissive; break;
                default: break;
            }
            if (attachment == FrameAttachment::HwDepth) {
                copy.aspect            = vk::ImageAspectFlagBits::eDepth;
                copy.metadata.format   = FramePixelFormat::Depth32Float;
                copy.metadata.encoding = "device-depth-0-to-1";
            } else if (attachment == FrameAttachment::Depth || attachment == FrameAttachment::Velocity) {
                copy.metadata.encoding = "r-linear01-gb-motion-ndc-times-half-plus-half";
            } else if (attachment == FrameAttachment::Normal) {
                copy.metadata.encoding = "rgb-world-normal-times-half-plus-half-a-legacy-pbr";
            } else if (attachment == FrameAttachment::Albedo) {
                copy.metadata.encoding = "rgb-albedo-times-tint-a-linear01-depth";
            } else if (attachment == FrameAttachment::PbrParams) {
                copy.metadata.encoding = "metallic-roughness-occlusion-specular";
            }
        }
        if (source) {
            const auto& info  = source->info();
            const bool  depth = copy.metadata.format == FramePixelFormat::Depth32Float;
            const bool  supportedFormat =
                depth ? info.format == vk::Format::eD32Sfloat : info.format == vk::Format::eR8G8B8A8Unorm;
            if ((info.usage & vk::ImageUsageFlagBits::eTransferSrc) && supportedFormat &&
                info.samples == vk::SampleCountFlagBits::e1 && copy.layer < info.arrayLayers) {
                copy.source          = source->image();
                copy.metadata.width  = info.extent.width;
                copy.metadata.height = info.extent.height;
            } else
                copy.metadata.unavailableReason =
                    !supportedFormat                                       ? "UnsupportedFormat"
                    : info.samples != vk::SampleCountFlagBits::e1          ? "UnsupportedSampleCount"
                    : !(info.usage & vk::ImageUsageFlagBits::eTransferSrc) ? "TransferSourceUsageMissing"
                                                                           : "InvalidArrayLayer";
        }
        if (!copy.source) {
            if (copy.metadata.unavailableReason.empty()) copy.metadata.unavailableReason = "NotProducedInTargetFrame";
            if (!request.reportUnavailable) {
                auto failed = frameCaptures().fail(
                    token, copy.metadata.unavailableReason + ":" + std::string(frameAttachmentName(attachment)));
                failed.ignore();
                return;
            }
        }
        copy.storageIndex = copies.size();
        for (size_t i = 0; i < copies.size(); ++i) {
            if (copy.source && copies[i].source == copy.source && copies[i].layer == copy.layer) {
                copy.storageIndex = i;
                break;
            }
        }
        if (copy.source && copy.storageIndex == copies.size())
            total += size_t(copy.metadata.width) * copy.metadata.height * 4;
        copies.push_back(std::move(copy));
    }
    auto begin = frameCaptures().begin(token, captureFrameId_, total);
    if (!begin) {
        auto failed = frameCaptures().fail(token, "BudgetExceeded");
        failed.ignore();
        return;
    }
    // Allocate the entire batch before modifying any command buffer.
    try {
        for (size_t i = 0; i < copies.size(); ++i) {
            auto& copy = copies[i];
            if (copy.source && copy.storageIndex == i)
                copy.staging = vkb::GenericBuffer(
                    device, vk::BufferUsageFlagBits::eTransferDst,
                    vk::DeviceSize(copy.metadata.width) * copy.metadata.height * 4,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        }
    } catch (const std::exception& e) {
        auto failed = frameCaptures().fail(token, std::string("ReadbackAllocationFailed: ") + e.what());
        failed.ignore();
        frameCaptures().retire(token);
        return;
    }
    captureToken_                     = token;
    captureCopies_                    = std::move(copies);
    captureSnapshot_                  = {};
    captureSnapshot_.targetGeneration = captureTargetGeneration_;
    captureSnapshot_.depthConvention  = "Vulkan-NDC-0-to-1-positive-viewport-y";
    const glm::mat4 inverseProjection = mesh3dFrameUbo.view * glm::inverse(mesh3dFrameUbo.mvp);
    std::copy_n(glm::value_ptr(inverseProjection), 16, captureSnapshot_.inverseProjection.begin());
    const glm::mat4 projection = glm::inverse(inverseProjection);
    std::copy_n(glm::value_ptr(projection), 16, captureSnapshot_.projectionMatrix.begin());
    std::copy_n(glm::value_ptr(mesh3dFrameUbo.view), 16, captureSnapshot_.viewMatrix.begin());
    for (size_t i = 0; i < captureCopies_.size(); ++i) {
        auto& copy = captureCopies_[i];
        if (!copy.source || copy.storageIndex != i) continue;
        detail::recordFrameReadback(cb, copy.source, copy.staging.buffer,
                                    vk::Extent2D{copy.metadata.width, copy.metadata.height}, copy.aspect, copy.layer,
                                    copy.layout);
    }
    captureFence_ = presentModel.in_flight_fences[presentRecording.slot().index];
    for (auto& copy : captureCopies_) copy.source = nullptr;
}

void Graphics::markFrameCaptureSubmitted() {
    if (captureToken_.empty() || captureSubmitted_) return;
    auto submitted = frameCaptures().submitted(captureToken_);
    submitted.ignore();
    captureSubmitted_ = true;
}

void Graphics::pollFrameCapture() {
    pollEntityInspection();
    frameCaptures().collect();
    if (!captureToken_.empty() && !captureSubmitted_ && device.instance) {
        // A new main-thread poll before submission means present threw. Drain
        // this exceptional path rather than publish unsubmitted staging bytes.
        try {
            device->waitIdle();
        } catch (const vk::SystemError&) {
        }
        auto failed = frameCaptures().fail(captureToken_, "CaptureSubmitFailed");
        failed.ignore();
        destroyFrameCapture();
        return;
    }
    if (!captureSubmitted_ || !captureFence_ || !device.instance) return;
    vk::Result status;
    try {
        status = device->getFenceStatus(captureFence_);
    } catch (const vk::SystemError&) {
        status = vk::Result::eErrorDeviceLost;
    }
    if (status == vk::Result::eNotReady) return;
    if (status != vk::Result::eSuccess) {
        auto failed = frameCaptures().fail(captureToken_, "DeviceLost");
        failed.ignore();
    } else {
        auto state = frameCaptures().snapshot(captureToken_);
        if (state && state.value().phase == FrameCapturePhase::ReadbackPending) {
            try {
                std::vector<std::shared_ptr<const std::vector<uint8_t>>> storage(captureCopies_.size());
                for (size_t i = 0; i < captureCopies_.size(); ++i) {
                    auto& copy = captureCopies_[i];
                    if (copy.staging.buffer && copy.storageIndex == i) {
                        auto        bytes  = std::make_shared<std::vector<uint8_t>>(size_t(copy.metadata.width) *
                                                                                    copy.metadata.height * 4);
                        const void* mapped = copy.staging.map();
                        if (!mapped) throw std::runtime_error("ReadbackMapFailed");
                        std::memcpy(bytes->data(), mapped, bytes->size());
                        copy.staging.unmap();
                        storage[i] = std::move(bytes);
                    }
                    copy.metadata.bytes = storage[copy.storageIndex];
                    captureSnapshot_.buffers.push_back(copy.metadata);
                }
                auto done = frameCaptures().complete(captureToken_, std::move(captureSnapshot_));
                if (!done) {
                    auto failed = frameCaptures().fail(captureToken_, "CapturePublicationFailed");
                    failed.ignore();
                }
            } catch (const std::exception& e) {
                auto failed = frameCaptures().fail(captureToken_, std::string("ReadbackFailed: ") + e.what());
                failed.ignore();
            }
        }
    }
    frameCaptures().retire(captureToken_);
    captureCopies_.clear();
    captureSnapshot_ = {};
    captureToken_.clear();
    captureFence_     = nullptr;
    captureSubmitted_ = false;
}

void Graphics::destroyFrameCapture() {
    destroyEntityInspection();
    ++captureTargetGeneration_;
    // Existing caller has retired the swapchain/device before destroying readback resources.
    if (!captureToken_.empty()) {
        auto failed = frameCaptures().fail(captureToken_, "TargetRecreated");
        failed.ignore();
        frameCaptures().retire(captureToken_);
    }
    captureCopies_.clear();
    captureSnapshot_ = {};
    captureToken_.clear();
    captureFence_     = nullptr;
    captureSubmitted_ = false;
}
}  // namespace eve::graphics::vulkan
