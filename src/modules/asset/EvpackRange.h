#pragma once

/**
 * @file EvpackRange.h
 * @brief Range-readable `.evpack` admission and on-demand verified chunks.
 */

#include "asset/Evpack.h"

#include <filesystem>
#include <memory>

namespace eve::asset {

/** @brief Abstract immutable byte-range provider for files, mmap, HTTP or object storage. */
class EvpackRangeSource {
public:
    virtual ~EvpackRangeSource() = default;

    /** @brief Return the authoritative immutable object size. */
    [[nodiscard]] virtual Result<std::uint64_t> size() const = 0;

    /**
     * @brief Read exactly one validated range.
     * @return Exactly `size` owning bytes or a structured failure.
     * @thread Implementations used concurrently by a mount must be thread-safe.
     */
    [[nodiscard]] virtual Result<std::vector<std::uint8_t>> read(
        std::uint64_t offset, std::uint64_t size) const = 0;
};

/** @brief Thread-safe file-backed range source; each request owns its file stream. */
class FileEvpackRangeSource final : public EvpackRangeSource {
public:
    /** @brief Bind a path; the file is opened separately for every operation. */
    explicit FileEvpackRangeSource(std::filesystem::path path) : path_(std::move(path)) {}
    [[nodiscard]] Result<std::uint64_t> size() const override;
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(
        std::uint64_t offset, std::uint64_t size) const override;
private:
    std::filesystem::path path_;
};

/**
 * @brief Admitted metadata plus an immutable range source retained by shared ownership.
 * @remarks Every returned chunk is owning and SHA-256 verified before exposure.
 */
class EvpackRangeMount {
public:
    /** @brief Verified package metadata and random-access TOC. */
    [[nodiscard]] const Evpack& index() const noexcept { return index_; }

    /** @brief Fetch, size-check and hash-verify one complete independently streamable chunk. */
    [[nodiscard]] Result<std::vector<std::uint8_t>> readChunk(std::size_t index) const;

private:
    friend Result<EvpackRangeMount> prepareEvpackRangeMount(
        std::shared_ptr<const EvpackRangeSource>, const EvpackLimits&, const EvpackTrust&);
    EvpackRangeMount(std::shared_ptr<const EvpackRangeSource> source, Evpack index,
                     EvpackLimits limits)
        : source_(std::move(source)), index_(std::move(index)), limits_(limits) {}
    std::shared_ptr<const EvpackRangeSource> source_;
    Evpack                              index_;
    EvpackLimits                        limits_;
};

/**
 * @brief Read only header/manifest/TOC and prepare on-demand range loading.
 * @param source Immutable source retained for the mount lifetime.
 * @param limits Metadata and chunk budgets.
 * @return Prepared range mount; no chunk payload has been fetched yet.
 */
[[nodiscard]] Result<EvpackRangeMount> prepareEvpackRangeMount(
    std::shared_ptr<const EvpackRangeSource> source, const EvpackLimits& limits = {},
    const EvpackTrust& trust = {});

}  // namespace eve::asset
