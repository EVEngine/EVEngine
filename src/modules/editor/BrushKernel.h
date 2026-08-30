#pragma once

#include "editor/EditorTarget.h"

#include <vector>

namespace eve::editor {

/** @brief Location and size passed to an interchangeable brush kernel. */
struct BrushSample {
    float centerX = 0.f;
    float centerY = 0.f;
    float radius = 0.5f;
    float rotation = 0.f;
};

/** @brief One weighted grid coordinate emitted by a brush kernel. */
struct BrushPoint {
    int x = 0;
    int y = 0;
    float weight = 0.f;
};

/** @brief Receives weighted points without constraining their storage. */
class IBrushSampleSink {
public:
    virtual ~IBrushSampleSink() = default;
    virtual void emit(int x, int y, float weight) = 0;
};

/** @brief Replaceable mapping from normalized distance to brush strength. */
class IBrushFalloff {
public:
    virtual ~IBrushFalloff() = default;
    virtual float evaluate(float normalizedDistance) const = 0;
};

class ConstantBrushFalloff final : public IBrushFalloff {
public:
    float evaluate(float normalizedDistance) const override;
};

class LinearBrushFalloff final : public IBrushFalloff {
public:
    float evaluate(float normalizedDistance) const override;
};

class SmoothBrushFalloff final : public IBrushFalloff {
public:
    float evaluate(float normalizedDistance) const override;
};

/** @brief Shape protocol shared by tile, terrain, mask and custom tools. */
class IBrushKernel {
public:
    virtual ~IBrushKernel() = default;
    virtual EditRegion bounds(const BrushSample &sample) const = 0;
    virtual void sample(const BrushSample &sample, IBrushSampleSink &sink) const = 0;
};

/** @brief Circular weighted kernel using a non-owning falloff strategy. */
class CircleBrushKernel final : public IBrushKernel {
public:
    explicit CircleBrushKernel(const IBrushFalloff *falloff = nullptr);
    void setFalloff(const IBrushFalloff *falloff) { falloff_ = falloff; }
    EditRegion bounds(const BrushSample &sample) const override;
    void sample(const BrushSample &sample, IBrushSampleSink &sink) const override;
private:
    const IBrushFalloff *falloff_ = nullptr;
};

/** @brief Rotatable square kernel using a non-owning falloff strategy. */
class BoxBrushKernel final : public IBrushKernel {
public:
    explicit BoxBrushKernel(const IBrushFalloff *falloff = nullptr);
    void setFalloff(const IBrushFalloff *falloff) { falloff_ = falloff; }
    EditRegion bounds(const BrushSample &sample) const override;
    void sample(const BrushSample &sample, IBrushSampleSink &sink) const override;
private:
    const IBrushFalloff *falloff_ = nullptr;
};

/** @brief Convenience sink for previews, tests and command construction. */
class BrushSampleBuffer final : public IBrushSampleSink {
public:
    void emit(int x, int y, float weight) override;
    void clear() { points_.clear(); }
    int size() const { return static_cast<int>(points_.size()); }
    const BrushPoint &point(int index) const;
private:
    std::vector<BrushPoint> points_;
};

}  // namespace eve::editor
