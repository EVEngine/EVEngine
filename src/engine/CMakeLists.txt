
create_module(EVCommon common)
create_module(EVDevTools devtools)
create_module(EVCmdLine cmdline)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/common/config.h.in common/config.h)
add_executable(eve main.cpp)

set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static libSDL2.a dl X11 pthread)
set(EVELIBS EVWindow EVMouse EVEvent EVCmdLine EVDevTools EVScripts EVCommon)

target_link_libraries(eve 
    -Wl,--whole-archive ${EVELIBS} -Wl,--no-whole-archive ${ThirdParty})