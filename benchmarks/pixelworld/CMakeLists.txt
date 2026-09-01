add_executable(pixelworld_benchmark
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/pixelworld/PixelMaterial.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/pixelworld/PixelMaterialCatalogCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/pixelworld/PixelWorld.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/pixelworld/PixelWorldControl.cpp)

eve_thirdparty_libs(_pixelworld_benchmark_poco poco)
find_package(Threads REQUIRED)
target_link_libraries(pixelworld_benchmark PRIVATE
    eve_engine_includes ${_pixelworld_benchmark_poco} Threads::Threads)

if(WIN32)
    target_compile_definitions(pixelworld_benchmark PRIVATE
        POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
endif()
