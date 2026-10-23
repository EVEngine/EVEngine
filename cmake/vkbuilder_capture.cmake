# Engine-owned generated-header adjustment; never modify the pinned submodule.
# Depth attachments must opt into transfer-source usage before capture can copy them.
foreach(_capture_original IN ITEMS
    "info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;"
    "info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;")
    string(FIND "${_vkbuilder_source}" "${_capture_original}" _capture_offset)
    if(_capture_offset EQUAL -1)
        message(FATAL_ERROR "Pinned VKBuilder depth capture allocation contract changed")
    endif()
    string(REPLACE "${_capture_original}"
        "${_capture_original}\n    info.usage |= vk::ImageUsageFlagBits::eTransferSrc;"
        _vkbuilder_source "${_vkbuilder_source}")
endforeach()

# Preserve the actual creation argument as immutable swapchain diagnostics.
# Tests inspect this value instead of patching the process-wide Vulkan dispatcher.
set(_present_original "  vk::Format image_format;\n  vk::Extent2D extent = {0, 0};")
set(_present_fixed "  vk::Format image_format;\n  /** @brief Actual mode used at creation; render-thread diagnostics, valid until destroy. */\n  vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;\n  vk::Extent2D extent = {0, 0};")
string(FIND "${_vkbuilder_source}" "${_present_original}" _present_offset)
if(_present_offset EQUAL -1)
    message(FATAL_ERROR "Pinned VKBuilder swapchain diagnostics contract changed")
endif()
string(REPLACE "${_present_original}" "${_present_fixed}" _vkbuilder_source "${_vkbuilder_source}")
set(_present_original "    swapchain.image_format = surface_format.format;")
string(FIND "${_vkbuilder_source}" "${_present_original}" _present_offset)
if(_present_offset EQUAL -1)
    message(FATAL_ERROR "Pinned VKBuilder swapchain creation contract changed")
endif()
string(REPLACE "${_present_original}"
    "${_present_original}\n    swapchain.present_mode = swapchain_create_info.presentMode;"
    _vkbuilder_source "${_vkbuilder_source}")
