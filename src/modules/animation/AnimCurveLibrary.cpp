#include "animation/AnimCurveLibrary.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/Utf8Validation.h"

namespace eve::animation {
namespace {
struct Key {
    float time, value;
};
struct Clip {
    float                                                duration;
    std::map<std::string, std::vector<Key>, std::less<>> channels;
};
class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : bytes(data) {}
    std::uint32_t integer() {
        require(4);
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result |= std::to_integer<std::uint32_t>(bytes[offset++]) << (8 * i);
        return result;
    }
    float number() {
        float value = std::bit_cast<float>(integer());
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite animation curve value");
        return value;
    }
    std::string string() {
        auto count = integer();
        if (count == 0 || count > 4096) throw std::runtime_error("invalid animation curve name length");
        require(count);
        std::string value(reinterpret_cast<const char*>(bytes.data() + offset), count);
        offset += count;
        if (!eve::isValidUtf8(value, eve::Utf8NullPolicy::Reject))
            throw std::runtime_error("invalid animation curve UTF-8");
        return value;
    }
    void require(std::size_t count) const {
        if (count > bytes.size() - offset) throw std::runtime_error("truncated animation curves");
    }
    bool finished() const { return offset == bytes.size(); }

private:
    std::span<const std::byte> bytes;
    std::size_t                offset = 0;
};
eve::Diagnostic invalid(std::string message) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), "animationCurves", {},
                                  "animation");
}
}  // namespace
struct AnimCurveLibrary::Impl {
    std::map<std::string, Clip, std::less<>> clips;
};
AnimCurveLibrary::AnimCurveLibrary() : impl_(std::make_unique<Impl>()) {}
AnimCurveLibrary::~AnimCurveLibrary() = default;
eve::Result<void> AnimCurveLibrary::load(std::span<const std::byte> bytes) {
    try {
        if (bytes.size() > 64 * 1024 * 1024) throw std::runtime_error("animation curves exceed 64 MiB");
        Reader reader(bytes);
        if (reader.integer() != 0x43465645 || reader.integer() != 1)
            throw std::runtime_error("unsupported animation curves format");
        auto count = reader.integer();
        if (count > 100000) throw std::runtime_error("too many animation curve sources");
        Impl next;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto source = reader.string();
            Clip clip;
            clip.duration     = reader.number();
            auto channelCount = reader.integer();
            if (channelCount > 64 || clip.duration < 0 || (channelCount && clip.duration <= 0))
                throw std::runtime_error("invalid animation curve duration or channel count");
            for (std::uint32_t j = 0; j < channelCount; ++j) {
                auto name     = reader.string();
                auto keyCount = reader.integer();
                if (keyCount < 2 || keyCount > 1000000) throw std::runtime_error("invalid animation curve key count");
                reader.require(std::size_t(keyCount) * 8);
                std::vector<Key> keys;
                keys.reserve(keyCount);
                for (std::uint32_t k = 0; k < keyCount; ++k) {
                    Key key{reader.number(), reader.number()};
                    if (key.time < 0 || key.time > clip.duration || (!keys.empty() && key.time <= keys.back().time))
                        throw std::runtime_error("unordered animation curve time");
                    keys.push_back(key);
                }
                if (keys.front().time != 0 || keys.back().time != clip.duration)
                    throw std::runtime_error("animation curve endpoints do not span duration");
                if (!clip.channels.emplace(name, std::move(keys)).second)
                    throw std::runtime_error("duplicate animation curve channel");
            }
            if (!next.clips.emplace(source, std::move(clip)).second)
                throw std::runtime_error("duplicate animation curve source");
        }
        if (!reader.finished()) throw std::runtime_error("trailing animation curve bytes");
        impl_->clips.swap(next.clips);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& error) {
        return eve::Result<void>::failure(invalid(error.what()));
    }
}
bool        AnimCurveLibrary::contains(std::string_view source) const { return impl_->clips.contains(source); }
std::size_t AnimCurveLibrary::size() const noexcept { return impl_->clips.size(); }
eve::Result<std::optional<float>> AnimCurveLibrary::sample(std::string_view source, std::string_view channel,
                                                           double seconds, bool loop) const {
    using Result = eve::Result<std::optional<float>>;
    if (!std::isfinite(seconds)) return Result::failure(invalid("nonfinite animation curve time"));
    auto found = impl_->clips.find(source);
    if (found == impl_->clips.end())
        return Result::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "animation curve source not found",
                                                      "source", {}, "animation"));
    const auto& clip  = found->second;
    auto        curve = clip.channels.find(channel);
    if (curve == clip.channels.end()) return Result::success(std::optional<float>{});
    if (loop) {
        seconds = std::fmod(seconds, double(clip.duration));
        if (seconds < 0) seconds += clip.duration;
    }
    const auto& keys = curve->second;
    if (seconds <= 0) return Result::success(keys.front().value);
    if (seconds >= clip.duration) return Result::success(keys.back().value);
    auto        end   = std::upper_bound(keys.begin(), keys.end(), seconds,
                                         [](double time, const Key& key) { return time < key.time; });
    const auto& a     = *(end - 1);
    const auto& b     = *end;
    double      alpha = (seconds - a.time) / (double(b.time) - a.time);
    return Result::success(float(double(a.value) * (1 - alpha) + double(b.value) * alpha));
}
}  // namespace eve::animation
