#include "fluids/VolumeFluidDiffuse.h"
#include <cmath>
#include <glm/geometric.hpp>
#include "fluids/VolumeFluid.h"

namespace eve::fluids {
namespace {
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool valid(const VolumeFluidDiffuseParticle& p) {
    return finite(p.position) && finite(p.velocity) && glm::length(p.velocity) <= 100.f && std::isfinite(p.life) &&
           p.life > 0.f && p.life <= 86400.f;
}
Result<void> invalid(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.diffuse"));
}
}  // namespace
Result<void> VolumeFluidDiffuse::emit(std::span<const VolumeFluidDiffuseParticle> particles) {
    if (particles.size() > availableCapacity()) return invalid("Diffuse capacity exceeded");
    for (const auto& particle : particles)
        if (!valid(particle)) return invalid("Invalid diffuse particle");
    particles_.insert(particles_.end(), particles.begin(), particles.end());
    return Result<void>::success();
}
Result<void> VolumeFluidDiffuse::advance(const VolumeFluid& fluid, float seconds, unsigned minimumNeighbors) {
    if (!std::isfinite(seconds) || seconds <= 0.f || seconds > 1.f / 30.f || minimumNeighbors > 1000000)
        return invalid("Invalid diffuse timestep or neighbor threshold");
    candidateScratch_.clear();
    positionScratch_.clear();
    candidateScratch_.reserve(particles_.size());
    positionScratch_.reserve(particles_.size());
    for (auto particle : particles_) {
        particle.life -= seconds;
        if (particle.life <= 0.f) continue;
        candidateScratch_.push_back(particle);
        positionScratch_.push_back(particle.position);
    }
    if (candidateScratch_.empty()) {
        particles_.clear();
        return Result<void>::success();
    }
    auto sampled = fluid.sampleFieldInto(positionScratch_, sampleScratch_);
    if (!sampled) return Result<void>::failure(sampled.status());
    size_t remaining = 0;
    for (size_t i = 0; i < candidateScratch_.size(); ++i) {
        const auto& sample = sampleScratch_[i];
        if (sample.neighborCount < minimumNeighbors) continue;
        auto particle     = candidateScratch_[i];
        particle.velocity = sample.velocity;
        particle.position += particle.velocity * seconds;
        if (!valid(particle)) return invalid("Invalid diffuse advection result");
        candidateScratch_[remaining++] = particle;
    }
    candidateScratch_.resize(remaining);
    particles_.swap(candidateScratch_);
    return Result<void>::success();
}
VolumeFluidDiffuseSnapshot VolumeFluidDiffuse::snapshot() const {
    return {"eve.volume-fluid-diffuse", 1, capacity_, particles_};
}
Result<void> VolumeFluidDiffuse::restore(const VolumeFluidDiffuseSnapshot& snapshot) {
    if (snapshot.schema != "eve.volume-fluid-diffuse" || snapshot.version != 1 || snapshot.capacity == 0 ||
        snapshot.capacity > 65536 || snapshot.particles.size() > snapshot.capacity)
        return invalid("Invalid diffuse snapshot");
    for (const auto& p : snapshot.particles)
        if (!valid(p)) return invalid("Invalid diffuse snapshot particle");
    auto candidate = snapshot.particles;
    particles_.swap(candidate);
    capacity_ = snapshot.capacity;
    return Result<void>::success();
}
unsigned VolumeFluidDiffuse::particleCount() const { return unsigned(particles_.size()); }
unsigned VolumeFluidDiffuse::availableCapacity() const { return capacity_ - particleCount(); }
void     VolumeFluidDiffuse::copyRenderData(std::vector<glm::vec4>& positionLife) const {
    positionLife.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i)
        positionLife[i] = glm::vec4(particles_[i].position, particles_[i].life);
}
void VolumeFluidDiffuse::clear() { particles_.clear(); }
}  // namespace eve::fluids
