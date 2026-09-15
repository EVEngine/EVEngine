#pragma once

#include "common/Result.h"

#include <cstdint>
#include <string>

namespace ssq { class Table; }

namespace eve::ui {

/** @brief Widget families created by Pcg PhotoModeUtils. */
enum class PcgPhotoModeWidgetKind {
    Field = 0, Slider = 1, IntSlider = 2, DisplaySlider = 3, DisplayIntSlider = 4,
    Toggle = 5, Header = 6, Dropdown = 7, Label = 8, Button = 9, Color = 10,
    BannerImage = 11, Vector2 = 12, Vector3 = 13
};

/** @brief Result of parsing one Pcg photo-mode numeric input field. */
enum class PcgPhotoModeInputStatus { Ignored = 0, Applied = 1 };

/** @brief Backend-neutral state of one Pcg PhotoModeUIHelper prefab instance. */
class PcgPhotoModeRuntimeUI {
public:
    /** @brief Configure one widget family and publish its initial callback revision. */
    [[nodiscard]] Result<void> configure(int kind, const std::string &name, const std::string &value,
                                         float sliderValue, float minimum, float maximum,
                                         bool bannerImageFound = true);
    /** @brief Mark that the next slider update also refreshes its input text. */
    void markSliderUsed(bool value = true) noexcept { isUsingSlider_ = value; }
    /** @brief Apply a slider value and consume the input synchronization flag. */
    [[nodiscard]] Result<void> setSliderValue(float value);
    /** @brief Parse, clamp, and apply text using Pcg GetAndSetFloatValue semantics. */
    PcgPhotoModeInputStatus applyFloatInput(const std::string &value) noexcept;
    /** @brief Wrap metrics text with Pcg's exact 68-column algorithm. */
    static std::string updateWrap(const std::string &value);

    int getKind() const noexcept { return static_cast<int>(kind_); }
    const std::string &getName() const noexcept { return name_; }
    const std::string &getValueText() const noexcept { return valueText_; }
    float getValue() const noexcept { return value_; }
    float getMinimum() const noexcept { return minimum_; }
    float getMaximum() const noexcept { return maximum_; }
    bool getWholeNumbers() const noexcept { return wholeNumbers_; }
    bool getUsingSlider() const noexcept { return isUsingSlider_; }
    bool getInputRefresh() const noexcept { return inputRefresh_; }
    bool getLabelVisible() const noexcept { return labelVisible_; }
    bool getSecondLabelVisible() const noexcept { return secondLabelVisible_; }
    bool getSliderVisible() const noexcept { return sliderVisible_; }
    bool getInputVisible() const noexcept { return inputVisible_; }
    bool getToggleVisible() const noexcept { return toggleVisible_; }
    bool getButtonVisible() const noexcept { return buttonVisible_; }
    bool getDropdownVisible() const noexcept { return dropdownVisible_; }
    bool getImageVisible() const noexcept { return imageVisible_; }
    bool getHeaderVisible() const noexcept { return headerVisible_; }
    bool getColorVisible() const noexcept { return colorVisible_; }
    bool getVector2Visible() const noexcept { return vector2Visible_; }
    bool getVector3Visible() const noexcept { return vector3Visible_; }
    uint64_t getInitialCallbackRevision() const noexcept { return initialCallbackRevision_; }

private:
    PcgPhotoModeWidgetKind kind_ = PcgPhotoModeWidgetKind::Field;
    std::string name_;
    std::string valueText_;
    float value_ = 0.0f, minimum_ = 0.0f, maximum_ = 0.0f;
    bool wholeNumbers_ = false, isUsingSlider_ = true, inputRefresh_ = false;
    bool labelVisible_ = false, secondLabelVisible_ = false, sliderVisible_ = false;
    bool inputVisible_ = false, toggleVisible_ = false, buttonVisible_ = false;
    bool dropdownVisible_ = false, imageVisible_ = false, headerVisible_ = false;
    bool colorVisible_ = false, vector2Visible_ = false, vector3Visible_ = false;
    uint64_t initialCallbackRevision_ = 0;
};

/** @brief Register Pcg photo-mode runtime UI bindings. */
void exposePcgPhotoModeRuntimeUIBindings(ssq::Table &table);

} // namespace eve::ui
