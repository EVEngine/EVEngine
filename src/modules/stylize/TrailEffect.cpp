#include "stylize/TrailEffect.h"

#include "common/Exception.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace eve::stylize {
namespace {
bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float endpointDistance(const TrailSample& sample, glm::vec3 root, glm::vec3 tip) {
    return std::max(glm::length(root - sample.root), glm::length(tip - sample.tip));
}
}  // namespace

TrailEmitter::TrailEmitter(TrailSettings settings) : settings_(settings) { validateSettings(settings_); }

void TrailEmitter::validateSettings(const TrailSettings& settings) {
    if (settings.maxSamples < 2) throw eve::Exception("TrailEmitter: maxSamples must be at least two");
    if (!std::isfinite(settings.lifetime) || settings.lifetime <= 0.f)
        throw eve::Exception("TrailEmitter: lifetime must be finite and positive");
    if (!std::isfinite(settings.minSampleDistance) || settings.minSampleDistance < 0.f)
        throw eve::Exception("TrailEmitter: minSampleDistance must be finite and non-negative");
    if (!std::isfinite(settings.teleportDistance) || settings.teleportDistance <= 0.f)
        throw eve::Exception("TrailEmitter: teleportDistance must be finite and positive");
    if (settings.teleportDistance < settings.minSampleDistance)
        throw eve::Exception("TrailEmitter: teleportDistance must not be smaller than minSampleDistance");
}

void TrailEmitter::setSettings(TrailSettings settings) {
    validateSettings(settings);
    settings_ = settings;
    while (samples_.size() > settings_.maxSamples) samples_.pop_front();
}

void TrailEmitter::update(float dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds < 0.f)
        throw eve::Exception("TrailEmitter.update: dt must be finite and non-negative");
    for (TrailSample& sample : samples_) sample.age += dtSeconds;
    while (!samples_.empty() && samples_.front().age >= settings_.lifetime) samples_.pop_front();
}

TrailAppendResult TrailEmitter::append(glm::vec3 root, glm::vec3 tip) {
    if (!finite(root) || !finite(tip)) throw eve::Exception("TrailEmitter.append: endpoints must be finite");
    TrailAppendResult result = TrailAppendResult::Added;
    if (!samples_.empty() && !forceBreak_) {
        const float distance = endpointDistance(samples_.back(), root, tip);
        if (distance < settings_.minSampleDistance) return TrailAppendResult::SkippedTooClose;
        if (distance > settings_.teleportDistance) {
            ++segment_;
            result = TrailAppendResult::StartedSegment;
        }
    } else {
        if (!samples_.empty()) ++segment_;
        result = TrailAppendResult::StartedSegment;
    }
    samples_.push_back({root, tip, 0.f, segment_});
    forceBreak_ = false;
    while (samples_.size() > settings_.maxSamples) samples_.pop_front();
    return result;
}

void TrailEmitter::breakTrail() noexcept { forceBreak_ = true; }

void TrailEmitter::clear() noexcept {
    samples_.clear();
    segment_ = 0;
    forceBreak_ = true;
}

TrailMeshSnapshot TrailEmitter::buildMesh() const {
    TrailMeshSnapshot mesh;
    mesh.vertices.reserve(samples_.size() * 2);
    if (samples_.size() < 2) return mesh;
    std::size_t segmentStart = 0;
    while (segmentStart < samples_.size()) {
        std::size_t segmentEnd = segmentStart + 1;
        while (segmentEnd < samples_.size() && samples_[segmentEnd].segment == samples_[segmentStart].segment)
            ++segmentEnd;
        const std::size_t segmentSize = segmentEnd - segmentStart;
        for (std::size_t i = segmentStart; i < segmentEnd; ++i) {
            const float u = segmentSize > 1 ? float(i - segmentStart) / float(segmentSize - 1) : 0.f;
            const float alpha = std::clamp(1.f - samples_[i].age / settings_.lifetime, 0.f, 1.f);
            mesh.vertices.push_back({samples_[i].root, {u, 0.f}, alpha});
            mesh.vertices.push_back({samples_[i].tip, {u, 1.f}, alpha});
            if (i > segmentStart) {
                const std::uint32_t previous = static_cast<std::uint32_t>(mesh.vertices.size() - 4);
                const std::uint32_t current = static_cast<std::uint32_t>(mesh.vertices.size() - 2);
                mesh.indices.insert(mesh.indices.end(), {previous, previous + 1, current + 1,
                                                         previous, current + 1, current});
            }
        }
        segmentStart = segmentEnd;
    }
    return mesh;
}

}  // namespace eve::stylize
