#include "snow_editing/SnowFieldTarget.h"

#include "snow/SnowField.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::snow_editing {

SnowFieldTarget::SnowFieldTarget(std::string id, snow::SnowField* field)
    : id_(std::move(id)), field_(field) {}
int SnowFieldTarget::width() const { return field_ ? field_->getWidth() : 0; }
int SnowFieldTarget::height() const { return field_ ? field_->getHeight() : 0; }
bool SnowFieldTarget::containsCell(int x, int y) const { return field_ && field_->inBounds(x, y); }
float SnowFieldTarget::readScalar(int x, int y) const {
    return containsCell(x, y) ? field_->height(x, y) : 0.0F;
}
editing::FieldWriteStatus SnowFieldTarget::writeScalar(int x, int y, float value) {
    if (!containsCell(x, y) || !std::isfinite(value)) return editing::FieldWriteStatus::Rejected;
    value = std::clamp(value, 0.0F, 1.0F);
    if (field_->height(x, y) == value) return editing::FieldWriteStatus::Unchanged;
    field_->setHeight(x, y, value);
    ++revision_;
    dirty_.include(x, y);
    return editing::FieldWriteStatus::Applied;
}
float SnowFieldTarget::sampleScalar(float x, float y) const {
    if (!field_ || width() <= 0 || height() <= 0 || !std::isfinite(x) || !std::isfinite(y)) return 0.0F;
    x = std::clamp(x, 0.0F, static_cast<float>(width() - 1));
    y = std::clamp(y, 0.0F, static_cast<float>(height() - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width() - 1);
    const int y1 = std::min(y0 + 1, height() - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float top = readScalar(x0, y0) + (readScalar(x1, y0) - readScalar(x0, y0)) * tx;
    const float bottom = readScalar(x0, y1) + (readScalar(x1, y1) - readScalar(x0, y1)) * tx;
    return top + (bottom - top) * ty;
}

}  // namespace eve::snow_editing
