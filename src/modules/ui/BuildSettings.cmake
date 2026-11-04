# --- Dear ImGui --------------------------------------------------------------

eve_module_enabled(ui _has_ui)
if(_has_ui)
  # Stage the editor icon font beside other runtime shared data. The backend
  # resolves this from SDL_GetBasePath, so games may run from any working dir.
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/eve/fonts")
  configure_file(
    "${CMAKE_SOURCE_DIR}/src/modules/ui/fonts/FontAwesome.ttf"
    "${CMAKE_BINARY_DIR}/share/eve/fonts/FontAwesome.ttf" COPYONLY)
  if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    # ImGui keeps its context in the process-wide GImGui. Linking the archive
    # into every link group and every test executable gives each of them its own
    # copy, so a context the test creates is invisible to the ui module and the
    # six ui cases abort with "No current context" (spec section 7.3). Compile
    # the vendored sources into one shared library instead: the state then has a
    # single owner, exactly as it does in the archive-only OBJECT route where
    # everything lands in one executable.
    set(_eve_imgui_sources
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_draw.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_tables.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_widgets.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_sdl.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_vulkan.cpp
    )
    add_library(eve_imgui SHARED ${_eve_imgui_sources})
    # GImGui is a data symbol, and an import library only carries __imp_<data>,
    # so consumers must see the declaration as dllimport rather than redeclare it.
    # Passed through SHELL: with the quotes intact: cmd.exe treats the
    # parentheses of __declspec(...) as separators, which turns a plain
    # -DIMGUI_API=__declspec(dllimport) into a stray token and breaks every
    # consumer TU (C2054 in imgui.h). ELF exports default-visibility symbols from
    # a shared object anyway, so the flag is Windows-only.
    if(WIN32)
      target_compile_options(eve_imgui PRIVATE "SHELL:/D\"IMGUI_API=__declspec(dllexport)\"")
      target_compile_options(eve_imgui INTERFACE "SHELL:/D\"IMGUI_API=__declspec(dllimport)\"")
    endif()
    set_target_properties(eve_imgui PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    # Vendored sources: the engine's strict set (/W4 /WX, or -Wall -Wextra
    # -Werror) targets our own code, and the ExternalProject path compiles this
    # library without it. Downgrade errors to warnings for this target only.
    if(MSVC)
      target_compile_options(eve_imgui PRIVATE /WX-)
    else()
      target_compile_options(eve_imgui PRIVATE -Wno-error)
    endif()
    if(NOT MSVC AND EVENGINE_ENABLE_STRICT_WARNINGS)
      target_compile_options(eve_imgui PRIVATE -Wno-deprecated-enum-enum-conversion)
    endif()
    target_include_directories(eve_imgui PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
    target_include_directories(eve_imgui PRIVATE
      ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include/SDL2
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/SDL2
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/SDL2
    )
    target_compile_definitions(eve_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)
    # The archive form left these references to whoever linked it; a shared
    # library has to resolve them itself: the SDL backend calls into SDL2 (and
    # therefore the OS libraries SDL2 needs), the Vulkan backend into the loader.
    eve_thirdparty_libs(_eve_imgui_third_party sdl2)
    eve_append_system_libraries(_eve_imgui_system_libs)
    target_link_libraries(eve_imgui PRIVATE
      ${_eve_imgui_third_party} ${_eve_imgui_system_libs} ${EVENGINE_VULKAN_LIB})
    target_link_libraries(eve_imgui PUBLIC eve_engine_includes)
    # The SDL import library this target links comes from the third-party install
    # tree, and nothing else in its dependency graph waits for that install: on a
    # cold build (no third-party cache) ninja may reach this link first and fail
    # with LNK1104 "cannot open file 'SDL2d.lib'". create_module() adds the same
    # dependency for every module target.
    if(NOT EVENGINE_PROFILE_HOSTLESS)
      add_dependencies(eve_imgui third-party)
    endif()
    # The host, the tests and the group libraries all load this library, so the
    # dynamic SDK has to ship it beside them, exactly like the link groups.
    install(TARGETS eve_imgui
      RUNTIME DESTINATION bin
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib)
  elseif(EVENGINE_THIRD_PARTY_BINARY_ONLY AND NOT BUILD_PLATFORM STREQUAL "webgpu")
    # The shared prefix already contains ImGui and its SDL/Vulkan backends.
    add_library(eve_imgui INTERFACE)
    if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_link_libraries(eve_imgui INTERFACE imguimdd)
    elseif(WIN32)
      target_link_libraries(eve_imgui INTERFACE imguimd)
    else()
      target_link_libraries(eve_imgui INTERFACE imgui)
    endif()
    # INTERFACE library: only propagate the shared include set to consumers.
    target_link_libraries(eve_imgui INTERFACE eve_engine_includes)
  else()
    # Compiled from third-party sources (also installed by the ExternalProject).
    set(_eve_imgui_sources
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_draw.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_tables.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_widgets.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_sdl.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_vulkan.cpp
    )
    if(BUILD_PLATFORM STREQUAL "webgpu")
      # Vulkan renderer is compiled out; the WebGPU backend is compiled by EVUI.
      list(REMOVE_ITEM _eve_imgui_sources
        ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_vulkan.cpp)
    endif()
    add_library(eve_imgui STATIC ${_eve_imgui_sources})
    # Vendored third-party imgui mixes public/private enums (input-text/tree
    # flags); GCC 15's -Wdeprecated-enum-enum-conversion is noise for this
    # bundled library. Engine call sites that mix enums are fixed at the
    # source instead (only relevant when strict warnings are enabled).
    if(NOT MSVC AND EVENGINE_ENABLE_STRICT_WARNINGS)
      target_compile_options(eve_imgui PRIVATE -Wno-deprecated-enum-enum-conversion)
    endif()
    target_include_directories(eve_imgui PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
    # On Emscripten SDL2 headers come from the port sysroot.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
      target_include_directories(eve_imgui PRIVATE ${EMSCRIPTEN_SYSROOT}/include)
    else()
      target_include_directories(eve_imgui PRIVATE
        ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include/SDL2
        ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/SDL2
        ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/SDL2
      )
    endif()
    target_compile_definitions(eve_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)
    # STATIC library: PUBLIC so eve_imgui's own TUs (imgui_impl_vulkan.cpp /
    # imgui_impl_sdl.cpp) compile against the shared include set (Vulkan SDK,
    # SDL2, engine headers) and consumers inherit it transitively.
    target_link_libraries(eve_imgui PUBLIC eve_engine_includes)
  endif()

  target_link_libraries(EVUI PUBLIC eve_imgui)
  target_include_directories(EVUI PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
  target_include_directories(EVUI BEFORE PUBLIC
    ${EVENGINE_VKBUILDER_INCLUDE_DIR})
endif()

