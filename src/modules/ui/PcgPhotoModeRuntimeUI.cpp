#include "ui/PcgPhotoModeRuntimeUI.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>

namespace eve::ui {
namespace { Result<void> invalid(const char *m) { return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, m, {}, {}, "ui.pcgPhotoModeRuntimeUI")); } }

Result<void> PcgPhotoModeRuntimeUI::configure(int rawKind, const std::string &name, const std::string &text,
                                                float value, float minimum, float maximum, bool imageFound) {
    if (rawKind < 0 || rawKind > 13) return invalid("photo-mode widget kind must be in range 0..13");
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        return invalid("photo-mode slider values must be finite and minimum must not exceed maximum");
    kind_ = static_cast<PcgPhotoModeWidgetKind>(rawKind); name_ = name; valueText_ = text;
    value_ = value; minimum_ = minimum; maximum_ = maximum;
    wholeNumbers_ = kind_ == PcgPhotoModeWidgetKind::IntSlider || kind_ == PcgPhotoModeWidgetKind::DisplayIntSlider;
    isUsingSlider_ = true; inputRefresh_ = false;
    labelVisible_ = secondLabelVisible_ = sliderVisible_ = inputVisible_ = false;
    toggleVisible_ = buttonVisible_ = dropdownVisible_ = imageVisible_ = false;
    headerVisible_ = colorVisible_ = vector2Visible_ = vector3Visible_ = false;
    switch (kind_) {
    case PcgPhotoModeWidgetKind::Field: labelVisible_ = inputVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Slider: case PcgPhotoModeWidgetKind::IntSlider: labelVisible_ = sliderVisible_ = inputVisible_ = true; break;
    case PcgPhotoModeWidgetKind::DisplaySlider: case PcgPhotoModeWidgetKind::DisplayIntSlider: labelVisible_ = secondLabelVisible_ = sliderVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Toggle: labelVisible_ = toggleVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Header: headerVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Dropdown: labelVisible_ = dropdownVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Label: labelVisible_ = secondLabelVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Button: labelVisible_ = buttonVisible_ = true; break;
    case PcgPhotoModeWidgetKind::Color: labelVisible_ = colorVisible_ = true; break;
    case PcgPhotoModeWidgetKind::BannerImage: imageVisible_ = imageFound; break;
    case PcgPhotoModeWidgetKind::Vector2: labelVisible_ = vector2Visible_ = true; break;
    case PcgPhotoModeWidgetKind::Vector3: labelVisible_ = vector3Visible_ = true; break;
    }
    if (kind_ == PcgPhotoModeWidgetKind::Field || (rawKind >= 1 && rawKind <= 5) ||
        kind_ == PcgPhotoModeWidgetKind::Dropdown || kind_ == PcgPhotoModeWidgetKind::Vector2 || kind_ == PcgPhotoModeWidgetKind::Vector3)
        ++initialCallbackRevision_;
    return Result<void>::success();
}

Result<void> PcgPhotoModeRuntimeUI::setSliderValue(float value) {
    if (!std::isfinite(value)) return invalid("photo-mode slider value must be finite");
    value_ = value; inputRefresh_ = isUsingSlider_;
    if (inputRefresh_) { std::ostringstream s; s.imbue(std::locale::classic()); s << value; valueText_ = s.str(); }
    isUsingSlider_ = false; return Result<void>::success();
}

PcgPhotoModeInputStatus PcgPhotoModeRuntimeUI::applyFloatInput(const std::string &text) noexcept {
    std::istringstream s(text.empty() ? "0" : text); s.imbue(std::locale::classic()); float parsed = 0.0f;
    s >> parsed; if (s.fail() || !std::isfinite(parsed)) return PcgPhotoModeInputStatus::Ignored;
    s >> std::ws; if (!s.eof()) return PcgPhotoModeInputStatus::Ignored;
    if (parsed < minimum_ || parsed > maximum_) isUsingSlider_ = true;
    value_ = std::clamp(parsed, minimum_, maximum_); return PcgPhotoModeInputStatus::Applied;
}

std::string PcgPhotoModeRuntimeUI::updateWrap(const std::string &text) {
    constexpr size_t wrap = 68; int sanity = 9000; std::string result; size_t p = 0;
    while (p < text.size() && --sanity > 0) {
        if (p + wrap >= text.size()) { result.append(text.substr(p)); break; }
        size_t edit = text.rfind(' ', p + wrap);
        if (edit == std::string::npos || edit <= p) edit = p + wrap;
        result.append(text.substr(p, edit - p)); result.push_back('\n'); p = edit + 1;
    }
    return result;
}

void exposePcgPhotoModeRuntimeUIBindings(ssq::Table &table) {
    auto vm = table.getHandle(); auto c = table.addClass("PcgPhotoModeRuntimeUI", ssq::Class::Ctor<PcgPhotoModeRuntimeUI()>());
    c.addFunc("configure", [vm](PcgPhotoModeRuntimeUI *s, int k, const std::string &n, const std::string &t, float v, float lo, float hi, bool found) { return eve::script::projectResult(vm, s->configure(k, n, t, v, lo, hi, found)); });
    c.addFunc("markSliderUsed", [](PcgPhotoModeRuntimeUI *s, bool v) { s->markSliderUsed(v); });
    c.addFunc("setSliderValue", [vm](PcgPhotoModeRuntimeUI *s, float v) { return eve::script::projectResult(vm, s->setSliderValue(v)); });
    c.addFunc("applyFloatInput", [](PcgPhotoModeRuntimeUI *s, const std::string &v) { return s->applyFloatInput(v) == PcgPhotoModeInputStatus::Applied; });
    c.addFunc("updateWrap", [](PcgPhotoModeRuntimeUI *, const std::string &v) { return PcgPhotoModeRuntimeUI::updateWrap(v); });
    c.addFunc("getKind", [](const PcgPhotoModeRuntimeUI* s){ return s->getKind(); });
    c.addFunc("getName", [](const PcgPhotoModeRuntimeUI* s){ return s->getName(); });
    c.addFunc("getValueText", [](const PcgPhotoModeRuntimeUI* s){ return s->getValueText(); });
    c.addFunc("getValue", [](const PcgPhotoModeRuntimeUI* s){ return s->getValue(); });
    c.addFunc("getMinimum", [](const PcgPhotoModeRuntimeUI* s){ return s->getMinimum(); });
    c.addFunc("getMaximum", [](const PcgPhotoModeRuntimeUI* s){ return s->getMaximum(); });
    c.addFunc("getWholeNumbers", [](const PcgPhotoModeRuntimeUI* s){ return s->getWholeNumbers(); });
    c.addFunc("getUsingSlider", [](const PcgPhotoModeRuntimeUI* s){ return s->getUsingSlider(); });
    c.addFunc("getInputRefresh", [](const PcgPhotoModeRuntimeUI* s){ return s->getInputRefresh(); });
    c.addFunc("getLabelVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getLabelVisible(); });
    c.addFunc("getSecondLabelVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getSecondLabelVisible(); });
    c.addFunc("getSliderVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getSliderVisible(); });
    c.addFunc("getInputVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getInputVisible(); });
    c.addFunc("getToggleVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getToggleVisible(); });
    c.addFunc("getButtonVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getButtonVisible(); });
    c.addFunc("getDropdownVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getDropdownVisible(); });
    c.addFunc("getImageVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getImageVisible(); });
    c.addFunc("getHeaderVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getHeaderVisible(); });
    c.addFunc("getColorVisible", [](const PcgPhotoModeRuntimeUI* s){ return s->getColorVisible(); });
    c.addFunc("getVector2Visible", [](const PcgPhotoModeRuntimeUI* s){ return s->getVector2Visible(); });
    c.addFunc("getVector3Visible", [](const PcgPhotoModeRuntimeUI* s){ return s->getVector3Visible(); });
    c.addFunc("getInitialCallbackRevision", [](const PcgPhotoModeRuntimeUI* s){ return s->getInitialCallbackRevision(); });
}
} // namespace eve::ui
