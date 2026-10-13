#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCoupling.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "fluids/VolumeFluidFoam.h"
#include "physics/Body3D.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"

// VolumeFluidContactTracker.cpp
namespace eve::fluids {
namespace {
bool keyLess(const VolumeFluidContact& a, const VolumeFluidContact& b) {
    return a.colliderLabel < b.colliderLabel || (a.colliderLabel == b.colliderLabel && a.actorGroup < b.actorGroup);
}
bool sameKey(const VolumeFluidContact& a, const VolumeFluidContact& b) {
    return a.colliderLabel == b.colliderLabel && a.actorGroup == b.actorGroup;
}
bool finiteContact(const VolumeFluidContact& contact) {
    const auto finite3 = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    return finite3(contact.point) && finite3(contact.normal) && finite3(contact.impulse) &&
           std::isfinite(contact.distance);
}
}  // namespace

Result<std::vector<VolumeFluidContactEvent>> VolumeFluidContactTracker::advance(
    std::span<const VolumeFluidContact> contacts, float distanceThreshold) {
    using Output = Result<std::vector<VolumeFluidContactEvent>>;
    if (contacts.size() > 65536 || !std::isfinite(distanceThreshold) || distanceThreshold < 0.f ||
        distanceThreshold > 1.f ||
        std::any_of(contacts.begin(), contacts.end(), [](const auto& contact) { return !finiteContact(contact); }))
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid contact event batch",
                                                 "fluids.volume.contacts"));

    scratch_.clear();
    scratch_.reserve(std::max(scratch_.capacity(), contacts.size()));
    for (const auto& contact : contacts)
        if (contact.distance <= distanceThreshold) scratch_.push_back(contact);
    std::stable_sort(scratch_.begin(), scratch_.end(), keyLess);
    scratch_.erase(std::unique(scratch_.begin(), scratch_.end(), sameKey), scratch_.end());

    std::vector<VolumeFluidContactEvent> events;
    events.reserve(scratch_.size() + previous_.size());
    size_t current = 0, previous = 0;
    while (current < scratch_.size() && previous < previous_.size()) {
        if (keyLess(scratch_[current], previous_[previous]))
            events.push_back({VolumeFluidContactEventType::Enter, scratch_[current++]});
        else if (keyLess(previous_[previous], scratch_[current]))
            events.push_back({VolumeFluidContactEventType::Exit, previous_[previous++]});
        else {
            events.push_back({VolumeFluidContactEventType::Stay, scratch_[current++]});
            ++previous;
        }
    }
    while (current < scratch_.size()) events.push_back({VolumeFluidContactEventType::Enter, scratch_[current++]});
    while (previous < previous_.size()) events.push_back({VolumeFluidContactEventType::Exit, previous_[previous++]});
    previous_ = scratch_;
    return Output::success(std::move(events));
}

void VolumeFluidContactTracker::reset() {
    previous_.clear();
    scratch_.clear();
}
}  // namespace eve::fluids

// VolumeFluidCoupling.cpp
namespace eve::fluids {
struct VolumeFluidCoupling::Impl {
    struct Binding {
        physics::PhysicsLink link;
        VolumeFluidCollider  shape;
    };
    std::vector<Binding>                     bindings;
    std::vector<physics::Body3D*>            bodies;
    std::vector<VolumeFluidCollider>         samples;
    std::vector<glm::vec3>                   centers, linear, angular;
    std::vector<std::pair<unsigned, size_t>> labels;
    float                                    maximumLinearDelta = 20.f, maximumAngularDelta = 100.f;
    unsigned                                 clampedBodyCount = 0;
};
VolumeFluidCoupling::VolumeFluidCoupling() : impl_(std::make_unique<Impl>()) {}
VolumeFluidCoupling::~VolumeFluidCoupling() = default;
Result<void> VolumeFluidCoupling::attach(physics::Body3D& body, const VolumeFluidCollider& localShape) {
    if (impl_->bindings.size() >= 1024 || std::any_of(impl_->bindings.begin(), impl_->bindings.end(),
                                                      [&](const auto& b) { return b.shape.label == localShape.label; }))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Duplicate coupling label or binding limit exceeded",
                                                       "fluids.volume.coupling"));
    auto link = physics::PhysicsLink::fromBody(body);
    if (!link) return Result<void>::failure(link.status());
    auto validator = VolumeFluid::create(VolumeFluidSettings{.capacity = 1});
    if (!validator) return Result<void>::failure(validator.status());
    auto valid = validator.value()->setColliders(std::span(&localShape, 1));
    if (!valid) return valid;
    impl_->bindings.push_back({link.value(), localShape});
    return Result<void>::success();
}
void VolumeFluidCoupling::detach(unsigned label) {
    std::erase_if(impl_->bindings, [label](const auto& b) { return b.shape.label == label; });
}
Result<void> VolumeFluidCoupling::setImpulseLimits(float maximumLinearDelta, float maximumAngularDelta) {
    if (!std::isfinite(maximumLinearDelta) || maximumLinearDelta < .01f || maximumLinearDelta > 1000.f ||
        !std::isfinite(maximumAngularDelta) || maximumAngularDelta < .01f || maximumAngularDelta > 10000.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid rigid impulse velocity-change limits", "fluids.volume.coupling"));
    impl_->maximumLinearDelta  = maximumLinearDelta;
    impl_->maximumAngularDelta = maximumAngularDelta;
    return Result<void>::success();
}
unsigned         VolumeFluidCoupling::lastClampedBodyCount() const { return impl_->clampedBodyCount; }
Result<unsigned> VolumeFluidCoupling::step(physics::World3D& world, VolumeFluid& fluid, float dt, unsigned substeps,
                                           std::span<const VolumeFluidThermalRule> rules) {
    if (!world.isValid())
        return Result<unsigned>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Rigid world is no longer valid", "fluids.volume.coupling"));
    auto& bodies  = impl_->bodies;
    auto& samples = impl_->samples;
    auto& centers = impl_->centers;
    auto& labels  = impl_->labels;
    bodies.clear();
    samples.clear();
    centers.clear();
    labels.clear();
    bodies.reserve(impl_->bindings.size());
    samples.reserve(impl_->bindings.size());
    centers.reserve(impl_->bindings.size());
    labels.reserve(impl_->bindings.size());
    for (const auto& binding : impl_->bindings) {
        auto resolved = binding.link.resolve(world);
        if (!resolved) return Result<unsigned>::failure(resolved.status());
        auto*           body = resolved.value();
        const glm::quat orientation(body->getRotW(), body->getRotX(), body->getRotY(), body->getRotZ());
        auto            sample = binding.shape;
        const auto      center = body->localToWorldPointOwned(sample.center.x, sample.center.y, sample.center.z);
        if (!center) return Result<unsigned>::failure(center.status());
        const auto velocity = body->getLocalPointVelocityOwned(sample.center.x, sample.center.y, sample.center.z);
        if (!velocity) return Result<unsigned>::failure(velocity.status());
        sample.center          = {center.value().x, center.value().y, center.value().z};
        sample.velocity        = {velocity.value().x, velocity.value().y, velocity.value().z};
        sample.angularVelocity = {body->getAngularVelocityX(), body->getAngularVelocityY(),
                                  body->getAngularVelocityZ()};
        const auto q =
            orientation * glm::quat(sample.rotation.w, sample.rotation.x, sample.rotation.y, sample.rotation.z);
        sample.rotation = {q.x, q.y, q.z, q.w};
        labels.emplace_back(sample.label, bodies.size());
        bodies.push_back(body);
        samples.push_back(sample);
        centers.push_back({body->getWorldCenterX(), body->getWorldCenterY(), body->getWorldCenterZ()});
    }
    std::sort(labels.begin(), labels.end());
    // Allocate all output aggregation before advancing either domain.
    auto& linear  = impl_->linear;
    auto& angular = impl_->angular;
    linear.assign(bodies.size(), glm::vec3(0.f));
    angular.assign(bodies.size(), glm::vec3(0.f));
    auto advanced = fluid.stepWithColliders(dt, substeps, samples, rules);
    if (!advanced) return Result<unsigned>::failure(advanced.status());
    const auto contacts = fluid.contacts();
    for (const auto& contact : contacts) {
        const auto at =
            std::lower_bound(labels.begin(), labels.end(), std::pair<unsigned, size_t>{contact.colliderLabel, 0});
        if (at == labels.end() || at->first != contact.colliderLabel) continue;
        const size_t i = at->second;
        linear[i] += contact.impulse;
        angular[i] += glm::cross(contact.point - centers[i], contact.impulse);
    }
    const auto reactions = fluid.attachmentReactions();
    for (const auto& reaction : reactions) {
        const auto at =
            std::lower_bound(labels.begin(), labels.end(), std::pair<unsigned, size_t>{reaction.colliderLabel, 0});
        if (at == labels.end() || at->first != reaction.colliderLabel) continue;
        const size_t i = at->second;
        linear[i] += reaction.impulse;
        angular[i] += glm::cross(reaction.point - centers[i], reaction.impulse);
        angular[i] += reaction.angularImpulse;
    }
    unsigned clampedBodyCount = 0;
    for (size_t i = 0; i < bodies.size(); ++i) {
        bool        clamped      = false;
        const float mass         = bodies[i]->getMass();
        const float linearLength = glm::length(linear[i]);
        if (mass > 0.f && linearLength > mass * impl_->maximumLinearDelta) {
            linear[i] *= mass * impl_->maximumLinearDelta / linearLength;
            clamped = true;
        }
        if (mass > 0.f && glm::dot(angular[i], angular[i]) > 0.f) {
            const glm::mat3 localInertia(
                bodies[i]->getInertiaXX(), bodies[i]->getInertiaXY(), bodies[i]->getInertiaXZ(),
                bodies[i]->getInertiaXY(), bodies[i]->getInertiaYY(), bodies[i]->getInertiaYZ(),
                bodies[i]->getInertiaXZ(), bodies[i]->getInertiaYZ(), bodies[i]->getInertiaZZ());
            const float inertiaScale =
                std::max({std::abs(localInertia[0][0]), std::abs(localInertia[0][1]), std::abs(localInertia[0][2]),
                          std::abs(localInertia[1][0]), std::abs(localInertia[1][1]), std::abs(localInertia[1][2]),
                          std::abs(localInertia[2][0]), std::abs(localInertia[2][1]), std::abs(localInertia[2][2])});
            if (std::isfinite(inertiaScale) && inertiaScale > 0.f) {
                const auto  normalizedInertia = localInertia / inertiaScale;
                const float determinant       = glm::determinant(normalizedInertia);
                if (std::isfinite(determinant) && std::abs(determinant) > 1e-6f) {
                    const glm::quat q(bodies[i]->getRotW(), bodies[i]->getRotX(), bodies[i]->getRotY(),
                                      bodies[i]->getRotZ());
                    const auto      rotation     = glm::mat3_cast(q);
                    const auto      angularDelta = rotation * (glm::inverse(normalizedInertia) / inertiaScale) *
                                              glm::transpose(rotation) * angular[i];
                    const float angularLength = glm::length(angularDelta);
                    if (angularLength > impl_->maximumAngularDelta) {
                        angular[i] *= impl_->maximumAngularDelta / angularLength;
                        clamped = true;
                    }
                }
            }
        }
        if (glm::dot(linear[i], linear[i]) > 0.f) bodies[i]->applyLinearImpulse(linear[i].x, linear[i].y, linear[i].z);
        if (glm::dot(angular[i], angular[i]) > 0.f)
            bodies[i]->applyAngularImpulse(angular[i].x, angular[i].y, angular[i].z);
        if (clamped) ++clampedBodyCount;
    }
    impl_->clampedBodyCount = clampedBodyCount;
    return Result<unsigned>::success(unsigned(contacts.size() + reactions.size()));
}
}  // namespace eve::fluids

// VolumeFluidDiffuse.cpp
namespace eve::fluids {
namespace {
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool valid(const VolumeFluidDiffuseParticle& p) {
    return finite(p.position) && finite(p.velocity) && glm::length(p.velocity) <= 100.f && std::isfinite(p.life) &&
           p.life > 0.f && p.life <= 86400.f;
}
}  // namespace
Result<void> VolumeFluidDiffuse::emit(std::span<const VolumeFluidDiffuseParticle> particles) {
    if (particles.size() > availableCapacity())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Diffuse capacity exceeded", "fluids.volume.diffuse"));
    for (const auto& particle : particles)
        if (!valid(particle))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid diffuse particle",
                                                           "fluids.volume.diffuse"));
    particles_.insert(particles_.end(), particles.begin(), particles.end());
    return Result<void>::success();
}
Result<void> VolumeFluidDiffuse::advance(const VolumeFluid& fluid, float seconds, unsigned minimumNeighbors) {
    if (!std::isfinite(seconds) || seconds <= 0.f || seconds > 1.f / 30.f || minimumNeighbors > 1000000)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid diffuse timestep or neighbor threshold",
                                                       "fluids.volume.diffuse"));
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
        if (!valid(particle))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Invalid diffuse advection result", "fluids.volume.diffuse"));
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
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid diffuse snapshot", "fluids.volume.diffuse"));
    for (const auto& p : snapshot.particles)
        if (!valid(p))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Invalid diffuse snapshot particle", "fluids.volume.diffuse"));
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

// VolumeFluidFoam.cpp
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
    const auto&        s      = state.settings;
    double             credit = 0;
    std::istringstream stream(state.credit);
    stream.imbue(std::locale::classic());
    stream >> std::noskipws >> credit;
    if (state.schema != "eve.volume-fluid-foam" || state.version != 1 || state.randomState == 0 ||
        !range(s.rate, 0.f, 1000000.f) || !range(s.randomness, 0.f, 10.f) ||
        !range(s.vorticityThreshold, 0.f, 1000000.f) || !range(s.densityThreshold, 0.f, 1000000000.f) ||
        !range(s.lifetime, 0.f, 86400.f) || s.lifetime == 0.f || s.maxPerStep == 0 || s.maxPerStep > 4096 ||
        stream.fail() || !stream.eof() || !std::isfinite(credit) || credit < 0 || credit >= 1)
        return Result<void>::failure(invalid("Invalid foam schema, settings, credit or RNG state"));
    settings_    = s;
    credit_      = credit;
    randomState_ = state.randomState;
    return Result<void>::success();
}
}  // namespace eve::fluids
