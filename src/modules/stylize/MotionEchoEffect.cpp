#include "stylize/MotionEchoEffect.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace eve::stylize {
namespace {

glm::vec3 safeNormal(glm::vec3 value, glm::vec3 fallback) {
    const float length = glm::length(value);
    return length > 1e-6f && std::isfinite(length) ? value / length : fallback;
}

graphics::Color mixColor(const graphics::Color& a, const graphics::Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

}  // namespace

ProjectileTrailEffect::ProjectileTrailEffect(ProjectileTrailConfig config)
    : config_(config) {
    config_.sampleIntervalSeconds =
        std::max(1e-4f, std::isfinite(config_.sampleIntervalSeconds)
                              ? config_.sampleIntervalSeconds
                              : 1.f / 60.f);
    config_.sampleLifetimeSeconds =
        std::max(config_.sampleIntervalSeconds,
                 std::isfinite(config_.sampleLifetimeSeconds)
                     ? config_.sampleLifetimeSeconds
                     : config_.sampleIntervalSeconds);
    config_.minimumDistance =
        std::max(0.f, std::isfinite(config_.minimumDistance) ? config_.minimumDistance : 0.f);
    config_.widthHead = std::max(0.f, std::isfinite(config_.widthHead) ? config_.widthHead : 0.f);
    config_.widthTail = std::max(0.f, std::isfinite(config_.widthTail) ? config_.widthTail : 0.f);
    config_.maximumSamples = std::max<std::size_t>(2u, config_.maximumSamples);
    samples_.reserve(config_.maximumSamples);
}

void ProjectileTrailEffect::reset() noexcept {
    samples_.clear();
    sampleAccumulator_ = 0.f;
    hasPreviousInput_ = false;
}

bool ProjectileTrailEffect::append(const glm::vec3& position, float initialAge,
                                   MotionEchoAdvanceReport& report) {
    if (!samples_.empty() &&
        glm::length(position - samples_.back().position) < config_.minimumDistance) {
        ++report.distanceRejected;
        return false;
    }
    if (samples_.size() == config_.maximumSamples) {
        samples_.erase(samples_.begin());
        ++report.expired;
    }
    samples_.push_back({position, std::max(0.f, initialAge)});
    ++report.captured;
    return true;
}

MotionEchoAdvanceReport ProjectileTrailEffect::update(float deltaSeconds,
                                                       const glm::vec3& currentPosition) {
    MotionEchoAdvanceReport report;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.f ||
        !std::isfinite(currentPosition.x) || !std::isfinite(currentPosition.y) ||
        !std::isfinite(currentPosition.z)) {
        report.alive = static_cast<std::uint32_t>(samples_.size());
        return report;
    }
    for (auto& sample : samples_) sample.age += deltaSeconds;
    const auto expired = std::remove_if(samples_.begin(), samples_.end(), [&](const Sample& sample) {
        return sample.age >= config_.sampleLifetimeSeconds;
    });
    report.expired += static_cast<std::uint32_t>(std::distance(expired, samples_.end()));
    samples_.erase(expired, samples_.end());

    if (!hasPreviousInput_) {
        previousInput_ = currentPosition;
        hasPreviousInput_ = true;
        (void)append(currentPosition, 0.f, report);
    } else {
        const float previousAccumulator = sampleAccumulator_;
        sampleAccumulator_ += deltaSeconds;
        const std::uint32_t count = static_cast<std::uint32_t>(
            std::floor(sampleAccumulator_ / config_.sampleIntervalSeconds));
        for (std::uint32_t i = 0; i < count; ++i) {
            const float elapsed = config_.sampleIntervalSeconds - previousAccumulator +
                                  static_cast<float>(i) * config_.sampleIntervalSeconds;
            const float t = deltaSeconds > 1e-6f ? std::clamp(elapsed / deltaSeconds, 0.f, 1.f) : 1.f;
            (void)append(previousInput_ + (currentPosition - previousInput_) * t,
                         std::max(0.f, deltaSeconds - elapsed), report);
        }
        sampleAccumulator_ -= static_cast<float>(count) * config_.sampleIntervalSeconds;
        previousInput_ = currentPosition;
    }
    report.alive = static_cast<std::uint32_t>(samples_.size());
    return report;
}

TrailMeshSnapshot ProjectileTrailEffect::build(const glm::vec3& cameraForward) const {
    TrailMeshSnapshot mesh;
    if (samples_.size() < 2) return mesh;
    mesh.vertices.reserve(samples_.size() * 2u);
    mesh.indices.reserve((samples_.size() - 1u) * 6u);
    const glm::vec3 view = safeNormal(cameraForward, glm::vec3(0.f, 0.f, -1.f));
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples_.size() - 1u);
        const auto& previous = samples_[i == 0 ? 0 : i - 1u].position;
        const auto& next = samples_[std::min(i + 1u, samples_.size() - 1u)].position;
        const glm::vec3 tangent = safeNormal(next - previous, glm::vec3(0.f, 1.f, 0.f));
        glm::vec3 side = safeNormal(glm::cross(view, tangent), glm::vec3(1.f, 0.f, 0.f));
        const float width = config_.widthTail + (config_.widthHead - config_.widthTail) * t;
        const float alpha = std::clamp(1.f - samples_[i].age / config_.sampleLifetimeSeconds,
                                       0.f, 1.f);
        mesh.vertices.push_back({samples_[i].position - side * (width * 0.5f), {t, 0.f}, alpha});
        mesh.vertices.push_back({samples_[i].position + side * (width * 0.5f), {t, 1.f}, alpha});
        if (i == 0) continue;
        const std::uint32_t left0 = static_cast<std::uint32_t>((i - 1u) * 2u);
        const std::uint32_t right0 = left0 + 1u;
        const std::uint32_t left1 = static_cast<std::uint32_t>(i * 2u);
        const std::uint32_t right1 = left1 + 1u;
        mesh.indices.insert(mesh.indices.end(), {left0, right0, left1, left1, right0, right1});
    }
    return mesh;
}

AfterimageEffect::AfterimageEffect(AfterimageEffectConfig config) : config_(config) {
    config_.sampleIntervalSeconds =
        std::max(1e-4f, std::isfinite(config_.sampleIntervalSeconds)
                              ? config_.sampleIntervalSeconds
                              : 0.06f);
    config_.lifetimeSeconds =
        std::max(config_.sampleIntervalSeconds,
                 std::isfinite(config_.lifetimeSeconds) ? config_.lifetimeSeconds
                                                        : config_.sampleIntervalSeconds);
    config_.maximumImages = std::max<std::size_t>(1u, config_.maximumImages);
    images_.reserve(config_.maximumImages);
}

void AfterimageEffect::reset() noexcept {
    images_.clear();
    sampleAccumulator_ = 0.f;
    nextStableId_ = 1;
}

MotionEchoAdvanceReport AfterimageEffect::update(float deltaSeconds,
                                                  const glm::mat4& currentModel) {
    MotionEchoAdvanceReport report;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.f) {
        report.alive = static_cast<std::uint32_t>(images_.size());
        return report;
    }
    for (auto& image : images_) image.age += deltaSeconds;
    const auto expired = std::remove_if(images_.begin(), images_.end(), [&](const Image& image) {
        return image.age >= config_.lifetimeSeconds;
    });
    report.expired = static_cast<std::uint32_t>(std::distance(expired, images_.end()));
    images_.erase(expired, images_.end());

    const float previousAccumulator = sampleAccumulator_;
    sampleAccumulator_ += deltaSeconds;
    const auto captures = static_cast<std::uint32_t>(
        std::floor(sampleAccumulator_ / config_.sampleIntervalSeconds));
    sampleAccumulator_ -= static_cast<float>(captures) * config_.sampleIntervalSeconds;
    for (std::uint32_t i = 0; i < captures; ++i) {
        if (images_.size() == config_.maximumImages) {
            images_.erase(images_.begin());
            ++report.expired;
        }
        const float elapsed = config_.sampleIntervalSeconds - previousAccumulator +
                              static_cast<float>(i) * config_.sampleIntervalSeconds;
        const float initialAge = std::max(0.f, deltaSeconds - elapsed);
        const std::uint64_t stableId = nextStableId_++;
        ++report.captured;
        if (initialAge >= config_.lifetimeSeconds) {
            ++report.expired;
            continue;
        }
        images_.push_back({stableId, currentModel, initialAge});
    }
    report.alive = static_cast<std::uint32_t>(images_.size());
    return report;
}

std::vector<MeshParticleInstance> AfterimageEffect::snapshot() const {
    std::vector<MeshParticleInstance> result;
    result.reserve(images_.size());
    for (const auto& image : images_) {
        const float t = std::clamp(image.age / config_.lifetimeSeconds, 0.f, 1.f);
        result.push_back({image.stableId, image.model,
                          mixColor(config_.colorStart, config_.colorEnd, t), t});
    }
    return result;
}

}  // namespace eve::stylize
