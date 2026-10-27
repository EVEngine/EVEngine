#include "physics/backend/bake/BakeRecipe.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include "physics/backend/bake/BakedAssetFile.h"

namespace eve::physics {
namespace {
Diagnostic invalidRecipe(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "bakeRecipe");
}
constexpr std::size_t MaxEntries = 64, MaxToken = 256;
bool                  token(std::string_view value) {
    if (value.empty() || value.size() > MaxToken) return false;
    for (unsigned char c : value)
        if (c < 33 || c > 126) return false;
    return true;
}
bool digest(std::string_view value) {
    if (value.size() != 32) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
void append32(std::string& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((value >> shift) & 255));
}
void appendText(std::string& out, std::string_view text) {
    append32(out, static_cast<std::uint32_t>(text.size()));
    out.append(text);
}
struct Reader {
    std::string_view bytes;
    std::size_t      cursor = 0;
    bool             valid  = true;
    std::string_view take(std::size_t size) {
        if (size > bytes.size() - cursor) {
            valid = false;
            return {};
        }
        auto result = bytes.substr(cursor, size);
        cursor += size;
        return result;
    }
    std::uint32_t word() {
        auto data = take(4);
        if (!valid) return 0;
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(static_cast<unsigned char>(data[i])) << (8 * i);
        return result;
    }
    std::string text() {
        const auto size = word();
        if (size > MaxToken) {
            valid = false;
            return {};
        }
        return std::string(take(size));
    }
};
}  // namespace
Result<void> BakeRecipe::setCoordinates(BakeCoordinates coordinates) {
    if (coordinates != BakeCoordinates::MetresRightHandedYUp && coordinates != BakeCoordinates::PlanarPixels)
        return Result<void>::failure(invalidRecipe("unknown bake coordinates"));
    coordinates_ = coordinates;
    return Result<void>::success();
}
BakeCoordinates BakeRecipe::coordinates() const noexcept { return coordinates_; }
Result<void>    BakeRecipe::setProducer(std::string_view revision) {
    if (!token(revision)) return Result<void>::failure(invalidRecipe("invalid producer revision token"));
    producer_ = revision;
    return Result<void>::success();
}
Result<void> BakeRecipe::setBackend(std::string_view revision) {
    if (!token(revision)) return Result<void>::failure(invalidRecipe("invalid backend revision token"));
    backend_ = revision;
    return Result<void>::success();
}
Result<void> BakeRecipe::setTiming(float stepSeconds, std::uint32_t sampleStride) {
    if (!std::isfinite(stepSeconds) || stepSeconds <= 0 || sampleStride == 0)
        return Result<void>::failure(invalidRecipe("invalid bake timing"));
    stepSeconds_  = stepSeconds;
    sampleStride_ = sampleStride;
    return Result<void>::success();
}
Result<void> BakeRecipe::setParameters(std::string_view bytes) {
    parameters_ = bakedAssetContentKey(bytes);
    return Result<void>::success();
}
Result<void> BakeRecipe::setSource(std::string_view name, std::string_view bytes) {
    if (!token(name) || (sources_.size() >= MaxEntries && !sources_.contains(std::string(name))))
        return Result<void>::failure(invalidRecipe("invalid source name or source budget exceeded"));
    auto key = bakedAssetContentKey(bytes);
    sources_.insert_or_assign(std::string(name), std::move(key));
    return Result<void>::success();
}
Result<void> BakeRecipe::setRandomStream(std::string_view name, std::uint64_t seed) {
    if (!token(name) || (randomStreams_.size() >= MaxEntries && !randomStreams_.contains(std::string(name))))
        return Result<void>::failure(invalidRecipe("invalid RNG name or stream budget exceeded"));
    randomStreams_.insert_or_assign(std::string(name), seed);
    return Result<void>::success();
}
Result<std::string> BakeRecipe::encode() const {
    if (!token(producer_) || !token(backend_) || !digest(parameters_) || !std::isfinite(stepSeconds_) ||
        stepSeconds_ <= 0 || sampleStride_ == 0 || sources_.empty())
        return Result<std::string>::failure(invalidRecipe("incomplete bake provenance"));
    const bool  planar = coordinates_ == BakeCoordinates::PlanarPixels;
    std::string out    = planar ? "EVEBREC1" : "EVECREC1";
    appendText(out, planar ? "planar-pixels-xy" : "metres-rh-y-up");
    appendText(out, producer_);
    appendText(out, backend_);
    out += parameters_;
    append32(out, std::bit_cast<std::uint32_t>(stepSeconds_));
    append32(out, sampleStride_);
    append32(out, static_cast<std::uint32_t>(sources_.size()));
    for (const auto& [name, key] : sources_) {
        appendText(out, name);
        out += key;
    }
    append32(out, static_cast<std::uint32_t>(randomStreams_.size()));
    for (const auto& [name, seed] : randomStreams_) {
        appendText(out, name);
        append32(out, static_cast<std::uint32_t>(seed));
        append32(out, static_cast<std::uint32_t>(seed >> 32));
    }
    return Result<std::string>::success(std::move(out));
}
Result<BakeRecipe> BakeRecipe::decode(std::string_view bytes) {
    auto failure = [] { return Result<BakeRecipe>::failure(invalidRecipe("invalid bake provenance schema or data")); };
    if (bytes.size() > 65536) return failure();
    Reader     reader{bytes};
    const auto schema      = reader.take(8);
    const auto coordinates = reader.text();
    BakeRecipe recipe;
    if (schema == "EVECREC1" && coordinates == "metres-rh-y-up")
        recipe.coordinates_ = BakeCoordinates::MetresRightHandedYUp;
    else if (schema == "EVEBREC1" && coordinates == "planar-pixels-xy")
        recipe.coordinates_ = BakeCoordinates::PlanarPixels;
    else
        return failure();
    recipe.producer_     = reader.text();
    recipe.backend_      = reader.text();
    recipe.parameters_   = reader.take(32);
    recipe.stepSeconds_  = std::bit_cast<float>(reader.word());
    recipe.sampleStride_ = reader.word();
    const auto sources   = reader.word();
    if (sources == 0 || sources > MaxEntries) return failure();
    for (std::uint32_t i = 0; i < sources; ++i) {
        auto name = reader.text();
        auto key  = reader.take(32);
        if (!reader.valid || !token(name) || !digest(key) || !recipe.sources_.emplace(name, key).second)
            return failure();
    }
    const auto streams = reader.word();
    if (streams > MaxEntries) return failure();
    for (std::uint32_t i = 0; i < streams; ++i) {
        auto       name = reader.text();
        const auto low  = reader.word();
        const auto high = reader.word();
        if (!reader.valid || !token(name) ||
            !recipe.randomStreams_.emplace(name, std::uint64_t(low) | (std::uint64_t(high) << 32)).second)
            return failure();
    }
    if (!reader.valid || reader.cursor != bytes.size()) return failure();
    auto canonical = recipe.encode();
    if (!canonical || canonical.value() != bytes) return failure();
    return Result<BakeRecipe>::success(std::move(recipe));
}
Result<void> BakeRecipe::validateSampleTime(std::uint32_t frame, float seconds) const {
    const double expected = double(frame) * stepSeconds_ * sampleStride_;
    const double tolerance =
        std::min(4 * std::numeric_limits<float>::epsilon() * std::max(expected, double(stepSeconds_)),
                 0.25 * double(stepSeconds_) * sampleStride_);
    if (!std::isfinite(seconds) || stepSeconds_ <= 0 || sampleStride_ == 0 ||
        expected > std::numeric_limits<float>::max() ||
        (frame == 0 ? seconds != 0 : std::abs(double(seconds) - expected) > tolerance))
        return Result<void>::failure(invalidRecipe("sample time does not match bake timing"));
    return Result<void>::success();
}
Result<std::string> BakeRecipe::key() const {
    auto bytes = encode();
    if (!bytes) return Result<std::string>::failure(bytes.status());
    return Result<std::string>::success(bakedAssetContentKey(bytes.value()));
}
}  // namespace eve::physics
