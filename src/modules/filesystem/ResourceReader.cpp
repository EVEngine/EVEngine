#include "filesystem/ResourceReader.h"

#include <string>
#include <utility>

namespace eve::filesystem {
namespace {

[[nodiscard]] Result<std::vector<std::uint8_t>> unsupported(const ResourceUri& uri) {
    return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported,
        "filesystem ResourceReader has no provider for this resource namespace", uri.format()));
}

[[nodiscard]] Result<std::vector<std::uint8_t>> readFailure(const ResourceUri& uri) {
    return Result<std::vector<std::uint8_t>>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "filesystem provider could not read resource",
                          uri.format()));
}

template <typename T>
[[nodiscard]] Result<T> propagate(const Status& status) {
    return Result<T>::failure(status);
}

}  // namespace

Result<std::vector<std::uint8_t>> ResourceReader::read(const ResourceUri& uri) const {
    if (uri.scheme() != UriScheme::Project) return unsupported(uri);

    std::vector<std::uint8_t> bytes;
    if (!filesystem_.readFile(std::string(uri.path()), bytes)) return readFailure(uri);
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<std::vector<std::uint8_t>> ResourceReader::read(std::string_view legacyPathOrUri) const {
    if (legacyPathOrUri.find("://") != std::string_view::npos) {
        auto parsed = ResourceUri::parse(legacyPathOrUri);
        if (!parsed.ok()) return propagate<std::vector<std::uint8_t>>(parsed.status());
        return read(std::move(parsed).takeValue());
    }

    auto converted = ResourceUri::fromLegacyProjectPath(legacyPathOrUri);
    if (!converted.ok()) return propagate<std::vector<std::uint8_t>>(converted.status());
    return read(std::move(converted).takeValue());
}

}  // namespace eve::filesystem
