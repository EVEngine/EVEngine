cmake_minimum_required(VERSION 3.21)
project(EVEngineEditorPresentationTests LANGUAGES C CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EVE_PRESENTATION_DEPENDENCIES "" CACHE PATH "Installed third-party include and lib tree")
if(NOT IS_DIRECTORY "${EVE_PRESENTATION_DEPENDENCIES}/include")
    message(FATAL_ERROR "Set EVE_PRESENTATION_DEPENDENCIES to an installed third-party tree")
endif()
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
set(production_sources
    engine/common/Value.cpp engine/common/Json.cpp engine/common/Exception.cpp
    engine/common/SquirrelBinding.cpp engine/common/Subscription.cpp
    engine/common/Module.cpp engine/common/ModuleRegistry.cpp engine/common/Identity.cpp engine/common/RenderTrace.cpp
    engine/common/Diagnostic.cpp engine/common/Status.cpp engine/common/Result.cpp
    engine/common/ResourceAccount.cpp
    modules/editing/EditingResult.cpp
    modules/editing/EditingValue.cpp modules/editing/EditingValueJson.cpp
    modules/editing/EditingProperty.cpp modules/editing/EditingAuthority.cpp modules/editing/EditingTaskService.cpp
    modules/transaction/Transaction.cpp modules/transaction/AtomicBatch.cpp
    modules/transaction/AtomicResourcePayment.cpp modules/transaction/ResourceAccountParticipant.cpp
    modules/property_access/PropertyAccess.cpp
    modules/editor/EditorPropertyPresenter.cpp modules/editor/EditorAssetDatabase.cpp modules/editor/EditorVolumeTarget.cpp
    modules/editor/EditorTransactionConsumer.cpp modules/editor/EditorSimulationPreview.cpp
    modules/editor/presentation/WorkspacePresentation.cpp
    modules/audio/editing/AudioTarget.cpp modules/audio/editing/AudioSerialization.cpp
    modules/audio/editing/AudioTransport.cpp modules/audio/editor/AudioSourceEditor.cpp
    modules/audio/editor/AudioSourcePresentation.cpp modules/audio/editor/AudioSourcePreview.cpp
    modules/animation/editing/AnimationClip.cpp modules/animation/editing/SkeletonOverlay.cpp
    modules/animation/editor/AnimationClipEditor.cpp modules/animation/editor/AnimationClipPresentation.cpp
    modules/animation/editor/AnimationClipPreview.cpp
    modules/avatar/editing/AvatarTarget.cpp modules/avatar/editor/AvatarDocumentEditor.cpp
    modules/voxel/editing/VoxelCatalog.cpp modules/voxel/editor/VoxelCatalogEditor.cpp)
list(TRANSFORM production_sources PREPEND "${EVE_ROOT}/src/")
add_executable(editor_presentation_tests ${production_sources}
    "${EVE_ROOT}/test/integration/editor-workspace/editor_workspace_presentation.cpp"
    "${EVE_ROOT}/test/integration/editor-workspace/editor_async_preview.cpp"
    "${EVE_ROOT}/test/integration/editor-workspace/editor_extended_contracts.cpp")
target_include_directories(editor_presentation_tests PRIVATE
    "${EVE_ROOT}/test" "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules" "${EVE_ROOT}/external/ECS.hpp/src"
    "${EVE_PRESENTATION_DEPENDENCIES}/include")
target_compile_definitions(editor_presentation_tests PRIVATE EVE_PRESENTATION_TEST_NO_AUDIO=1)

# Compile the real current registration sites as well as exercising the shared
# binder in the VM fixtures. Linking the runtime module factories is not needed.
add_library(editor_presentation_bindings_compile OBJECT
    "${EVE_ROOT}/src/modules/audio/editor/AudioEditorModule.cpp"
    "${EVE_ROOT}/src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp")
target_include_directories(editor_presentation_bindings_compile PRIVATE
    $<TARGET_PROPERTY:editor_presentation_tests,INCLUDE_DIRECTORIES>)
find_library(EVE_SQUIRREL NAMES squirrel_staticd squirrel_static HINTS "${EVE_PRESENTATION_DEPENDENCIES}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(EVE_SQLIB NAMES sqstdlib_staticd sqstdlib_static HINTS "${EVE_PRESENTATION_DEPENDENCIES}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(EVE_SIMPLESQUIRREL NAMES simplesquirrel_staticd simplesquirrel_static HINTS "${EVE_PRESENTATION_DEPENDENCIES}/lib" REQUIRED NO_DEFAULT_PATH)
target_link_libraries(editor_presentation_tests PRIVATE zeroerr ${EVE_SIMPLESQUIRREL} ${EVE_SQLIB} ${EVE_SQUIRREL})
if(MSVC)
    target_compile_options(editor_presentation_tests PRIVATE /utf-8 /EHsc /Gy /W4)
    target_link_options(editor_presentation_tests PRIVATE /OPT:REF)
else()
    target_compile_options(editor_presentation_tests PRIVATE -Wall -Wextra -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(editor_presentation_tests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(editor_presentation_tests PRIVATE -Wl,--gc-sections)
    endif()
endif()
enable_testing()
file(STRINGS "${EVE_ROOT}/test/integration/editor-workspace/editor_workspace_presentation.cpp" cases REGEX "^TEST_CASE\\(\"")
file(STRINGS "${EVE_ROOT}/test/integration/editor-workspace/editor_async_preview.cpp" async_cases REGEX "^TEST_CASE\\(\"")
file(STRINGS "${EVE_ROOT}/test/integration/editor-workspace/editor_extended_contracts.cpp" domain_cases REGEX "^TEST_CASE\\(\"")
list(APPEND cases ${async_cases} ${domain_cases})
foreach(line IN LISTS cases)
    string(REGEX REPLACE "^TEST_CASE\\(\"([^\"]+)\".*" "\\1" name "${line}")
    string(REPLACE "." "\\." pattern "${name}")
    add_test(NAME "${name}" COMMAND editor_presentation_tests "--testcase=^${pattern}$")
    set_tests_properties("${name}" PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "editor;cpu")
endforeach()
