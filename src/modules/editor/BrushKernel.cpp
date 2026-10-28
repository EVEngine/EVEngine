#include "editor/BrushKernel.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::editor {
namespace {
ConstantBrushFalloff defaultFalloff;
float weightAt(const IBrushFalloff *falloff, float distance) {
    return std::clamp((falloff ? falloff : &defaultFalloff)->evaluate(distance), 0.f, 1.f);
}
EditRegion squareBounds(const BrushSample &sample) {
    const float radius = std::max(0.f, sample.radius);
    EditRegion region;
    region.minX = static_cast<int>(std::floor(sample.centerX - radius));
    region.minY = static_cast<int>(std::floor(sample.centerY - radius));
    region.maxX = static_cast<int>(std::ceil(sample.centerX + radius));
    region.maxY = static_cast<int>(std::ceil(sample.centerY + radius));
    return region;
}
}  // namespace

float ConstantBrushFalloff::evaluate(float normalizedDistance) const {
    return normalizedDistance <= 1.f ? 1.f : 0.f;
}
float LinearBrushFalloff::evaluate(float normalizedDistance) const {
    return std::clamp(1.f - normalizedDistance, 0.f, 1.f);
}
float SmoothBrushFalloff::evaluate(float normalizedDistance) const {
    const float t = std::clamp(1.f - normalizedDistance, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

CircleBrushKernel::CircleBrushKernel(const IBrushFalloff *falloff) : falloff_(falloff) {}
EditRegion CircleBrushKernel::bounds(const BrushSample &sample) const { return squareBounds(sample); }
void CircleBrushKernel::sample(const BrushSample &sample, IBrushSampleSink &sink) const {
    const float radius = std::max(0.f, sample.radius);
    const EditRegion region = bounds(sample);
    for (int y = region.minY; y <= region.maxY; ++y) {
        for (int x = region.minX; x <= region.maxX; ++x) {
            const float dx = static_cast<float>(x) - sample.centerX;
            const float dy = static_cast<float>(y) - sample.centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) continue;
            sink.emit(x, y, weightAt(falloff_, radius > 0.f ? distance / radius : 0.f));
        }
    }
}

BoxBrushKernel::BoxBrushKernel(const IBrushFalloff *falloff) : falloff_(falloff) {}
EditRegion BoxBrushKernel::bounds(const BrushSample &sample) const { return squareBounds(sample); }
void BoxBrushKernel::sample(const BrushSample &sample, IBrushSampleSink &sink) const {
    const float radius = std::max(0.f, sample.radius);
    const float c = std::cos(-sample.rotation);
    const float s = std::sin(-sample.rotation);
    const EditRegion region = bounds(sample);
    for (int y = region.minY; y <= region.maxY; ++y) {
        for (int x = region.minX; x <= region.maxX; ++x) {
            const float dx = static_cast<float>(x) - sample.centerX;
            const float dy = static_cast<float>(y) - sample.centerY;
            const float localX = c * dx - s * dy;
            const float localY = s * dx + c * dy;
            const float distance = std::max(std::abs(localX), std::abs(localY));
            if (distance > radius) continue;
            sink.emit(x, y, weightAt(falloff_, radius > 0.f ? distance / radius : 0.f));
        }
    }
}

void BrushSampleBuffer::emit(int x, int y, float weight) { points_.push_back({x, y, weight}); }
const BrushPoint &BrushSampleBuffer::point(int index) const {
    if (index < 0 || index >= size()) throw Exception("BrushSampleBuffer::point: bad index");
    return points_[static_cast<size_t>(index)];
}

}  // namespace eve::editor
