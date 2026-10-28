#include "animation/AnimClipBinary.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimParallelInternal.h"
#include "animation/AnimSkeleton.h"
#include "common/Utf8Validation.h"

namespace eve::animation {
class AnimClipBinaryAccess {
public:
    static void resize(AnimClip& clip, int bone, int kind, std::size_t count) {
        clip.ensureBone(bone);
        auto& track = clip.tracks_[bone];
        if (kind == 0)
            track.positions.resize(count);
        else if (kind == 1)
            track.rotations.resize(count);
        else
            track.scales.resize(count);
    }
    static void write(AnimClip& clip, int bone, int kind, std::size_t key, float time, float x, float y, float z,
                      float w) {
        auto& track = clip.tracks_[bone];
        if (kind == 0)
            track.positions[key] = {time, x, y, z};
        else if (kind == 2)
            track.scales[key] = {time, x, y, z};
        else {
            TransformTRS rotation;
            rotation.qx = x;
            rotation.qy = y;
            rotation.qz = z;
            rotation.qw = w;
            rotation.normalizeRotation();
            track.rotations[key] = {time, rotation.qx, rotation.qy, rotation.qz, rotation.qw};
        }
    }
};
namespace {
class Reader {
public:
    explicit Reader(std::span<const std::byte> input) : data(input) {}
    std::uint32_t integer() {
        require(4);
        std::uint32_t value = 0;
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(&value, data.data() + offset, 4);
            offset += 4;
        } else {
            for (int i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(data[offset++]) << (i * 8);
        }
        return value;
    }
    float number() {
        const float value = std::bit_cast<float>(integer());
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite track value");
        return value;
    }
    std::string string() {
        const auto length = integer();
        if (length == 0 || length > 4096) throw std::runtime_error("invalid track name length");
        require(length);
        std::string value(reinterpret_cast<const char*>(data.data() + offset), length);
        offset += length;
        if (!eve::isValidUtf8(value, eve::Utf8NullPolicy::Reject)) throw std::runtime_error("invalid UTF-8 name");
        return value;
    }
    void require(std::size_t count) const {
        if (count > data.size() - offset) throw std::runtime_error("truncated animation tracks");
    }
    bool finished() const { return offset == data.size(); }

private:
    std::span<const std::byte> data;
    std::size_t                offset = 0;
};
}  // namespace

eve::Result<int> loadAnimationTrackBatch(std::span<const AnimationTrackInput> inputs, const AnimSkeleton& skeleton,
                                         int workerCount) {
    try {
        if (workerCount < 0 || workerCount > 8 || inputs.size() > 64)
            throw std::runtime_error("invalid animation batch size or worker count");
        std::size_t total = 0;
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            if (inputs[i].bytes.size() > 256 * 1024 * 1024 - total)
                throw std::runtime_error("animation batch exceeds 256 MiB limit");
            total += inputs[i].bytes.size();
            for (std::size_t j = 0; j < i; ++j)
                if (&inputs[i].destination.get() == &inputs[j].destination.get())
                    throw std::runtime_error("duplicate animation batch destination");
        }
        std::vector<AnimClip> candidates(inputs.size());
        const int             workers = detail::parallelAnimationItems(inputs.size(), workerCount, [&](std::size_t i) {
            auto loaded = loadAnimationTracks(candidates[i], inputs[i].bytes, skeleton);
            if (!loaded.ok()) throw std::runtime_error(loaded.status().describe());
        });
        for (std::size_t i = 0; i < inputs.size(); ++i) inputs[i].destination.get().adopt(candidates[i]);
        return eve::Result<int>::success(workers, eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& error) {
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(),
                                                                "animation-track-batch", {}, "animation"));
    }
}

eve::Result<void> loadAnimationTracks(AnimClip& destination, std::span<const std::byte> bytes,
                                      const AnimSkeleton& skeleton) {
    try {
        if (bytes.size() > 128 * 1024 * 1024) throw std::runtime_error("animation track file exceeds 128 MiB limit");
        Reader reader(bytes);
        if (reader.integer() != 0x43415645 || reader.integer() != 1)
            throw std::runtime_error("unsupported animation track schema");
        const auto count = reader.integer();
        if (count > static_cast<unsigned>(skeleton.getBoneCount())) throw std::runtime_error("too many bone tracks");
        const float duration = reader.number(), rate = reader.number();
        const auto  flags = reader.integer();
        if (duration < 0 || rate <= 0 || flags > 1) throw std::runtime_error("invalid animation settings");
        AnimClip candidate(reader.string());
        candidate.setDuration(duration);
        candidate.setSampleRate(rate);
        candidate.setLoop(flags != 0);
        std::vector<bool> seen(skeleton.getBoneCount(), false);
        for (unsigned track = 0; track < count; ++track) {
            const int bone = skeleton.findBone(reader.string());
            if (bone < 0 || seen[bone]) throw std::runtime_error("unknown or duplicate bone track");
            seen[bone] = true;
            for (int kind = 0; kind < 3; ++kind) {
                const auto keys       = reader.integer();
                const int  components = kind == 1 ? 4 : 3;
                if (keys > 1000000) throw std::runtime_error("too many animation keys");
                reader.require(static_cast<std::size_t>(keys) * (components + 1) * 4);
                AnimClipBinaryAccess::resize(candidate, bone, kind, keys);
                float previous = -1.f;
                for (unsigned key = 0; key < keys; ++key) {
                    const float time = reader.number();
                    if (time < 0 || time > duration || time <= previous)
                        throw std::runtime_error("invalid key time order");
                    previous      = time;
                    const float x = reader.number(), y = reader.number(), z = reader.number();
                    float       w = 1.f;
                    if (kind == 1) {
                        w                = reader.number();
                        const float norm = x * x + y * y + z * z + w * w;
                        if (!std::isfinite(norm) || norm < 1e-12f) throw std::runtime_error("invalid key quaternion");
                    }
                    AnimClipBinaryAccess::write(candidate, bone, kind, key, time, x, y, z, w);
                }
            }
        }
        if (!reader.finished()) throw std::runtime_error("unknown trailing animation data");
        destination.adopt(candidate);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& error) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(),
                                                                 "animation-tracks", {}, "animation"));
    }
}
}  // namespace eve::animation
