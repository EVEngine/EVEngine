cmake_minimum_required(VERSION 3.21)
project(EVEngineUnitySourceTests LANGUAGES C CXX)

# CPU-only admission tests: no engine host, graphics backend or asset store login.
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
find_package(ZLIB REQUIRED)
add_executable(unity_source_tests
    "${EVE_ROOT}/src/engine/common/Utf8Validation.cpp"
    "${EVE_ROOT}/test/asset_import_unity_package.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnityPackage.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnitySource.cpp")
target_include_directories(unity_source_tests PRIVATE "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(unity_source_tests PRIVATE zeroerr ZLIB::ZLIB)
if(MSVC)
    target_compile_options(unity_source_tests PRIVATE /utf-8 /EHsc /W4)
endif()
enable_testing()
add_test(NAME asset.import.unitySource COMMAND unity_source_tests)

add_library(unity_import_core STATIC
    "${EVE_ROOT}/src/modules/asset/import/UnitySpriteAnimation.cpp"
    "${EVE_ROOT}/src/engine/common/Utf8Validation.cpp"
    "${EVE_ROOT}/src/engine/common/Identity.cpp"
    "${EVE_ROOT}/src/engine/common/Uri.cpp"
    "${EVE_ROOT}/src/engine/common/ResourceRef.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Json.cpp"
    "${EVE_ROOT}/src/engine/common/Exception.cpp"
    "${EVE_ROOT}/src/engine/common/RenderTrace.cpp"
    "${EVE_ROOT}/src/modules/data/HashFunction.cpp"
    "${EVE_ROOT}/src/modules/asset/CanonicalImageCook.cpp"
    "${EVE_ROOT}/src/modules/asset/import/ImportReport.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnityCollection.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnityFbx.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnityRenderBindings.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnityImporter.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnitySource.cpp"
    "${EVE_ROOT}/src/modules/asset/import/UnityPackage.cpp"
    "${EVE_ROOT}/src/modules/asset/import/ImageImporter.cpp"
    "${EVE_ROOT}/src/modules/asset/import/GltfImporter.cpp"
    "${EVE_ROOT}/src/modules/asset/import/TerrainImporter.cpp")
target_include_directories(unity_import_core PUBLIC "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(unity_import_core PUBLIC zeroerr ZLIB::ZLIB)
find_path(ASSIMP_INCLUDE_DIR assimp/Importer.hpp REQUIRED)
find_library(ASSIMP_LIBRARY NAMES assimpmdd assimp REQUIRED)
find_library(XXHASH_LIBRARY NAMES xxHashmdd xxHash REQUIRED)
target_include_directories(unity_import_core PRIVATE "${ASSIMP_INCLUDE_DIR}" "${EVE_ROOT}/third-party")
target_link_libraries(unity_import_core PUBLIC "${ASSIMP_LIBRARY}" "${XXHASH_LIBRARY}")
if(MSVC)
    target_compile_options(unity_import_core PRIVATE /utf-8 /EHsc)
endif()

find_path(UTF8PROC_INCLUDE_DIR utf8proc.h REQUIRED)
find_library(UTF8PROC_LIBRARY NAMES utf8proc_static utf8proc REQUIRED)
find_path(ZSTD_INCLUDE_DIR zstd.h REQUIRED)
find_library(ZSTD_LIBRARY NAMES zstd_static zstd REQUIRED)
file(GLOB EVE_ASSET_SOURCES "${EVE_ROOT}/src/modules/asset/*.cpp")
list(REMOVE_ITEM EVE_ASSET_SOURCES "${EVE_ROOT}/src/modules/asset/CanonicalImageCook.cpp")
target_sources(unity_import_core PRIVATE ${EVE_ASSET_SOURCES})
target_include_directories(unity_import_core PRIVATE "${UTF8PROC_INCLUDE_DIR}" "${ZSTD_INCLUDE_DIR}")
target_compile_definitions(unity_import_core PRIVATE UTF8PROC_STATIC)
target_link_libraries(unity_import_core PUBLIC "${UTF8PROC_LIBRARY}" "${ZSTD_LIBRARY}")
add_executable(unity_collection_tests "${EVE_ROOT}/test/asset_import_unity_collection.cpp")
target_sources(unity_collection_tests PRIVATE "${EVE_ROOT}/test/asset_migration.cpp")
target_sources(unity_collection_tests PRIVATE "${EVE_ROOT}/test/asset_scene_bindings.cpp")
target_sources(unity_collection_tests PRIVATE "${EVE_ROOT}/test/asset_import_unity_sprite.cpp")
target_link_libraries(unity_collection_tests PRIVATE unity_import_core)
if(MSVC)
    target_compile_options(unity_collection_tests PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME asset.import.unityCollection COMMAND unity_collection_tests)
add_executable(unity_fbx_tests "${EVE_ROOT}/test/asset_import_unity_fbx.cpp")
target_include_directories(unity_fbx_tests PRIVATE "${ASSIMP_INCLUDE_DIR}")
target_link_libraries(unity_fbx_tests PRIVATE unity_import_core)
if(MSVC)
    target_compile_options(unity_fbx_tests PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME asset.import.unityFbx COMMAND unity_fbx_tests)
add_executable(unity_fbx_unavailable UnityFbxUnavailable.cpp "${EVE_ROOT}/src/modules/asset/import/UnityFbx.cpp")
target_compile_definitions(unity_fbx_unavailable PRIVATE __EMSCRIPTEN__)
target_link_libraries(unity_fbx_unavailable PRIVATE unity_import_core)
if(MSVC)
    target_compile_options(unity_fbx_unavailable PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME asset.import.unityFbxProviderAbsent COMMAND unity_fbx_unavailable)
set_tests_properties(asset.import.unityFbx PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed")
target_sources(unity_import_core PRIVATE "${EVE_ROOT}/src/modules/asset/scene/EvpackSceneTemplateLoader.cpp")
target_sources(unity_import_core PRIVATE
    "${EVE_ROOT}/src/modules/asset/graphics/EvpackStaticPrefab.cpp"
    "${EVE_ROOT}/src/modules/asset/graphics/EvpackGraphicsLoader.cpp"
    "${EVE_ROOT}/src/modules/asset/graphics/EvpackImageLoader.cpp")
target_include_directories(unity_import_core PRIVATE "${EVE_ROOT}/third-party/glm")
add_executable(unity_package_probe UnityPackageProbe.cpp)
target_link_libraries(unity_package_probe PRIVATE unity_import_core)
if(MSVC)
    target_compile_options(unity_package_probe PRIVATE /utf-8 /EHsc)
endif()
set_tests_properties(asset.import.unitySource asset.import.unityCollection PROPERTIES
    FAIL_REGULAR_EXPRESSION "Assertion Failed")

# Compile the production CLI entry point even when the full host cannot be built.
find_path(CLI11_INCLUDE_DIR CLI11.hpp REQUIRED)
add_library(unity_command_compile OBJECT "${EVE_ROOT}/src/modules/asset/import/AssetCommand.cpp")
target_include_directories(unity_command_compile PRIVATE "${CLI11_INCLUDE_DIR}")
target_link_libraries(unity_command_compile PRIVATE unity_import_core)
if(MSVC)
    target_compile_options(unity_command_compile PRIVATE /utf-8 /EHsc)
endif()
