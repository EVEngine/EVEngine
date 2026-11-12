# The filesystem implementation calls WebGPU platform path helpers. Link them
# at the consumer so SHARED groups resolve them without relying on the host exe.
if(TARGET EVFileSystem AND BUILD_PLATFORM STREQUAL "webgpu" AND TARGET EVPlatformWebGPU)
  target_link_libraries(EVFileSystem PRIVATE EVPlatformWebGPU)
endif()
