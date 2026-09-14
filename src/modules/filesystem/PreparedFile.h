#pragma once
#include <memory>
#include <string>
#include "common/Result.h"
namespace ssq {
class Class;
}
namespace eve::filesystem {
class FileData;
/** @brief Queue immutable file bytes in the canonical ResourceManager cache.
 * @param path VFS path, copied before submission; query suffixes are reserved.
 * @return Submission diagnostic; requires an initialized Filesystem and worker executor.
 * @ownership ResourceManager owns the job and its snapshot. Workers do no VM/GPU work.
 * @thread Game-thread submission; no callbacks. Decode jobs finish before filesystem teardown.
 */
[[nodiscard]] Result<void> requestPreparedFile(const std::string& path);
/** @brief Join a queued read or synchronously read an immutable file snapshot.
 * @param path VFS path. @param limit Maximum accepted byte length, at most 1 GiB.
 * @return Shared immutable bytes, valid after cache unload/reload until the last reader releases them.
 * @thread Game-thread read; no callbacks. Failure publishes no partial snapshot.
 */
[[nodiscard]] Result<std::shared_ptr<const FileData>> readPreparedFile(const std::string& path, size_t limit);
/** @brief Install game-thread script submission bindings; retains no class borrow. */
void exposePreparedFileBindings(ssq::Class& cls);
}  // namespace eve::filesystem
