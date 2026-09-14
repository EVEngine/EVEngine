#include "fluids/VolumeFluidFoam.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidDiffuse.h"

namespace eve::fluids {
namespace {
bool   range(float v, float low, float high) { return std::isfinite(v) && v >= low && v <= high; }
Status invalid(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.foam"));
}
uint32_t next(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
float     unit(uint32_t& state) { return float(next(state) >> 8) * (1.f / 16777216.f); }
glm::vec3 jitter(uint32_t& state, float radius) {
    // Fixed three draws, uniformly distributed ball; no unbounded rejection loop.
    const float z     = 2.f * unit(state) - 1.f;
    const float angle = 6.28318530718f * unit(state);
    const float r     = radius * std::cbrt(unit(state));
    const float xy    = std::sqrt(std::max(0.f, 1.f - z * z));
    return r * glm::vec3(xy * std::cos(angle), xy * std::sin(angle), z);
}
}  // namespace

Result<unsigned> VolumeFluidFoam::advance(const VolumeFluid& source, VolumeFluidDiffuse& pool, float seconds) {
    return advanceFiltered(source, pool, seconds, false, 0, 1.f);
}

Result<unsigned> VolumeFluidFoam::advanceFromActor(const VolumeFluid& source, VolumeFluidDiffuse& pool, float seconds,
                                                   unsigned actorGroup) {
    return advanceFiltered(source, pool, seconds, true, actorGroup, 1.f);
}

Result<unsigned> VolumeFluidFoam::advanceFromActorInterpolated(const VolumeFluid& source, VolumeFluidDiffuse& pool,
                                                               float seconds, unsigned actorGroup, float alpha) {
    return advanceFiltered(source, pool, seconds, true, actorGroup, alpha);
}

Result<unsigned> VolumeFluidFoam::advanceFiltered(const VolumeFluid& source, VolumeFluidDiffuse& pool, float seconds,
                                                  bool filterActor, unsigned actorGroup, float alpha) {
    if (!range(seconds, 0.f, 1.f / 30.f) || !range(alpha, 0.f, 1.f))
        return Result<unsigned>::failure(invalid("Invalid foam timestep or interpolation alpha"));
    const double   credit    = credit_ + double(seconds) * settings_.rate;
    const auto     whole     = unsigned(std::floor(credit));
    const double   remainder = credit - whole;
    const unsigned budget    = std::min({whole, settings_.maxPerStep, pool.availableCapacity()});
    if (budget == 0) {
        credit_ = remainder;
        return Result<unsigned>::success(0);
    }
    if (source.particleCount() > 65536)
        return Result<unsigned>::failure(invalid("Foam source exceeds 65536 particles"));
    const auto particles = source.particleView();
    positionScratch_.clear();
    positionScratch_.reserve(particles.size());
    for (size_t i = 0; i < particles.size(); ++i) {
        const auto& p = particles[i];
        if ((!filterActor || p.actorGroup == actorGroup) &&
            (p.material.phase == VolumeFluidPhase::Liquid || p.material.phase == VolumeFluidPhase::Gas))
            positionScratch_.push_back(source.interpolatedPositionUnchecked(i, alpha));
    }
    auto sampled = source.sampleFieldInto(positionScratch_, sampleScratch_);
    if (!sampled) return Result<unsigned>::failure(sampled.status());
    size_t eligibleCount = 0;
    for (size_t i = 0; i < positionScratch_.size(); ++i) {
        const auto& field = sampleScratch_[i];
        const float curl2 = glm::dot(field.vorticity, field.vorticity);
        if (!std::isfinite(curl2) || !std::isfinite(field.density))
            return Result<unsigned>::failure(invalid("Invalid foam field sample"));
        if (curl2 > settings_.vorticityThreshold * settings_.vorticityThreshold &&
            field.density < settings_.densityThreshold)
            ++eligibleCount;
    }
    const unsigned count = std::min(budget, unsigned(eligibleCount));
    auto           rng   = randomState_;
    batchScratch_.clear();
    batchScratch_.reserve(count);
    if (count != 0) {
        const unsigned stride          = unsigned(eligibleCount) / count;
        const unsigned start           = next(rng) % stride;
        unsigned       eligibleOrdinal = 0;
        unsigned       nextOrdinal     = start;
        for (size_t at = 0; at < positionScratch_.size() && batchScratch_.size() < count; ++at) {
            const auto& field = sampleScratch_[at];
            const float curl2 = glm::dot(field.vorticity, field.vorticity);
            if (!(curl2 > settings_.vorticityThreshold * settings_.vorticityThreshold &&
                  field.density < settings_.densityThreshold))
                continue;
            if (eligibleOrdinal++ != nextOrdinal) continue;
            batchScratch_.push_back(
                {positionScratch_[at] + jitter(rng, settings_.randomness), field.velocity, settings_.lifetime});
            nextOrdinal += stride;
        }
    }
    auto admitted = pool.emit(batchScratch_);
    if (!admitted) return Result<unsigned>::failure(admitted.status());
    credit_      = remainder;
    randomState_ = rng;
    return Result<unsigned>::success(count);
}

VolumeFluidFoamSnapshot VolumeFluidFoam::snapshot() const {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << credit_;
    if (!stream) throw std::runtime_error("Foam credit encoding failed");
    return {"eve.volume-fluid-foam", 1, settings_, stream.str(), randomState_};
}
Result<void> VolumeFluidFoam::restore(const VolumeFluidFoamSnapshot& state) {
    const auto& s      = state.settings;
    double      credit = 0;
    std::istringstream stream(state.credit);
    stream.imbue(std::locale::classic());
    stream >> std::noskipws >> credit;
    if (state.schema != "eve.volume-fluid-foam" || state.version != 1 || state.randomState == 0 ||
        !range(s.rate, 0.f, 1000000.f) || !range(s.randomness, 0.f, 10.f) ||
        !range(s.vorticityThreshold, 0.f, 1000000.f) || !range(s.densityThreshold, 0.f, 1000000000.f) ||
        !range(s.lifetime, 0.f, 86400.f) || s.lifetime == 0.f || s.maxPerStep == 0 || s.maxPerStep > 4096 ||
        stream.fail() || !stream.eof() || !std::isfinite(credit) ||
        credit < 0 || credit >= 1)
        return Result<void>::failure(invalid("Invalid foam schema, settings, credit or RNG state"));
    settings_    = s;
    credit_      = credit;
    randomState_ = state.randomState;
    return Result<void>::success();
}
}  // namespace eve::fluids
