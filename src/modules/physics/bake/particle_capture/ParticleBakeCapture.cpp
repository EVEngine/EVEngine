#include "physics/bake/particle_capture/ParticleBakeCapture.h"
#include <cmath>
#include <map>
#include <set>
#include "particles/ParticleEventJournal.h"
#include "physics/bake/BakeRecipe.h"
namespace eve::physics {
namespace {
Result<std::vector<ParticleTrajectory>> convertEvents(PersistentId session, float duration,
                                                      std::span<const particles::ParticleRecordEvent> events) {
    auto fail = [](const char* text) {
        return Result<std::vector<ParticleTrajectory>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, text, "particleBakeCapture"));
    };
    if (session.isNil() || !std::isfinite(duration) || duration < 0 || events.size() > 2000000)
        return fail("invalid session, duration or event budget");
    struct Birth {
        ParticleTrajectory trajectory;
        double             lastTime = 0;
        bool               dead     = false;
    };
    std::map<std::uint64_t, Birth> births;
    double                         previous = 0;
    for (const auto& event : events) {
        if (!std::isfinite(event.seconds) || event.seconds < previous || event.seconds > duration ||
            !event.birthSerial ||
            (event.kind != particles::ParticleRecordEventKind::Snapshot &&
             event.kind != particles::ParticleRecordEventKind::Death))
            return fail("invalid or out-of-order particle event");
        previous      = event.seconds;
        const auto& p = event.appearance;
        for (float v :
             {p.x, p.y, p.w, p.h, p.rotation, p.color.r, p.color.g, p.color.b, p.color.a, p.u0, p.v0, p.u1, p.v1})
            if (!std::isfinite(v)) return fail("nonfinite recorded appearance");
        auto found = births.find(event.birthSerial);
        if (found == births.end()) {
            if (event.kind != particles::ParticleRecordEventKind::Snapshot || births.size() == 65536)
                return fail("death without birth or birth budget exceeded");
            found =
                births.emplace(event.birthSerial, Birth{{session, event.birthSerial, {}}, event.seconds, false}).first;
        }
        auto& birth = found->second;
        if (birth.dead) return fail("event after terminal death or reused birth identity");
        const float time = static_cast<float>(event.seconds);
        if (!birth.trajectory.keys.empty() && event.seconds != birth.lastTime &&
            time <= birth.trajectory.keys.back().seconds)
            return fail("particle times cannot be represented distinctly in asset precision");
        ParticleTrackKey key{
            time, {p.x, p.y, p.w, p.h, p.rotation, p.color.r, p.color.g, p.color.b, p.color.a, p.u0, p.v0, p.u1, p.v1}};
        if (!birth.trajectory.keys.empty() && event.seconds == birth.lastTime)
            birth.trajectory.keys.back() = key;
        else
            birth.trajectory.keys.push_back(key);
        birth.lastTime = event.seconds;
        birth.dead     = event.kind == particles::ParticleRecordEventKind::Death;
    }
    std::vector<ParticleTrajectory> tracks;
    tracks.reserve(births.size());
    for (auto& [identity, birth] : births) {
        (void)identity;
        if (!birth.dead) return fail("recording ended with an unclosed birth");
        if (birth.trajectory.keys.size() > 1) tracks.push_back(std::move(birth.trajectory));
    }
    return Result<std::vector<ParticleTrajectory>>::success(std::move(tracks));
}
}  // namespace
Result<ParticleTrack> buildParticleTrack(float duration, std::span<const ParticleRecordingSource> sources,
                                         const BakeRecipe& recipe) {
    if (sources.size() > 65536)
        return Result<ParticleTrack>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "particle session budget exceeded", "particleBakeCapture"));
    std::set<PersistentId> identities;
    std::size_t            events = 0;
    for (const auto& source : sources) {
        if (source.emitterSession.isNil() || !identities.insert(source.emitterSession).second ||
            source.events.size() > 2000000 - events)
            return Result<ParticleTrack>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "invalid or duplicate particle session, or event budget exceeded",
                "particleBakeCapture"));
        events += source.events.size();
    }
    std::vector<ParticleTrajectory> tracks;
    for (const auto& source : sources) {
        auto converted = convertEvents(source.emitterSession, duration, source.events);
        if (!converted) return Result<ParticleTrack>::failure(converted.status());
        auto values = std::move(converted).takeValue();
        if (values.size() > 65536 - tracks.size())
            return Result<ParticleTrack>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "combined particle birth budget exceeded", "particleBakeCapture"));
        for (auto& value : values) tracks.push_back(std::move(value));
    }
    return ParticleTrack::prepare(duration, std::move(tracks), recipe);
}
Result<ParticleTrack> buildParticleTrack(PersistentId session, float duration,
                                         std::span<const particles::ParticleRecordEvent> events,
                                         const BakeRecipe&                               recipe) {
    const ParticleRecordingSource source{session, events};
    return buildParticleTrack(duration, std::span<const ParticleRecordingSource>(&source, 1), recipe);
}
}  // namespace eve::physics
