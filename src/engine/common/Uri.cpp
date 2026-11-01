#include "common/Uri.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace eve {
namespace {

[[nodiscard]] Result<Uri> uriFailure(std::string message, std::string_view path = {}) {
    return Result<Uri>::failure(Diagnostic::error(DiagnosticCode::ParseError, std::move(message), std::string(path)));
}

[[nodiscard]] Result<ResourceUri> resourceUriFailure(std::string message, std::string_view path = {}) {
    return Result<ResourceUri>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, std::move(message), std::string(path)));
}

[[nodiscard]] bool isSchemeCharacter(char value, bool first) noexcept {
    const bool alpha = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    const bool digit = value >= '0' && value <= '9';
    return first ? alpha : (alpha || digit || value == '+' || value == '-' || value == '.');
}

[[nodiscard]] bool isPathCharacter(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    if (byte < 0x21 || byte > 0x7e) return false;
    switch (value) {
        case '/':
        case '_':
        case '-':
        case '.':
        case '~':
        case '+':
        case '@': return true;
        default: return std::isalnum(byte) != 0;
    }
}

[[nodiscard]] bool isQueryOrFragmentCharacter(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    if (byte < 0x21 || byte > 0x7e) return false;
    return value != '?' && value != '#' && value != '%' && value != '\\';
}

[[nodiscard]] std::optional<UriScheme> schemeFromText(std::string_view scheme) noexcept {
    if (scheme == "asset") return UriScheme::Asset;
    if (scheme == "project") return UriScheme::Project;
    if (scheme == "builtin") return UriScheme::Builtin;
    if (scheme == "generated") return UriScheme::Generated;
    if (scheme == "memory") return UriScheme::Memory;
    return std::nullopt;
}

[[nodiscard]] bool isKnownScheme(UriScheme scheme) noexcept {
    switch (scheme) {
        case UriScheme::Asset:
        case UriScheme::Project:
        case UriScheme::Builtin:
        case UriScheme::Generated:
        case UriScheme::Memory: return true;
    }
    return false;
}

[[nodiscard]] bool validPath(std::string_view path) noexcept {
    if (path.empty() || path.front() == '/' || path.back() == '/') return false;
    char        previous     = '\0';
    std::size_t segmentStart = 0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char value = path[i];
        if (!isPathCharacter(value) || (value == '/' && previous == '/')) return false;
        if (value == '/') {
            const auto segment = path.substr(segmentStart, i - segmentStart);
            if (segment == "." || segment == ".." || segment.empty()) return false;
            segmentStart = i + 1;
        }
        previous = value;
    }
    const auto lastSegment = path.substr(segmentStart);
    return lastSegment != "." && lastSegment != ".." && !lastSegment.empty();
}

[[nodiscard]] bool validQueryOrFragment(std::string_view value) noexcept {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), isQueryOrFragmentCharacter);
}

[[nodiscard]] Result<Uri> parseUri(std::string_view text) {
    const auto delimiter = text.find("://");
    if (delimiter == std::string_view::npos || delimiter == 0) {
        return uriFailure("resource URI must use scheme://path and cannot be a bare path", text);
    }
    for (std::size_t i = 0; i < delimiter; ++i) {
        if (!isSchemeCharacter(text[i], i == 0)) {
            return uriFailure("resource URI contains an invalid scheme", text);
        }
    }

    std::string scheme(text.substr(0, delimiter));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    const auto schemeKind = schemeFromText(scheme);
    if (!schemeKind) return uriFailure("unsupported resource URI scheme", text);

    std::string_view remainder         = text.substr(delimiter + 3);
    const auto       fragmentDelimiter = remainder.find('#');
    std::string_view fragment;
    if (fragmentDelimiter != std::string_view::npos) {
        fragment  = remainder.substr(fragmentDelimiter + 1);
        remainder = remainder.substr(0, fragmentDelimiter);
        if (!validQueryOrFragment(fragment)) return uriFailure("URI fragment is empty or invalid", text);
    }

    const auto       queryDelimiter = remainder.find('?');
    std::string_view query;
    if (queryDelimiter != std::string_view::npos) {
        query     = remainder.substr(queryDelimiter + 1);
        remainder = remainder.substr(0, queryDelimiter);
        if (!validQueryOrFragment(query)) return uriFailure("URI query is empty or invalid", text);
    }
    if (!validPath(remainder)) return uriFailure("URI path is empty, absolute, or invalid", text);

    return Uri::fromParts(*schemeKind, remainder, query, fragment);
}

}  // namespace

const char* uriSchemeName(UriScheme scheme) noexcept {
    switch (scheme) {
        case UriScheme::Asset: return "asset";
        case UriScheme::Project: return "project";
        case UriScheme::Builtin: return "builtin";
        case UriScheme::Generated: return "generated";
        case UriScheme::Memory: return "memory";
    }
    return "unknown";
}

Uri::Uri(UriScheme scheme, std::string path, std::string query, std::string fragment)
    : scheme_(scheme), path_(std::move(path)), query_(std::move(query)), fragment_(std::move(fragment)) {
    formatted_.reserve(3 + path_.size() + query_.size() + fragment_.size() + 16);
    formatted_ += uriSchemeName(scheme_);
    formatted_ += "://";
    formatted_ += path_;
    if (!query_.empty()) {
        formatted_ += '?';
        formatted_ += query_;
    }
    if (!fragment_.empty()) {
        formatted_ += '#';
        formatted_ += fragment_;
    }
}

Result<Uri> Uri::parse(std::string_view text) { return parseUri(text); }

Result<Uri> Uri::fromParts(UriScheme scheme, std::string_view path, std::string_view query, std::string_view fragment) {
    if (!isKnownScheme(scheme)) return uriFailure("unknown resource URI scheme", path);
    if (!validPath(path)) return uriFailure("URI path is empty, absolute, or invalid", path);
    if ((!query.empty() && !validQueryOrFragment(query)) || (!fragment.empty() && !validQueryOrFragment(fragment))) {
        return uriFailure("URI query or fragment is invalid", path);
    }
    return Result<Uri>::success(Uri(scheme, std::string(path), std::string(query), std::string(fragment)));
}

Result<ResourceUri> ResourceUri::parse(std::string_view text) {
    auto parsed = Uri::parse(text);
    if (!parsed.ok()) {
        const auto* diagnostic = parsed.error();
        return resourceUriFailure(diagnostic ? diagnostic->message() : "invalid resource URI", text);
    }
    return Result<ResourceUri>::success(ResourceUri(std::move(parsed).takeValue()));
}

Result<ResourceUri> ResourceUri::fromParts(UriScheme scheme, std::string_view path, std::string_view query,
                                           std::string_view fragment) {
    auto parsed = Uri::fromParts(scheme, path, query, fragment);
    if (!parsed.ok()) {
        const auto* diagnostic = parsed.error();
        return resourceUriFailure(diagnostic ? diagnostic->message() : "invalid resource URI", path);
    }
    return Result<ResourceUri>::success(ResourceUri(std::move(parsed).takeValue()));
}

Result<ResourceUri> ResourceUri::fromLegacyProjectPath(std::string_view path) {
    if (path.find("://") != std::string_view::npos) {
        return resourceUriFailure("legacy path facade does not reinterpret URI-looking input", path);
    }
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
        normalized.erase(0, 2);
    }
    return fromParts(UriScheme::Project, normalized);
}

}  // namespace eve
