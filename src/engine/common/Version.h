#pragma once

#include "common/Export.h"

#include <string>

/** @brief Build metadata (engine git commit, build time, third-party version). */
namespace eve {

/**
 * @brief One-line build metadata for diagnostics (printed by `eve --version`).
 * @return "v<engine version> git=<commit> built=<time> [<build type>] tp=<third-party version>"
 */
EVENGINE_API std::string buildInfo();

/** @return Short engine git commit, or "unknown" when not built from a git checkout. */
EVENGINE_API const char* gitCommit();

/** @return Third-party version this build was linked against (source@<commit> / prebuilt@<commit>). */
EVENGINE_API const char* thirdPartyVersion();

}  // namespace eve
