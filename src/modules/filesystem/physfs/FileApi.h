#pragma once

namespace ssq {
class VM;
}

namespace eve {
namespace filesystem {
namespace physfs {

/** @brief 用 PhysFS 版本覆盖 Squirrel 的 `file` / `dofile` / `loadfile` 全局，
 * 使脚本与资源从已挂载的游戏源（真实目录或内存挂载的 .eve 归档）解析；
 * PhysFS 中不存在的路径回退到 OS 文件系统，保持原有行为。 */
void installScriptFileApi(ssq::VM& vm);

}  // namespace physfs
}  // namespace filesystem
}  // namespace eve
