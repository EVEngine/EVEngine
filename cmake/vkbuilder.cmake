include_guard(GLOBAL)
# Both source/prebuilt builds consume the same unmodified canonical dependency.
set(EVENGINE_VKBUILDER_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/VKBuilder/include")
if(NOT EXISTS "${EVENGINE_VKBUILDER_INCLUDE_DIR}/vkbuilder/binding_validation.hpp")
    message(FATAL_ERROR
        "The pinned external/VKBuilder submodule is required. Run git submodule update --init --recursive.")
endif()
