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
