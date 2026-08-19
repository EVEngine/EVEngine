#pragma once

namespace ssq {
class VM;
}

namespace eve {
namespace filesystem {
namespace physfs {

// Override Squirrel's OS-backed `file` / `dofile` / `loadfile` globals with
// PhysFS-backed versions so scripts and assets resolve from the mounted game
// source (a real directory or a memory-mounted .eve archive). Falls back to the
// OS filesystem when a path is not present in PhysFS, preserving prior behavior.
void installScriptFileApi(ssq::VM& vm);

}  // namespace physfs
}  // namespace filesystem
}  // namespace eve
