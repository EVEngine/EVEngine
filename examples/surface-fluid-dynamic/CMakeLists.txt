cmake_minimum_required(VERSION 3.21)
project(surface_fluid_example LANGUAGES CXX)

get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(surface_fluid_demo
    main.cpp
    "${EVE_ROOT}/src/modules/fluids/FluidSurfaceBinding.cpp"
    "${EVE_ROOT}/src/modules/fluids/SurfaceDropletSimulation.cpp"
    "${EVE_ROOT}/src/modules/fluids/SurfaceFluidRenderData.cpp"
    "${EVE_ROOT}/src/modules/fluids/SurfaceWetnessField.cpp")
target_compile_features(surface_fluid_demo PRIVATE cxx_std_20)
target_include_directories(surface_fluid_demo PRIVATE
    "${EVE_ROOT}/src/engine"
    "${EVE_ROOT}/src/modules"
    "${EVE_ROOT}/third-party/glm")
if(MSVC)
    target_compile_options(surface_fluid_demo PRIVATE /utf-8 /EHsc)
endif()
