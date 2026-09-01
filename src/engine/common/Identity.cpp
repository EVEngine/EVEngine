#include "common/Identity.h"

#include <utility>

namespace eve {

namespace {

[[nodiscard]] bool isLogicalName(std::string_view value) noexcept {
    if (value.empty()) return false;
    for (const char character : value) {
        const bool alpha = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        if (!(alpha || digit || character == '_' || character == '-' || character == '.')) {
            return false;
        }
    }
    const char first = value.front();
    return (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || (first >= '0' && first <= '9');
}

[[nodiscard]] bool isLogicalNamespace(std::string_view value) noexcept {
    if (value.empty()) return false;
    for (const char character : value) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!(lower || digit || character == '_' || character == '-' || character == '.')) {
            return false;
        }
    }
    return value.front() >= 'a' && value.front() <= 'z';
}

}  // namespace

std::optional<LogicalId> LogicalId::parse(std::string_view text) {
    const auto separator = text.find(':');
    if (separator == std::string_view::npos || text.find(':', separator + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const auto namespaceName = text.substr(0, separator);
    const auto name          = text.substr(separator + 1);
    if (!isLogicalNamespace(namespaceName) || !isLogicalName(name)) return std::nullopt;
    return LogicalId(std::string(text));
}

std::optional<LogicalId> LogicalId::fromParts(std::string_view namespaceName, std::string_view name) {
    if (!isLogicalNamespace(namespaceName) || !isLogicalName(name)) return std::nullopt;
    std::string value;
    value.reserve(namespaceName.size() + name.size() + 1);
    value.append(namespaceName);
    value.push_back(':');
    value.append(name);
    return LogicalId(std::move(value));
}

std::string_view LogicalId::namespaceName() const noexcept {
    const auto separator = value_.find(':');
    return separator == std::string::npos ? std::string_view{} : std::string_view(value_).substr(0, separator);
}

std::string_view LogicalId::name() const noexcept {
    const auto separator = value_.find(':');
    return separator == std::string::npos ? std::string_view{} : std::string_view(value_).substr(separator + 1);
}

std::uint64_t LogicalId::hash() const noexcept {
    std::uint64_t result = 14695981039346656037ull;
    for (const auto character : value_) {
        result ^= static_cast<std::uint8_t>(character);
        result *= 1099511628211ull;
    }
    return result;
}

UuidV7Generator::UuidV7Generator(UuidEntropySource entropy, UuidClock clock)
    : entropy_(std::move(entropy)), clock_(std::move(clock)) {}

std::optional<PersistentId> UuidV7Generator::generate() const {
    const auto timestamp = clock_ ? clock_() : std::chrono::system_clock::now();
    return generate(timestamp);
}

std::optional<PersistentId> UuidV7Generator::generate(std::chrono::system_clock::time_point timestamp) const {
    return generateUuidV7(timestamp, entropy_);
}

std::optional<PersistentId> generateUuidV7(std::chrono::system_clock::time_point timestamp,
                                           const UuidEntropySource&              entropy) {
    if (!entropy) return std::nullopt;

    if (timestamp.time_since_epoch() < std::chrono::system_clock::duration::zero()) return std::nullopt;
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    constexpr std::uint64_t maxTimestamp = (std::uint64_t{1} << 48u) - 1u;
    if (milliseconds < 0 || static_cast<std::uint64_t>(milliseconds) > maxTimestamp) {
        return std::nullopt;
    }

    PersistentId::Bytes bytes{};
    if (!entropy(std::span<std::uint8_t>(bytes).subspan(6))) return std::nullopt;

    auto timestampValue = static_cast<std::uint64_t>(milliseconds);
    for (std::size_t index = 6; index > 0; --index) {
        bytes[index - 1] = static_cast<std::uint8_t>(timestampValue & 0xffu);
        timestampValue >>= 8u;
    }

    bytes[6] = static_cast<std::uint8_t>(0x70u | (bytes[6] & 0x0fu));
    bytes[8] = static_cast<std::uint8_t>(0x80u | (bytes[8] & 0x3fu));
    return PersistentId(bytes);
}

}  // namespace eve
