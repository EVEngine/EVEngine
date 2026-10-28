#pragma once
#include "common/Export.h"


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
    /** @brief Releases IBrushSampleSink resources. */
    virtual ~IBrushSampleSink() = default;
    /** @brief Emit. */
    virtual void emit(int x, int y, float weight) = 0;
};

/** @brief Replaceable mapping from normalized distance to brush strength. */
class IBrushFalloff {
public:
    /** @brief Releases IBrushFalloff resources. */
    virtual ~IBrushFalloff() = default;
    /** @brief Evaluate. */
    virtual float evaluate(float normalizedDistance) const = 0;
};

/** @brief EVENGINE_API_ORCHESTRATION public API. */
class EVENGINE_API_ORCHESTRATION ConstantBrushFalloff final : public IBrushFalloff {
public:
    /** @brief Evaluate. */
    float evaluate(float normalizedDistance) const override;
};

/** @brief EVENGINE_API_ORCHESTRATION public API. */
class EVENGINE_API_ORCHESTRATION LinearBrushFalloff final : public IBrushFalloff {
public:
    /** @brief Evaluate. */
    float evaluate(float normalizedDistance) const override;
};

/** @brief SmoothBrushFalloff public API. */
class SmoothBrushFalloff final : public IBrushFalloff {
public:
    /** @brief Evaluate. */
    float evaluate(float normalizedDistance) const override;
};

/** @brief Shape protocol shared by tile, terrain, mask and custom tools. */
class IBrushKernel {
public:
    /** @brief Releases IBrushKernel resources. */
    virtual ~IBrushKernel() = default;
    /** @brief Bounds. */
    virtual EditRegion bounds(const BrushSample &sample) const = 0;
    /** @brief Sample. */
    virtual void sample(const BrushSample &sample, IBrushSampleSink &sink) const = 0;
};

/** @brief Circular weighted kernel using a non-owning falloff strategy. */
class EVENGINE_API_ORCHESTRATION CircleBrushKernel final : public IBrushKernel {
public:
    /** @brief Circle brush kernel. */
    explicit CircleBrushKernel(const IBrushFalloff *falloff = nullptr);
    /** @brief Sets the falloff. */
    void setFalloff(const IBrushFalloff *falloff) { falloff_ = falloff; }
    /** @brief Bounds. */
    EditRegion bounds(const BrushSample &sample) const override;
    /** @brief Sample. */
    void sample(const BrushSample &sample, IBrushSampleSink &sink) const override;
private:
    const IBrushFalloff *falloff_ = nullptr;
};

/** @brief Rotatable square kernel using a non-owning falloff strategy. */
class EVENGINE_API_ORCHESTRATION BoxBrushKernel final : public IBrushKernel {
public:
    /** @brief Box brush kernel. */
    explicit BoxBrushKernel(const IBrushFalloff *falloff = nullptr);
    /** @brief Sets the falloff. */
    void setFalloff(const IBrushFalloff *falloff) { falloff_ = falloff; }
    /** @brief Bounds. */
    EditRegion bounds(const BrushSample &sample) const override;
    /** @brief Sample. */
    void sample(const BrushSample &sample, IBrushSampleSink &sink) const override;
private:
    const IBrushFalloff *falloff_ = nullptr;
};

/** @brief Convenience sink for previews, tests and command construction. */
class EVENGINE_API_ORCHESTRATION BrushSampleBuffer final : public IBrushSampleSink {
public:
    /** @brief Emit. */
    void emit(int x, int y, float weight) override;
    /** @brief Clears . */
    void clear() { points_.clear(); }
    /** @brief Returns the size of . */
    int size() const { return static_cast<int>(points_.size()); }
    /** @brief Point. */
    const BrushPoint &point(int index) const;
private:
    std::vector<BrushPoint> points_;
};

}  // namespace eve::editor
