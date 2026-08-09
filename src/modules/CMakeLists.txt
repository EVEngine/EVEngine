
create_module(EVFileSystem filesystem)
target_compile_definitions(EVFileSystem PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVGraphics graphics)
create_module(EVWindow window)
create_module(EVEvent event)
create_module(EVTimer timer)
create_module(EVSystem system)
create_module(EVMath math)
create_module(EVIK ik)
target_include_directories(EVIK PUBLIC ${CMAKE_SOURCE_DIR}/external/ik.hpp)
create_module(EVAnimation animation)
create_module(EVTensor tensor)
create_module(EVMouse mouse)
create_module(EVKeyboard keyboard)
create_module(EVJoystick joystick)
create_module(EVTouch touch)
create_module(EVData data)
target_compile_definitions(EVData PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVImage image)
create_module(EVNetwork network)
target_compile_definitions(EVNetwork PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVSound sound)
target_include_directories(EVSound PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/sound/include_shim
  ${CMAKE_SOURCE_DIR}/third-party/medialoader/sound-libs
  ${CMAKE_SOURCE_DIR}/third-party/medialoader/sound-libs/libmodplug/src
  ${CMAKE_SOURCE_DIR}/third-party/medialoader/sound-libs/libogg/include
)
create_module(EVAudio audio)
target_compile_definitions(EVAudio PRIVATE AL_LIBTYPE_STATIC)

# Dear ImGui (compiled from third-party sources; also installed by third-party ExternalProject)
set(_eve_imgui_sources
  ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui.cpp
  ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_draw.cpp
  ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_tables.cpp
  ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_widgets.cpp
  ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_sdl.cpp
  ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_vulkan.cpp
)
add_library(eve_imgui STATIC ${_eve_imgui_sources})
target_include_directories(eve_imgui PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
# imgui_impl_sdl includes <SDL.h>; vendored install puts headers under include/SDL2/.
target_include_directories(eve_imgui PRIVATE
  ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/SDL2
  ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/SDL2
)
# Avoid pulling imgui_demo.cpp (ShowFontAtlas etc.) into every link.
target_compile_definitions(eve_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)

create_module(EVUI ui)
target_link_libraries(EVUI PUBLIC eve_imgui)
target_include_directories(EVUI PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)

create_module(EVScene scene)

create_module(EVParticles particles)
target_compile_definitions(EVParticles PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVPhysics physics)
# Box2D headers use <Box2D/Common/...>; prefer source tree over flattened install.
target_include_directories(EVPhysics PUBLIC ${CMAKE_SOURCE_DIR}/third-party)
create_module(EVMap map)
target_compile_definitions(EVMap PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVGpgpu gpgpu)
create_module(EVModel3D model3d)
create_module(EVFont font)
# ft2build.h lives under include/freetype2/ (installed by third-party FreeType).
target_include_directories(EVFont PRIVATE
  ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/freetype2
  ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/freetype2
)
create_module(EVThread thread)
create_module(EVPlugins plugins)
create_module(EVRPG rpg)
target_compile_definitions(EVRPG PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVAvatar avatar)
create_module(EVDialogue dialogue)
create_module(EVProcgen procgen)
# Compat shims first (Apple Clang: missing drawOperatorArray in upstream DTL), then DTL.
# BEFORE must precede the visibility keyword so -I dtl_compat wins over the submodule.
target_include_directories(EVProcgen BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/procgen/dtl_compat)
target_include_directories(EVProcgen PUBLIC ${CMAKE_SOURCE_DIR}/external/DungeonTemplateLibrary/include)
if(EVENGINE_BUILD_DEMO)
    create_module(EVDemo demo)
endif()
