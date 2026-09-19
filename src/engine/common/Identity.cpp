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

// ---- Tag-free UUID core ---------------------------------------------------
//
// One copy of each algorithm, shared by every eve::detail::Id128<Tag>
// instantiation. The tagged methods are inline one-line forwards, so adding a
// domain tag no longer duplicates the parser, the formatter or the derivation.

namespace detail {

namespace {

[[nodiscard]] constexpr std::optional<std::uint8_t> hexValue(char value) noexcept {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    return std::nullopt;
}

}  // namespace

std::optional<UuidBytes> parseUuidText(std::string_view text) noexcept {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
        return std::nullopt;
    }

    UuidBytes   bytes{};
    std::size_t byteIndex = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '-') {
            ++i;
            continue;
        }

        if (i + 1 >= text.size()) return std::nullopt;
        const auto high = hexValue(text[i]);
        const auto low  = hexValue(text[i + 1]);
        if (!high || !low || byteIndex >= bytes.size()) {
            return std::nullopt;
        }
        bytes[byteIndex++] = static_cast<std::uint8_t>((*high << 4u) | *low);
        i += 2;
    }
    if (byteIndex != bytes.size()) return std::nullopt;
    return bytes;
}

std::string formatUuidBytes(const UuidBytes& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string           result;
    result.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
        result.push_back(digits[(bytes[i] >> 4u) & 0x0fu]);
        result.push_back(digits[bytes[i] & 0x0fu]);
    }
    return result;
}

UuidBytes childUuidBytes(const UuidBytes& parent, std::string_view role) noexcept {
    UuidBytes     result = parent;
    std::uint64_t hash   = 14695981039346656037ull;
    for (const unsigned char byte : role) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    for (std::size_t i = 0; i < sizeof(hash); ++i) {
        result[8 + i] ^= static_cast<std::uint8_t>((hash >> (i * 8u)) & 0xffu);
    }
    // Preserve the UUID variant while retaining the parent's namespace.
    result[8] = static_cast<std::uint8_t>((result[8] & 0x3fu) | 0x80u);
    return result;
}

}  // namespace detail

}  // namespace eve
