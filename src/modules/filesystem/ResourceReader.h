#pragma once

/**
 * @file ResourceReader.h
 * @brief Filesystem consumer for strongly typed resource locators.
 */

#include "common/ResourceRef.h"
#include "common/Result.h"
#include "common/ServiceInterfaces.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::filesystem {

/**
 * @brief Reads project resources through the filesystem capability.
 *
 * The typed overload is the source of truth.  The string overload is a
 * compatibility facade for existing project-relative path callers: a legacy
 * path is explicitly converted to `project://...`, while URI-looking input is
 * parsed as a URI and is never silently treated as a path.  The referenced
 * IFileSystem must outlive this reader; reads are synchronous and execute with
 * the provider's thread-affinity contract.
 */
class EVENGINE_API ResourceReader {
public:
    /**
     * @brief Borrow a filesystem capability for synchronous resource reads.
     * @param filesystem Provider that owns and performs the actual read.
     * @remarks The reader does not own or retain the provider beyond its lifetime.
     */
    explicit ResourceReader(eve::service::IFileSystem& filesystem) noexcept : filesystem_(filesystem) {}

    /**
     * @brief Read a supported project resource by typed URI.
     * @param uri Resource locator. Only `project://` is resolved by this
     *             filesystem consumer; other namespaces return Unsupported.
     * @return Owning bytes on success, or a checked diagnostic result. The
     *         returned bytes do not borrow provider storage.
     */
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(const ResourceUri& uri) const;

    /**
     * @brief Compatibility read facade for a legacy path or resource URI.
     * @param legacyPathOrUri Old project-relative path, or one of the strict
     *                        resource URIs accepted by ResourceUri::parse.
     * @return The same checked result as the typed overload. Bare paths are
     *         explicitly converted to project URIs; malformed URI-looking input
     *         is rejected.
     */
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(std::string_view legacyPathOrUri) const;

private:
    eve::service::IFileSystem& filesystem_;
};

}  // namespace eve::filesystem
