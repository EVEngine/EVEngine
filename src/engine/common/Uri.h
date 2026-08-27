#pragma once

/**
 * @file Uri.h
 * @brief Strict, canonical resource URI values shared by engine modules.
 */

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace eve {

/**
 * @brief URI schemes understood by the resource layer.
 *
 * These schemes identify virtual resource namespaces, not operating-system
 * paths.  A URI has the canonical shape `scheme://path[?query][#fragment]`.
 */
enum class UriScheme : std::uint8_t {
    Asset,
    Project,
    Builtin,
    Generated,
    Memory,
};

/** @brief Return the stable lower-case spelling of a resource URI scheme. */
[[nodiscard]] EVENGINE_API const char* uriSchemeName(UriScheme scheme) noexcept;

/**
 * @brief An immutable, canonical URI value.
 *
 * This type accepts only the five engine resource schemes.  It deliberately
 * does not accept `file://`, URLs, or a bare path.  URI values own their text
 * and are safe to retain across frames; they contain no borrowed pointers and
 * have no thread affinity.
 */
class EVENGINE_API Uri {
public:
    /**
     * @brief Parse and canonicalize a resource URI.
     * @param text URI in `scheme://path[?query][#fragment]` form.
     * @return A checked URI value, or a parse diagnostic for malformed,
     *         unsupported, absolute, or path-like input.
     */
    [[nodiscard]] static Result<Uri> parse(std::string_view text);

    /**
     * @brief Construct a URI from validated components.
     * @param scheme One of the supported resource schemes.
     * @param path Relative virtual path; `.` and `..` segments are forbidden.
     * @param query Optional query component without the leading `?`.
     * @param fragment Optional fragment component without the leading `#`.
     * @return A canonical URI or a structured parse diagnostic.
     */
    [[nodiscard]] static Result<Uri> fromParts(UriScheme scheme, std::string_view path, std::string_view query = {},
                                               std::string_view fragment = {});

    /** @brief Return the parsed scheme. */
    [[nodiscard]] UriScheme scheme() const noexcept { return scheme_; }

    /** @brief Return the virtual path, borrowed from this URI until it is moved or destroyed. */
    [[nodiscard]] std::string_view path() const noexcept { return path_; }

    /** @brief Return the query component without `?`, borrowed from this URI. */
    [[nodiscard]] std::string_view query() const noexcept { return query_; }

    /** @brief Return the fragment component without `#`, borrowed from this URI. */
    [[nodiscard]] std::string_view fragment() const noexcept { return fragment_; }

    /** @brief Return the stable canonical URI text owned by this value. */
    [[nodiscard]] const std::string& format() const noexcept { return formatted_; }

    /** @brief Compare canonical URI values. */
    friend bool operator==(const Uri&, const Uri&) noexcept = default;

private:
    Uri(UriScheme scheme, std::string path, std::string query, std::string fragment);

    UriScheme   scheme_;
    std::string path_;
    std::string query_;
    std::string fragment_;
    std::string formatted_;
};

/**
 * @brief A semantically named resource locator.
 *
 * `Uri` is the structural parser; `ResourceUri` is the type exposed at
 * resource-module boundaries.  Keeping this wrapper distinct prevents an
 * arbitrary future URI value from being passed where a resource locator is
 * expected, while retaining one canonical representation internally.
 */
class EVENGINE_API ResourceUri {
public:
    /**
     * @brief Parse a supported resource URI.
     * @param text Strict virtual URI; a bare filesystem path is rejected.
     * @return A checked resource URI or a parse diagnostic.
     */
    [[nodiscard]] static Result<ResourceUri> parse(std::string_view text);

    /**
     * @brief Construct a resource URI from validated components.
     * @param scheme Supported resource namespace.
     * @param path Relative virtual path.
     * @param query Optional query component without `?`.
     * @param fragment Optional fragment component without `#`.
     * @return A checked resource URI or a parse diagnostic.
     */
    [[nodiscard]] static Result<ResourceUri> fromParts(UriScheme scheme, std::string_view path,
                                                       std::string_view query = {}, std::string_view fragment = {});

    /**
     * @brief Explicitly convert a legacy project-relative path.
     * @param path The old string facade's virtual path, not a URI.
     * @return `project://path` after legacy separator normalization, or a
     *         diagnostic. Inputs containing `://` are never reinterpreted.
     */
    [[nodiscard]] static Result<ResourceUri> fromLegacyProjectPath(std::string_view path);

    /** @brief Return the resource namespace. */
    [[nodiscard]] UriScheme scheme() const noexcept { return uri_.scheme(); }

    /** @brief Return the relative virtual path, borrowed from this value. */
    [[nodiscard]] std::string_view path() const noexcept { return uri_.path(); }

    /** @brief Return the query component, borrowed from this value. */
    [[nodiscard]] std::string_view query() const noexcept { return uri_.query(); }

    /** @brief Return the fragment component, borrowed from this value. */
    [[nodiscard]] std::string_view fragment() const noexcept { return uri_.fragment(); }

    /** @brief Return the stable canonical URI text owned by this value. */
    [[nodiscard]] const std::string& format() const noexcept { return uri_.format(); }

    /** @brief Return the structural URI view owned by this resource URI. */
    [[nodiscard]] const Uri& uri() const noexcept { return uri_; }

    /** @brief Compare resource URI values. */
    friend bool operator==(const ResourceUri&, const ResourceUri&) noexcept = default;

private:
    explicit ResourceUri(Uri uri) : uri_(std::move(uri)) {}

    Uri uri_;
};

}  // namespace eve

namespace std {

template <>
struct hash<eve::Uri> {
    /** @brief Hash a canonical URI for standard hash containers. */
    std::size_t operator()(const eve::Uri& value) const noexcept {
        return std::hash<std::string_view>{}(value.format());
    }
};

template <>
struct hash<eve::ResourceUri> {
    /** @brief Hash a canonical resource URI for standard hash containers. */
    std::size_t operator()(const eve::ResourceUri& value) const noexcept {
        return std::hash<std::string_view>{}(value.format());
    }
};

}  // namespace std
