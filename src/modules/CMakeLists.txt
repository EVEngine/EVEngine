
create_module(EVFileSystem filesystem)
create_module(EVGraphics graphics)
create_module(EVWindow window)
create_module(EVEvent event)
create_module(EVTimer timer)
create_module(EVMouse mouse)
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