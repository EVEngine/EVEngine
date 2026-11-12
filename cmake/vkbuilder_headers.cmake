# VKBuilder is independently developed and pinned by the engine submodule.
# Source builds and SDKs consume these canonical headers without rewriting them.
set(EVENGINE_VKBUILDER_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/VKBuilder/include")
if(NOT EXISTS "${EVENGINE_VKBUILDER_INCLUDE_DIR}/vkbuilder.hpp")
    message(FATAL_ERROR "VKBuilder submodule missing; run git submodule update --init --recursive")
endif()
