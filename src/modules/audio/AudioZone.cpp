#include "audio/AudioZone.h"
#include "audio/Source.h"

#include <algorithm>
#include <cmath>

namespace eve::audio {
namespace {
bool finiteItem(const AudioZoneItem& v) {
    return std::isfinite(v.volume) && std::isfinite(v.fadeInTime) && std::isfinite(v.fadeOutTime) &&
           std::isfinite(v.duration) && v.volume >= 0 && v.fadeInTime >= 0 && v.fadeOutTime >= 0 && v.duration >= 0;
}
float unit(std::uint32_t seed) {
    seed = seed * 1664525u + 1013904223u;
    return float(seed >> 8u) * (1.f / 16777216.f);
}

float trackEnvelopeVolume(const AudioZoneItem* item, const AudioZoneState& state, float now) {
    if (!item) return 0.f;
    float volume = item->volume;
    if (now < state.fadeInEnds && state.fadeInEnds > state.trackStarted)
        volume = item->volume * (now - state.trackStarted) / (state.fadeInEnds - state.trackStarted);
    else if (now >= state.fadeOutBegins && now < state.fadeOutEnds && state.fadeOutEnds > state.fadeOutBegins)
        volume = item->volume * (state.fadeOutEnds - now) / (state.fadeOutEnds - state.fadeOutBegins);
    else if (now >= state.fadeOutEnds)
        volume = 0.f;
    return volume;
}
}  // namespace

Result<int> AudioZoneProfile::addItem(const AudioZoneItem& item) {
    if (!finiteItem(item))
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "audio-zone track values must be finite and non-negative", "audio.zone"));
    items_.push_back(item);
    return Result<int>::success(itemCount() - 1);
}
const AudioZoneItem* AudioZoneProfile::itemAt(int index) const noexcept {
    return index >= 0 && index < itemCount() ? &items_[static_cast<std::size_t>(index)] : nullptr;
}

Result<AudioZoneOutput> evaluateAudioZone(const AudioZoneProfile& p, AudioZoneState& state, float now, float px,
                                          float py, float pz, float master, std::uint32_t seed) {
    if (!std::isfinite(now) || !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
        !std::isfinite(master) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(p.radius) || !std::isfinite(p.minimumBreakTime) || !std::isfinite(p.maximumBreakTime) ||
        !std::isfinite(p.deactivationTime) || now < 0 || master < 0 || p.radius < 0 || p.minimumBreakTime < 0 ||
        p.maximumBreakTime < p.minimumBreakTime || p.deactivationTime < 0)
        return Result<AudioZoneOutput>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "audio-zone inputs must be finite and ranges valid", "audio.zone"));
    AudioZoneState  next = state;
    AudioZoneOutput out;
    const float     dx = p.x - px, dy = p.y - py, dz = p.z - pz;
    const bool      inRange = p.global || dx * dx + dy * dy + dz * dz <= p.radius * p.radius;
    if (inRange) {
        if (next.phase != AudioZonePhase::Active) {
            next.phase        = AudioZonePhase::Active;
            next.deactivateAt = 0;
            next.exitFadeFrom = 0.f;
        }
    } else if (next.phase == AudioZonePhase::Active) {
        const auto* item  = p.itemAt(next.selectedTrack);
        next.exitFadeFrom = next.playing ? std::min(trackEnvelopeVolume(item, next, now), master) : 0.f;
        next.phase        = AudioZonePhase::BecomingInactive;
        next.deactivateAt = now + p.deactivationTime;
    } else if (next.phase == AudioZonePhase::BecomingInactive && now >= next.deactivateAt) {
        next.phase         = AudioZonePhase::Inactive;
        next.playing       = false;
        out.stop           = true;
        out.volume         = 0.f;
        next.selectedTrack = -1;
        next.exitFadeFrom  = 0.f;
        state              = next;
        return Result<AudioZoneOutput>::success(out);
    }

    if (next.phase == AudioZonePhase::BecomingInactive) {
        const float windowStart = next.deactivateAt - p.deactivationTime;
        float       volume      = 0.f;
        if (p.deactivationTime <= 0.f || now >= next.deactivateAt) {
            volume = 0.f;
        } else if (now <= windowStart) {
            volume = next.exitFadeFrom;
        } else {
            volume = next.exitFadeFrom * (next.deactivateAt - now) / p.deactivationTime;
        }
        out.volume = std::max(0.f, volume);
        state      = next;
        return Result<AudioZoneOutput>::success(out);
    }

    if (next.phase == AudioZonePhase::Active && p.itemCount() > 0 && !next.playing && now > next.nextTrackStarts) {
        int chosen = std::min(p.itemCount() - 1, int(unit(seed) * p.itemCount()));
        if (chosen == next.selectedTrack && p.itemCount() > 1) chosen = (chosen + 1) % p.itemCount();
        const auto* item     = p.itemAt(chosen);
        next.selectedTrack   = chosen;
        next.playing         = true;
        next.trackStarted    = now;
        next.fadeInEnds      = now + item->fadeInTime;
        next.fadeOutBegins   = now + item->duration - item->fadeOutTime;
        next.fadeOutEnds     = now + item->duration;
        next.nextTrackStarts = next.fadeOutEnds + p.minimumBreakTime +
                               (p.maximumBreakTime - p.minimumBreakTime) * unit(seed ^ 0x9e3779b9u);
        out.play             = true;
        out.trackIndex       = chosen;
    }
    if (next.playing) {
        const auto* item   = p.itemAt(next.selectedTrack);
        float       volume = trackEnvelopeVolume(item, next, now);
        if (item && now >= next.fadeOutEnds) {
            next.playing = false;
            out.stop     = true;
            volume       = 0.f;
        }
        out.volume = std::min(volume, master);
    }
    state = next;
    return Result<AudioZoneOutput>::success(out);
}

Result<void> applyAudioZoneOutput(Source* source, const AudioZoneOutput& output) {
    if (!source || !std::isfinite(output.volume) || output.volume < 0.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "audio-zone output requires a source and finite non-negative volume", "audio.zone"));
    source->setVolume(output.volume);
    if (output.stop) source->stop();
    if (output.play) source->play();
    return Result<void>::success();
}
}  // namespace eve::audio
