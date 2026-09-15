#include "audio/AudioZone.h"
#include "audio/Source.h"

#include <algorithm>
#include <cmath>

namespace eve::audio {
namespace {
template<class T> Result<T> fail(const char* message) { return Result<T>::failure(
    Diagnostic::error(DiagnosticCode::InvalidArgument,message,"audio.zone")); }
bool finiteItem(const AudioZoneItem& v) { return std::isfinite(v.volume)&&std::isfinite(v.fadeInTime)&&
    std::isfinite(v.fadeOutTime)&&std::isfinite(v.duration)&&v.volume>=0&&v.fadeInTime>=0&&v.fadeOutTime>=0&&v.duration>=0; }
float unit(std::uint32_t seed) { seed=seed*1664525u+1013904223u; return float(seed>>8u)*(1.f/16777216.f); }
}
Result<int> AudioZoneProfile::addItem(const AudioZoneItem& item) {
    if(!finiteItem(item)) return fail<int>("audio-zone track values must be finite and non-negative");
    items_.push_back(item); return Result<int>::success(itemCount()-1);
}
const AudioZoneItem* AudioZoneProfile::itemAt(int index) const noexcept {
    return index>=0&&index<itemCount()?&items_[static_cast<std::size_t>(index)]:nullptr;
}
Result<AudioZoneOutput> evaluateAudioZone(const AudioZoneProfile& p,AudioZoneState& state,float now,
    float px,float py,float pz,float master,std::uint32_t seed) {
    if(!std::isfinite(now)||!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(pz)||!std::isfinite(master)||
       !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.radius)||
       !std::isfinite(p.minimumBreakTime)||!std::isfinite(p.maximumBreakTime)||!std::isfinite(p.deactivationTime)||
       now<0||master<0||p.radius<0||p.minimumBreakTime<0||p.maximumBreakTime<p.minimumBreakTime||p.deactivationTime<0)
        return fail<AudioZoneOutput>("audio-zone inputs must be finite and ranges valid");
    AudioZoneState next=state; AudioZoneOutput out;
    const float dx=p.x-px,dy=p.y-py,dz=p.z-pz;
    const bool inRange=p.global||dx*dx+dy*dy+dz*dz<=p.radius*p.radius;
    if(inRange) {
        if(next.phase!=AudioZonePhase::Active) { next.phase=AudioZonePhase::Active; next.deactivateAt=0; }
    } else if(next.phase==AudioZonePhase::Active) {
        next.phase=AudioZonePhase::BecomingInactive; next.deactivateAt=now+p.deactivationTime;
    } else if(next.phase==AudioZonePhase::BecomingInactive && now>next.deactivateAt) {
        next.phase=AudioZonePhase::Inactive; next.playing=false; out.stop=true; next.selectedTrack=-1;
    }
    if(next.phase==AudioZonePhase::Active && p.itemCount()>0 && !next.playing && now>next.nextTrackStarts) {
        int chosen=std::min(p.itemCount()-1,int(unit(seed)*p.itemCount()));
        if(chosen==next.selectedTrack && p.itemCount()>1) chosen=(chosen+1)%p.itemCount();
        const auto* item=p.itemAt(chosen); next.selectedTrack=chosen; next.playing=true; next.trackStarted=now;
        next.fadeInEnds=now+item->fadeInTime; next.fadeOutBegins=now+item->duration-item->fadeOutTime;
        next.fadeOutEnds=now+item->duration; next.nextTrackStarts=next.fadeOutEnds+
            p.minimumBreakTime+(p.maximumBreakTime-p.minimumBreakTime)*unit(seed^0x9e3779b9u);
        out.play=true; out.trackIndex=chosen;
    }
    if(next.playing) {
        const auto* item=p.itemAt(next.selectedTrack); float volume=item?item->volume:0.f;
        if(now<next.fadeInEnds && next.fadeInEnds>next.trackStarted)
            volume=item->volume*(now-next.trackStarted)/(next.fadeInEnds-next.trackStarted);
        else if(now>=next.fadeOutBegins && now<next.fadeOutEnds && next.fadeOutEnds>next.fadeOutBegins)
            volume=item->volume*(next.fadeOutEnds-now)/(next.fadeOutEnds-next.fadeOutBegins);
        else if(now>=next.fadeOutEnds) { volume=0; next.playing=false; out.stop=true; }
        out.volume=std::min(volume,master);
    }
    state=next; return Result<AudioZoneOutput>::success(out);
}
Result<void> applyAudioZoneOutput(Source* source,const AudioZoneOutput& output) {
    if(!source||!std::isfinite(output.volume)||output.volume<0.f)
        return fail<void>("audio-zone output requires a source and finite non-negative volume");
    source->setVolume(output.volume);
    if(output.stop) source->stop();
    if(output.play) source->play();
    return Result<void>::success();
}
}  // namespace eve::audio
