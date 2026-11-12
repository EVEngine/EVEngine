# Private, unmodified upstream headers. No dependency types escape Scene's ABI.
if(TARGET EVScene)
  include(FetchContent)
  set(HNSWLIB_EXAMPLES OFF CACHE BOOL "Build hnswlib examples" FORCE)
  FetchContent_Declare(evengine_hnswlib
    URL https://codeload.github.com/nmslib/hnswlib/tar.gz/refs/tags/v0.8.0
    URL_HASH SHA256=cf61d7dd8dc9bfba7f4abe0ed26698e90ac9f49a0badb2e1b0f3ba89b72cf3bb
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(evengine_hnswlib)
  target_include_directories(EVScene SYSTEM PRIVATE "${evengine_hnswlib_SOURCE_DIR}")
endif()
