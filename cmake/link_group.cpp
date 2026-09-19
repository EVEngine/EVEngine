// Placeholder translation unit for the link-group shared libraries.
//
// A link group is built with
//     add_library(<group> SHARED link_group.cpp)
//     target_link_libraries(<group> PRIVATE <module OBJECT libraries...>)
// rather than passing $<TARGET_OBJECTS:...> as sources. Naming the objects as
// sources bypasses CMake's normal link-input handling, and on MSVC the target
// then does not end up with the CRT/VC runtime default libraries: the link fails
// on UCRT and VC runtime internals (__acrt_initialize, __CxxFrameHandler4, ...)
// even though the module objects themselves were compiled with /MDd.
//
// The real content of each group comes from the module object libraries; this
// file only gives the target a source of its own so CMake treats it as an
// ordinary library.

namespace eve::internal {
void linkGroupAnchor() {}
}  // namespace eve::internal
