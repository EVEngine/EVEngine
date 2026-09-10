#pragma once

#include "common/BorrowedRef.h"
#include "common/Result.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::level_editing {
class LevelDocument;

/** @brief Plug-in contract for importing and exporting a level representation. */
class LevelFormat {
public:
    virtual ~LevelFormat()                                                  = default;
    virtual std::string              id() const                             = 0;
    virtual std::vector<std::string> extensions() const                     = 0;
    virtual bool                     canRead(const std::string& text) const = 0;

    [[nodiscard]] virtual eve::Result<std::unique_ptr<LevelDocument>> read(const std::string& text) const     = 0;
    [[nodiscard]] virtual eve::Result<std::string>                    write(const LevelDocument& level) const = 0;
};

/**
 * @brief Registry and conversion gateway for level formats.
 *
 * Own formats may be registered from C++ with registerFormat(). Built-ins are
 * `eve.level` (version 1) and `tiled.json` (finite array-backed Tiled JSON maps).
 * Unmodeled fields are owned by the document and preserved on export. Group,
 * image, infinite and encoded layers are rejected instead of flattened/lost.
 * All operations are synchronous on the document owner thread, without callbacks.
 */
class LevelFormatRegistry {
public:
    LevelFormatRegistry();
    [[nodiscard]] eve::Result<void> registerFormat(std::unique_ptr<LevelFormat> format);
    int                             getFormatCount() const { return static_cast<int>(formats_.size()); }
    std::string                     getFormatId(int index) const;
    std::string                     detect(const std::string& path, const std::string& text) const;

    [[nodiscard]] eve::Result<std::unique_ptr<LevelDocument>> decode(const std::string& format,
                                                                     const std::string& text) const;
    [[nodiscard]] eve::Result<std::string> encode(const std::string& format, const LevelDocument& level) const;

    [[nodiscard]] eve::Result<std::unique_ptr<LevelDocument>> load(const std::string& path,
                                                                   const std::string& format = {}) const;
    /** @brief Encode then atomically replace a file; failures preserve an existing destination.
     * @param path Destination path; relative asset references are preserved verbatim.
     * @param level Synchronously borrowed document, never retained or mutated.
     * @param format Registered format, or infer it from the extension.
     * @return Structured encoding or filesystem failure, or successful replacement.
     * @thread Document owner thread; no callbacks. This is not a power-loss durability guarantee.
     */
    [[nodiscard]] eve::Result<void>                           save(const std::string& path, const LevelDocument& level,
                                                                   const std::string& format = {}) const;

private:
    [[nodiscard]] eve::OptionalRef<const LevelFormat> find(const std::string& id) const;
    std::vector<std::unique_ptr<LevelFormat>>         formats_;
};
}  // namespace eve::level_editing
