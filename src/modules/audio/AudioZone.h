#pragma once

#include "common/Result.h"

#include <cstdint>
#include <vector>

namespace eve::audio {
class Source;

/** @brief One Pcg audio-zone track timing and gain profile. */
struct AudioZoneItem { float volume=1.f, fadeInTime=5.f, fadeOutTime=5.f, duration=0.f; };
enum class AudioZonePhase : std::uint8_t { Active, BecomingInactive, Inactive };
/** @brief Caller-owned Pcg audio-zone configuration and track list. */
class AudioZoneProfile {
public:
    float x=0.f,y=0.f,z=0.f,radius=30.f,minimumBreakTime=5.f,maximumBreakTime=10.f,deactivationTime=10.f;
    bool global=false;
    /** @brief Add one validated track profile. */
    [[nodiscard]] Result<int> addItem(const AudioZoneItem& item);
    int itemCount() const noexcept { return static_cast<int>(items_.size()); }
    /**
     * @brief Borrow track data.
     * @return Non-owning pointer, or null for an invalid index.
     * @ownership This profile retains ownership.
     * @lifetime Valid until this profile is destroyed or its next addItem call.
     */
    const AudioZoneItem* itemAt(int index) const noexcept;
private:
    std::vector<AudioZoneItem> items_;
};
/** @brief Persistent playback state owned by the caller. */
struct AudioZoneState {
    AudioZonePhase phase=AudioZonePhase::Inactive;
    int selectedTrack=-1;
    bool playing=false;
    float trackStarted=0.f,fadeInEnds=0.f,fadeOutBegins=0.f,fadeOutEnds=0.f,nextTrackStarts=0.f,deactivateAt=0.f;
};
/** @brief Commands to apply to a caller-owned audio Source. */
struct AudioZoneOutput { bool play=false,stop=false; int trackIndex=-1; float volume=0.f; };
/**
 * @brief Evaluate PcgAudioManager/PcgAudioZone for one explicit time and player observation.
 * @param profile Immutable zone configuration.
 * @param state Caller-owned playback state, atomically updated on success.
 * @param now Absolute simulation time in seconds.
 * @param playerX Player world position.
 * @param playerY Player world position.
 * @param playerZ Player world position.
 * @param masterVolume Pcg master-volume ceiling.
 * @param seed Named audio-zone RNG stream seed used when a new track starts.
 */
[[nodiscard]] Result<AudioZoneOutput> evaluateAudioZone(const AudioZoneProfile& profile, AudioZoneState& state,
    float now,float playerX,float playerY,float playerZ,float masterVolume,std::uint32_t seed);
/** @brief Apply one evaluated command to the caller-selected track Source without retaining it. */
[[nodiscard]] Result<void> applyAudioZoneOutput(Source* source,const AudioZoneOutput& output);
}
