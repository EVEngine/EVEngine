#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace eve::stylize {

/** @brief Sampling and retention policy for a weapon ribbon trail. */
struct TrailSettings {
    std::size_t maxSamples = 48;
    float lifetime = 0.28f;
    float minSampleDistance = 0.015f;
    float teleportDistance = 2.f;
};

/** @brief Result of attempting to append one weapon-edge sample. */
enum class TrailAppendResult { Added, StartedSegment, SkippedTooClose };

/** @brief One retained pair of weapon root/tip positions. */
struct TrailSample {
    glm::vec3 root{0.f};
    glm::vec3 tip{0.f};
    float age = 0.f;
    std::uint32_t segment = 0;
};

/** @brief Backend-neutral vertex emitted by TrailEmitter. */
struct TrailVertex {
    glm::vec3 position{0.f};
    glm::vec2 uv{0.f};
    float alpha = 0.f;
};

/** @brief Owning CPU snapshot suitable for upload by a graphics adapter. */
struct TrailMeshSnapshot {
    std::vector<TrailVertex> vertices;
    std::vector<std::uint32_t> indices;
};

/**
 * @brief Deterministic CPU sampler and ribbon mesh generator for weapon trails.
 *
 * Call update(dt) once per simulation step, then append(root, tip). A large
 * endpoint jump starts a disconnected segment, preventing teleport triangles.
 * The emitter owns all samples and returned snapshots own their data. It is
 * main-thread affine, performs no rendering, and invokes no callbacks.
 */
class TrailEmitter {
public:
    /** @brief Construct an empty emitter with validated settings. */
    explicit TrailEmitter(TrailSettings settings = {});
    TrailEmitter(const TrailEmitter&) = delete;
    TrailEmitter& operator=(const TrailEmitter&) = delete;

    /** @brief Replace settings and enforce the new sample budget. */
    void setSettings(TrailSettings settings);
    /** @brief Return a value snapshot of current settings. */
    [[nodiscard]] TrailSettings settings() const noexcept { return settings_; }
    /** @brief Age samples and remove expired entries using injected dt. */
    void update(float dtSeconds);
    /** @brief Append a weapon edge sample or report why it was not appended. */
    [[nodiscard]] TrailAppendResult append(glm::vec3 root, glm::vec3 tip);
    /** @brief Start a disconnected segment at the next appended sample. */
    void breakTrail() noexcept;
    /** @brief Remove all samples and reset segment numbering. */
    void clear() noexcept;
    /** @brief Return the number of retained root/tip samples. */
    [[nodiscard]] std::size_t sampleCount() const noexcept { return samples_.size(); }
    /** @brief Build an owning triangle-list snapshot from current samples. */
    [[nodiscard]] TrailMeshSnapshot buildMesh() const;

private:
    static void validateSettings(const TrailSettings& settings);

    TrailSettings settings_;
    std::deque<TrailSample> samples_;
    std::uint32_t segment_ = 0;
    bool forceBreak_ = true;
};

}  // namespace eve::stylize
