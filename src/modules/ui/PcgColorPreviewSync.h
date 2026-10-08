#pragma once

#include "common/Export.h"
#include "common/Result.h"
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Caller-owned highlighted-color snapshot ported from Pcg ColorPreviewSync. */
class EVENGINE_API_WORLD PcgColorPreviewSync {
public:
    /** @brief Synchronize from the source image color exactly as Pcg does on enable. */
    [[nodiscard]] Result<void> sync(float red, float green, float blue, float alpha);
    /** @brief Return highlighted red. */ float getRed() const noexcept { return red_; }
    /** @brief Return highlighted green. */ float getGreen() const noexcept { return green_; }
    /** @brief Return highlighted blue. */ float getBlue() const noexcept { return blue_; }
    /** @brief Return highlighted alpha. */ float getAlpha() const noexcept { return alpha_; }
private:
    float red_ = 1.f, green_ = 1.f, blue_ = 1.f, alpha_ = 1.f;
};
/** @brief Register Pcg color-preview bindings. */
void exposePcgColorPreviewSyncBindings(ssq::Table& table);
}
