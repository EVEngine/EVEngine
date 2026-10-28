#include "stylize/MeshParticleEmitter.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::stylize {
namespace {

float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

graphics::Color mixColor(const graphics::Color& a, const graphics::Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

template <typename Key, typename Value, typename Interpolate>
Value sampleKeys(const std::vector<Key>& keys, float time, Value fallback, Interpolate interpolate) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                        [](float value, const Key& key) { return value < key.time; });
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const float width = right.time - left.time;
    const float t = width > 1e-6f ? (time - left.time) / width : 1.f;
    return interpolate(left.value, right.value, t);
}

template <typename Key>
void sanitizeKeys(std::vector<Key>& keys) {
    for (auto& key : keys) key.time = std::clamp(finiteOr(key.time, 0.f), 0.f, 1.f);
    std::stable_sort(keys.begin(), keys.end(),
                     [](const Key& a, const Key& b) { return a.time < b.time; });
    keys.erase(std::unique(keys.begin(), keys.end(),
                           [](const Key& a, const Key& b) {
                               return std::abs(a.time - b.time) <= 1e-6f;
                           }),
               keys.end());
}

float sampleScalar(const std::vector<MeshParticleScalarKey>& keys, float time, float fallback) {
    return sampleKeys(keys, time, fallback,
                      [](float a, float b, float t) { return a + (b - a) * t; });
}

graphics::Color sampleColor(const std::vector<MeshParticleColorKey>& keys, float time,
                            const graphics::Color& fallback) {
    return sampleKeys(keys, time, fallback,
                      [](const auto& a, const auto& b, float t) { return mixColor(a, b, t); });
}

glm::vec3 safeDirection(glm::vec3 direction) {
    const float length = glm::length(direction);
    return length > 1e-6f && std::isfinite(length) ? direction / length : glm::vec3(0.f, 1.f, 0.f);
}

}  // namespace

struct MeshParticleEmitter::Particle {
    std::uint64_t stableId = 0;
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    float age = 0.f;
    float lifetime = 1.f;
    float rotationRadians = 0.f;
    float angularVelocity = 0.f;
};

MeshParticleEmitter::~MeshParticleEmitter() = default;

std::size_t MeshParticleEmitter::size() const noexcept { return particles_.size(); }

MeshParticleEmitter::MeshParticleEmitter(MeshParticleEmitterConfig config)
    : config_(std::move(config)) {
    config_.capacity = std::max(1u, config_.capacity);
    config_.emissionRate = std::max(0.f, finiteOr(config_.emissionRate, 0.f));
    config_.lifetimeMin = std::max(1e-4f, finiteOr(config_.lifetimeMin, 1.f));
    config_.lifetimeMax = std::max(config_.lifetimeMin, finiteOr(config_.lifetimeMax, config_.lifetimeMin));
    config_.speedMin = finiteOr(config_.speedMin, 0.f);
    config_.speedMax = std::max(config_.speedMin, finiteOr(config_.speedMax, config_.speedMin));
    config_.direction = safeDirection(config_.direction);
    config_.boxExtents = glm::max(glm::abs(config_.boxExtents), glm::vec3(0.f));
    config_.sphereRadius = std::max(0.f, finiteOr(config_.sphereRadius, 0.f));
    config_.coneAngleRadians = std::clamp(finiteOr(config_.coneAngleRadians, 0.f), 0.f, glm::pi<float>());
    config_.damping = std::max(0.f, finiteOr(config_.damping, 0.f));
    config_.rotationMinRadians = finiteOr(config_.rotationMinRadians, 0.f);
    config_.rotationMaxRadians =
        std::max(config_.rotationMinRadians,
                 finiteOr(config_.rotationMaxRadians, config_.rotationMinRadians));
    config_.angularVelocityMin = finiteOr(config_.angularVelocityMin, 0.f);
    config_.angularVelocityMax =
        std::max(config_.angularVelocityMin,
                 finiteOr(config_.angularVelocityMax, config_.angularVelocityMin));
    config_.fixedStepSeconds = std::max(1e-4f, finiteOr(config_.fixedStepSeconds, 1.f / 60.f));
    config_.maximumSubsteps = std::max(1u, config_.maximumSubsteps);
    config_.loopDurationSeconds = std::max(0.f, finiteOr(config_.loopDurationSeconds, 0.f));
    std::stable_sort(config_.bursts.begin(), config_.bursts.end(),
                     [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    sanitizeKeys(config_.scaleCurve);
    sanitizeKeys(config_.velocityCurve);
    sanitizeKeys(config_.colorGradient);
    particles_.reserve(config_.capacity);
    reset();
}

void MeshParticleEmitter::start() {
    timeline_ = 0.f;
    accumulator_ = 0.f;
    emissionAccumulator_ = 0.f;
    nextBurst_ = 0;
    randomState_ = config_.randomSeed ? config_.randomSeed : 1u;
    emitting_ = true;
}

void MeshParticleEmitter::reset() {
    particles_.clear();
    nextStableId_ = 1;
    start();
    emitting_ = false;
}

std::uint32_t MeshParticleEmitter::emit(std::uint32_t count) { return spawn(count, nullptr); }

float MeshParticleEmitter::randomUnit() {
    randomState_ ^= randomState_ << 13u;
    randomState_ ^= randomState_ >> 17u;
    randomState_ ^= randomState_ << 5u;
    return static_cast<float>(randomState_ >> 8u) * (1.f / 16777216.f);
}

glm::vec3 MeshParticleEmitter::randomDirection() {
    const float z = randomUnit() * 2.f - 1.f;
    const float angle = randomUnit() * glm::two_pi<float>();
    const float radius = std::sqrt(std::max(0.f, 1.f - z * z));
    const glm::vec3 sphere(radius * std::cos(angle), z, radius * std::sin(angle));
    if (config_.shape != MeshParticleShape::Cone) return sphere;

    const float cosLimit = std::cos(config_.coneAngleRadians);
    const float coneY = cosLimit + (1.f - cosLimit) * randomUnit();
    const float coneRadius = std::sqrt(std::max(0.f, 1.f - coneY * coneY));
    const float coneAngle = randomUnit() * glm::two_pi<float>();
    glm::vec3 local(coneRadius * std::cos(coneAngle), coneY, coneRadius * std::sin(coneAngle));
    const glm::vec3 up(0.f, 1.f, 0.f);
    const float alignment = glm::dot(up, config_.direction);
    if (alignment > 0.9999f) return local;
    if (alignment < -0.9999f) return glm::vec3(local.x, -local.y, -local.z);
    const glm::vec3 axis = glm::normalize(glm::cross(up, config_.direction));
    return glm::vec3(glm::rotate(glm::mat4(1.f), std::acos(alignment), axis) * glm::vec4(local, 0.f));
}

glm::vec3 MeshParticleEmitter::spawnOffset() {
    switch (config_.shape) {
        case MeshParticleShape::Sphere:
            return randomDirection() * (std::cbrt(randomUnit()) * config_.sphereRadius);
        case MeshParticleShape::Box:
            return {(randomUnit() * 2.f - 1.f) * config_.boxExtents.x,
                    (randomUnit() * 2.f - 1.f) * config_.boxExtents.y,
                    (randomUnit() * 2.f - 1.f) * config_.boxExtents.z};
        case MeshParticleShape::Point:
        case MeshParticleShape::Cone:
            return glm::vec3(0.f);
    }
    return glm::vec3(0.f);
}

std::uint32_t MeshParticleEmitter::spawn(std::uint32_t count, MeshParticleAdvanceReport* report) {
    const std::uint32_t available = config_.capacity - static_cast<std::uint32_t>(particles_.size());
    const std::uint32_t accepted = std::min(count, available);
    if (report) {
        report->spawned += accepted;
        report->dropped += count - accepted;
    }
    for (std::uint32_t i = 0; i < accepted; ++i) {
        Particle particle;
        particle.stableId = nextStableId_++;
        particle.position = origin_ + spawnOffset();
        const float speed = config_.speedMin + (config_.speedMax - config_.speedMin) * randomUnit();
        particle.velocity = (config_.shape == MeshParticleShape::Cone ? randomDirection()
                                                                      : config_.direction) * speed;
        particle.lifetime = config_.lifetimeMin +
                            (config_.lifetimeMax - config_.lifetimeMin) * randomUnit();
        particle.rotationRadians = config_.rotationMinRadians +
                                   (config_.rotationMaxRadians - config_.rotationMinRadians) *
                                       randomUnit();
        particle.angularVelocity = config_.angularVelocityMin +
                                   (config_.angularVelocityMax - config_.angularVelocityMin) *
                                       randomUnit();
        particles_.push_back(particle);
    }
    return accepted;
}

void MeshParticleEmitter::simulateStep(float dt, MeshParticleAdvanceReport& report) {
    for (auto& particle : particles_) {
        const float previousAge = particle.age;
        particle.age += dt;
        particle.velocity += config_.gravity * dt;
        particle.velocity *= std::exp(-config_.damping * dt);
        const float normalizedAge = std::clamp(previousAge / particle.lifetime, 0.f, 1.f);
        particle.position +=
            particle.velocity * (sampleScalar(config_.velocityCurve, normalizedAge, 1.f) * dt);
        particle.rotationRadians += particle.angularVelocity * dt;
    }
    const auto firstExpired = std::remove_if(particles_.begin(), particles_.end(),
                                             [](const Particle& p) { return p.age >= p.lifetime; });
    report.expired += static_cast<std::uint32_t>(std::distance(firstExpired, particles_.end()));
    particles_.erase(firstExpired, particles_.end());

    if (!emitting_) return;
    const float previousTimeline = timeline_;
    timeline_ += dt;
    while (nextBurst_ < config_.bursts.size() &&
           config_.bursts[nextBurst_].timeSeconds <= timeline_ + 1e-6f) {
        if (config_.bursts[nextBurst_].timeSeconds >= previousTimeline - 1e-6f)
            (void)spawn(config_.bursts[nextBurst_].count, &report);
        ++nextBurst_;
    }
    emissionAccumulator_ += config_.emissionRate * dt;
    const auto continuous = static_cast<std::uint32_t>(std::floor(emissionAccumulator_));
    emissionAccumulator_ -= static_cast<float>(continuous);
    (void)spawn(continuous, &report);

    if (config_.looping && config_.loopDurationSeconds > 0.f &&
        timeline_ >= config_.loopDurationSeconds) {
        timeline_ = std::fmod(timeline_, config_.loopDurationSeconds);
        nextBurst_ = 0;
    }
}

MeshParticleAdvanceReport MeshParticleEmitter::advance(float deltaSeconds) {
    MeshParticleAdvanceReport report;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f) {
        report.alive = static_cast<std::uint32_t>(particles_.size());
        return report;
    }
    accumulator_ += deltaSeconds;
    while (accumulator_ + 1e-7f >= config_.fixedStepSeconds &&
           report.simulatedSteps < config_.maximumSubsteps) {
        simulateStep(config_.fixedStepSeconds, report);
        accumulator_ -= config_.fixedStepSeconds;
        ++report.simulatedSteps;
    }
    if (accumulator_ >= config_.fixedStepSeconds) {
        const float retained = std::fmod(accumulator_, config_.fixedStepSeconds);
        report.discardedTimeSeconds = accumulator_ - retained;
        accumulator_ = retained;
    }
    report.alive = static_cast<std::uint32_t>(particles_.size());
    return report;
}

std::vector<MeshParticleInstance> MeshParticleEmitter::snapshot() const {
    std::vector<MeshParticleInstance> result;
    result.reserve(particles_.size());
    for (const auto& particle : particles_) {
        const float t = std::clamp(particle.age / particle.lifetime, 0.f, 1.f);
        const float linearScale = config_.scaleStart + (config_.scaleEnd - config_.scaleStart) * t;
        const float scale = config_.scaleCurve.empty()
                                ? linearScale
                                : sampleScalar(config_.scaleCurve, t, linearScale);
        glm::mat4 model = glm::translate(glm::mat4(1.f), particle.position);
        model = glm::rotate(model, particle.rotationRadians, safeDirection(particle.velocity));
        model = glm::scale(model, glm::vec3(scale));
        const graphics::Color linearColor = mixColor(config_.colorStart, config_.colorEnd, t);
        result.push_back(
            {particle.stableId, model, sampleColor(config_.colorGradient, t, linearColor), t});
    }
    return result;
}

}  // namespace eve::stylize
