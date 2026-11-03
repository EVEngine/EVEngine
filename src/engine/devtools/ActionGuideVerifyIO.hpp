#pragma once

/**
 * @file ActionGuideVerifyIO.hpp
 * @brief Filesystem helpers for ActionGuide geometric verify.
 */

#include "common/ActionGuideVerify.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <filesystem>
#include <string>

namespace eve::dev {

/**
 * @brief Load guide silhouette PNGs + candidate frames into a verify request.
 * @remarks guideArtifactDir uses export layout `shots/NNNN/silhouette.png`.
 *          candidateDir uses ingest layout `NNNN/frame.png`.
 * @cost Disk IO + PNG decode × shot count.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideVerifyRequest> loadActionGuideVerifyRequest(
    const ActionGuidePack& pack, const std::filesystem::path& guideArtifactDir,
    const std::filesystem::path& candidateDir);

/**
 * @brief Write verify.evidence.json under outputRoot (creates directories).
 * @cost Small JSON encode + filesystem write.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> writeActionGuideVerifyEvidence(
    const ActionGuideVerifyReport& report, const std::filesystem::path& outputRoot);

}  // namespace eve::dev
