#pragma once

/**
 * @file ActionGuideExport.hpp
 * @brief Write a Ready guide-capture snapshot to the guide-pack artifact directory layout.
 */

#include "common/ActionGuidePack.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <filesystem>
#include <string>

namespace eve::dev {

/**
 * @brief Encode Ready silhouette/depth(/bone) buffers as PNG + manifest under `root`.
 * @param snapshot Ready lease borrowed for this call; bytes must remain valid for the duration.
 * @param root Absolute destination directory (created if missing).
 * @return Owning Value summarizing written relative paths, or a structured failure.
 * @remarks Layout: `manifest.json` + `shots/NNNN/{meta.json,silhouette.png,depth.png,…}`.
 * @cost Linear in shot × AOV × canvas pixels (PNG encode); amortize by reusing Ready leases.
 * @thread Caller thread; may perform filesystem IO. Does not touch GPU.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> writeActionGuideArtifactPack(
    const ActionGuideCaptureSnapshot& snapshot, const std::filesystem::path& root);

/** @brief Map pack AOV name to export file stem (`silhouette` → `silhouette.png`). */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> actionGuideAovFileStem(std::string_view aov);

}  // namespace eve::dev
