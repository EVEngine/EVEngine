#pragma once

/**
 * @file ActionGuideIngestIO.hpp
 * @brief Filesystem load/write helpers for ActionGuide candidate ingest.
 */

#include "common/ActionGuideIngest.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace eve::dev {

/**
 * @brief Decode an 8-bit RGBA PNG into an owning RGBA8 buffer.
 * @remarks Supports only non-interlaced 8-bit RGBA with IHDR/IDAT/IEND (matches FrameCapturePreview encode).
 * @cost Linear in pixel count (inflate + unfilter).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideCandidateFrame> decodeActionGuideCandidatePng(
    std::span<const std::uint8_t> pngBytes);

/**
 * @brief Load `NNNN/{meta.json,frame.png}` under candidateDir into an ordered frame list.
 * @cost Disk IO + PNG decode × frame count.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<ActionGuideCandidateFrame>> loadActionGuideCandidates(
    const std::filesystem::path& candidateDir, const ActionGuidePack& pack);

/**
 * @brief Atomically write frames + clip/ability drafts under outputRoot.
 * @remarks Stages under `.staging` then renames; failure leaves no published tree.
 * @cost Linear in frame count × pixels (PNG encode) plus filesystem IO.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> writeActionGuideIngestPack(
    const ActionGuideIngestDraft& draft, std::span<const ActionGuideCandidateFrame> frames,
    const std::filesystem::path& outputRoot);

}  // namespace eve::dev
