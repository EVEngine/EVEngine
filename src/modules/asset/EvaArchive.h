#pragma once

/**
 * @file EvaArchive.h
 * @brief Deterministic and bounded ZIP64 container for EVEngine source assets.
 */

#include "asset/EvaManifest.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eve::asset {

/** @brief Admission budgets applied before allocating decoded archive entries. */
struct EvaArchiveLimits {
    std::uint64_t maximumArchiveBytes = 4ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumEntryBytes   = 1ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumDecodedBytes = 8ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumManifestBytes = 4ULL * 1024 * 1024;
    std::uint32_t maximumEntries       = 100000;
    std::uint32_t maximumPathBytes     = 1024;
};

/** @brief One owning regular-file entry inside an `.eva` archive. */
struct EvaArchiveEntry {
    std::string               path;
    std::vector<std::uint8_t> bytes;
};

/** @brief Fully admitted source archive with its typed manifest and payloads. */
struct EvaArchive {
    EvaManifest                  manifest;
    std::vector<EvaArchiveEntry> entries;
};

/**
 * @brief Build a deterministic ZIP64 `.eva`, selecting raw Deflate only when smaller than Store.
 * @param manifest Typed manifest written as the root `manifest.json` entry.
 * @param entries Regular payload files; callers must not supply `manifest.json`.
 * @param limits Resource budgets enforced before construction.
 * @return Complete owning archive bytes or a structured diagnostic.
 * @thread Worker-safe when inputs are not concurrently mutated.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> buildEvaArchive(
    const EvaManifest& manifest, std::vector<EvaArchiveEntry> entries,
    const EvaArchiveLimits& limits = {});

/**
 * @brief Parse and fully verify an untrusted `.eva` ZIP64 image.
 * @param bytes Complete archive bytes whose lifetime need only cover this call.
 * @param limits Resource budgets checked before decoded allocations.
 * @return Owning admitted archive or a structured diagnostic.
 * @thread Worker-safe; no state is retained across calls.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<EvaArchive> parseEvaArchive(
    std::span<const std::uint8_t> bytes, const EvaArchiveLimits& limits = {});

}  // namespace eve::asset
