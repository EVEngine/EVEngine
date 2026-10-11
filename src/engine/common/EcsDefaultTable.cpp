// The process-wide default ECS Table has exactly one owner.
//
// `ecs::default_table()` used to be an inline function with a function-local
// static (external/ECS.hpp). In a single-image build that is one Table, but the
// shared link-group route (EVENGINE_MODULE_LINKAGE=SHARED) puts the engine in
// several DLLs plus the host executable, and MSVC/ELF do not unify an inline
// function's local static across images: the host and each group would address a
// different Table, so an entity created through a domain module would be
// invisible to `ecs::current()` in the caller (and vice versa).
//
// external/ECS.hpp is patched (cmake/patches/ecs-shared-default-table.patch) so
// that in SHARED mode `ecs::default_table()` forwards to
// `ecs::engine_default_table()`, and this translation unit -- compiled into the
// foundation link unit, which every other group already depends on -- owns the
// storage. The OBJECT/static route keeps the inline definition, so a release SDK
// remains a single self-contained executable with no engine DLLs.
//
// The definition repeats EVE_ECS_DEFAULT_TABLE_API on purpose: MSVC rejects a
// plain declaration followed by a dllexport definition (C2375, "redefinition;
// different linkage"), so the header's declaration and this definition must carry
// the same annotation.

#include <ECS.hpp>

#if defined(EVENGINE_MODULE_DLL)

namespace ecs {

EVE_ECS_DEFAULT_TABLE_API Table& engine_default_table() {
    static Table table;
    return table;
}

}  // namespace ecs

#endif
