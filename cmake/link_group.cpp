// Registration anchor and CRT translation unit for the link-group shared libraries.
//
// A link group is built with
//     add_library(<group> SHARED link_group.cpp)
//     target_link_libraries(<group> PRIVATE <module OBJECT libraries...>)
// rather than passing $<TARGET_OBJECTS:...> as sources: naming the objects as
// sources bypasses CMake's normal link-input handling and the target then misses
// the MSVC CRT/VC runtime default libraries.
//
// Including a CRT header here is deliberate. With /MDd the compiler emits
// /DEFAULTLIB:MSVCRTD from the command line alone, but the UCRT and VC runtime
// import libraries are requested by pragmas that live inside the CRT headers
// (<corecrt.h> and friends). A target whose own sources include no CRT header
// therefore ends up with MSVCRTD.lib's startup object unable to resolve
// __acrt_initialize / __vcrt_initialize. Including <cstdlib> makes this TU emit
// the same pragma set an ordinary translation unit does, which is what the
// repository's other shared libraries (native_test_plugin) rely on.

#include <cstdlib>

// Consumers reference a unique manifest-derived export so a DLL containing only
// static registrations is retained by the host linker even without API calls.
#if defined(_WIN32)
#define EVE_LINK_GROUP_EXPORT __declspec(dllexport)
#else
#define EVE_LINK_GROUP_EXPORT __attribute__((visibility("default")))
#endif
extern "C" EVE_LINK_GROUP_EXPORT void EVENGINE_LINK_GROUP_ANCHOR() {}
