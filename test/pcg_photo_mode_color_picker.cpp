#include <cmath>
#include <limits>
#include "ui/PcgPhotoModeColorPicker.h"
#include "ui/PcgPhotoModePanels.h"
#include "ui/PcgPhotoModeRanges.h"
#include "ui/PcgPhotoModeRuntimeUI.h"
#include "zeroerr/unittest.h"

// pcg_photo_mode_color_picker.cpp
using eve::ui::PcgPhotoModeColorPicker;
TEST_CASE("ui.pcgPhotoModeColorPicker.preservesHdrDoubleApplication") {
    PcgPhotoModeColorPicker p;
    REQUIRE(p.open(.2f, .3f, .4f, 2.f, true).ok());
    CHECK_EQ(p.getRed(), .4f);
    CHECK_EQ(p.getPreviewRed(), .8f);
    CHECK_EQ(p.getPreviewGreen(), 1.2f);
    CHECK_EQ(p.getPreviewBlue(), 1.6f);
    CHECK_EQ(p.getHdrSwatch(), .7f);
    CHECK(p.getOpen());
    CHECK_EQ(p.consumeBringToFront(), eve::ui::PcgUiRequestStatus::Requested);
    CHECK_EQ(p.consumeBringToFront(), eve::ui::PcgUiRequestStatus::None);
}
TEST_CASE("ui.pcgPhotoModeColorPicker.slidersResetAndNegativeHdr") {
    PcgPhotoModeColorPicker p;
    REQUIRE(p.setLast(.1f, .2f, .3f, -2.f).ok());
    REQUIRE(p.open(1, 1, 1, 1, true).ok());
    REQUIRE(p.setRed(.7f).ok());
    REQUIRE(p.setGreen(.8f).ok());
    REQUIRE(p.setBlue(.9f).ok());
    REQUIRE(p.setHdr(3.f).ok());
    CHECK_EQ(p.getChangeRevision(), uint64_t(4));
    p.reset();
    CHECK_EQ(p.getRed(), .1f);
    CHECK_EQ(p.getAlpha(), -2.f);
    CHECK_EQ(p.getPreviewRed(), -.2f);
    CHECK_EQ(p.getChangeRevision(), uint64_t(9));
    p.setFocusedName("Fog");
    CHECK_EQ(p.getFocusedName(), "(Fog)");
    p.close();
    CHECK(!p.getOpen());
}
TEST_CASE("ui.pcgPhotoModeColorPicker.invalidValuesAreAtomic") {
    PcgPhotoModeColorPicker p;
    REQUIRE(p.open(.1f, .2f, .3f, 1, false).ok());
    CHECK(!p.open(std::numeric_limits<float>::infinity(), 0, 0, 1, true).ok());
    CHECK_EQ(p.getRed(), .1f);
    CHECK(!p.setHdr(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(p.getAlpha(), 1.f);
}

// pcg_photo_mode_panels.cpp
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModePanels.selectsExactlyOneAndResetsScroll") {
    PcgPhotoModePanels p;
    REQUIRE(p.select(4).ok());
    CHECK_EQ(p.getSelected(), 4);
    for (int i = 0; i < 7; ++i) CHECK_EQ(p.isActive(i), i == 4);
    CHECK_EQ(p.consumeScrollReset(), PcgUiRequestStatus::Requested);
    CHECK_EQ(p.consumeScrollReset(), PcgUiRequestStatus::None);
    CHECK_EQ(p.getRevision(), uint64_t(1));
    REQUIRE(p.select(6).ok());
    CHECK(p.isActive(6));
}
TEST_CASE("ui.pcgPhotoModePanels.invalidSelectionIsAtomic") {
    PcgPhotoModePanels p;
    REQUIRE(p.select(2).ok());
    auto rev = p.getRevision();
    CHECK(!p.select(7).ok());
    CHECK_EQ(p.getSelected(), 2);
    CHECK_EQ(p.getRevision(), rev);
    CHECK(p.isActive(2));
}
TEST_CASE("ui.pcgPhotoModePanelButton.matchesColorsAndLayout") {
    PcgPhotoModePanelButton b;
    REQUIRE(b.configure(.1f, .2f, .3f, .4f, .8f, .7f, .6f, .5f).ok());
    b.setSelected(false);
    CHECK_EQ(b.getNormalR(), .1f);
    CHECK_EQ(b.getHighlightR(), .8f);
    CHECK(!b.getLayoutEnabled());
    b.setSelected(true);
    CHECK_EQ(b.getNormalR(), .8f);
    CHECK_EQ(b.getNormalA(), .5f);
    CHECK(b.getLayoutEnabled());
    CHECK(!b.configure(std::numeric_limits<float>::quiet_NaN(), 0, 0, 1, 1, 1, 0, 1).ok());
    CHECK_EQ(b.getNormalR(), .8f);
}

// pcg_photo_mode_ranges.cpp
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModeRanges.containsAllExactDefaults") {
    PcgPhotoModeRanges r;
    CHECK_EQ(r.getCount(), uint64_t(57));
    auto first = r.getName(0);
    auto last  = r.getName(56);
    REQUIRE(first.ok());
    REQUIRE(last.ok());
    CHECK_EQ(first.value(), "m_0To1");
    CHECK_EQ(last.value(), "m_cameraCellSubdivision");
    REQUIRE(r.select("m_fieldOfView").ok());
    CHECK_EQ(r.getMinimum(), 1.0);
    CHECK_EQ(r.getMaximum(), 115.0);
    CHECK(!r.getIntegral());
    REQUIRE(r.select("m_cameraCellSubdivision").ok());
    CHECK_EQ(r.getMinimum(), -8.0);
    CHECK_EQ(r.getMaximum(), 8.0);
    CHECK(r.getIntegral());
    REQUIRE(r.select("m_densityVolumeFogDistance").ok());
    CHECK(std::isinf(r.getMaximum()));
}
TEST_CASE("ui.pcgPhotoModeRanges.clampsAndUpdatesAtomically") {
    PcgPhotoModeRanges r;
    auto               c = r.clamp("m_fieldOfView", 200);
    REQUIRE(c.ok());
    CHECK_EQ(c.value(), 115.0);
    REQUIRE(r.setRange("m_fieldOfView", 10, 80).ok());
    c = r.clamp("m_fieldOfView", 5);
    REQUIRE(c.ok());
    CHECK_EQ(c.value(), 10.0);
    CHECK(!r.setRange("m_fieldOfView", 90, 20).ok());
    REQUIRE(r.select("m_fieldOfView").ok());
    CHECK_EQ(r.getMaximum(), 80.0);
    CHECK(!r.setRange("m_targetFPS", 0.5, 60).ok());
    CHECK(!r.clamp("missing", 1).ok());
}

// pcg_photo_mode_runtime_ui.cpp
using namespace eve::ui;

TEST_CASE("pcg.photoModeRuntimeUI.configuresWidgetFamilies") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Exposure", "1.5", 1.5f, 0.0f, 4.0f).ok());
    CHECK(ui.getLabelVisible());
    CHECK(ui.getSliderVisible());
    CHECK(ui.getInputVisible());
    CHECK(!ui.getButtonVisible());
    CHECK(ui.getInitialCallbackRevision() == 1);
    REQUIRE(ui.configure(13, "Position", "", 0.0f, -10.0f, 10.0f).ok());
    CHECK(ui.getVector3Visible());
    CHECK(!ui.getSliderVisible());
    CHECK(ui.getInitialCallbackRevision() == 2);
    REQUIRE(ui.configure(11, "Banner", "", 0.0f, 0.0f, 0.0f, false).ok());
    CHECK(!ui.getImageVisible());
    CHECK(ui.getInitialCallbackRevision() == 2);
}

TEST_CASE("pcg.photoModeRuntimeUI.preservesSliderInputSynchronization") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Value", "2.0", 2.0f, 0.0f, 10.0f).ok());
    REQUIRE(ui.setSliderValue(3.5f).ok());
    CHECK(ui.getInputRefresh());
    CHECK(ui.getValueText() == "3.5");
    CHECK(!ui.getUsingSlider());
    REQUIRE(ui.setSliderValue(4.0f).ok());
    CHECK(!ui.getInputRefresh());
    CHECK(ui.getValueText() == "3.5");
    ui.markSliderUsed();
    REQUIRE(ui.setSliderValue(5.0f).ok());
    CHECK(ui.getInputRefresh());
}

TEST_CASE("pcg.photoModeRuntimeUI.parsesAndClampsFloatInput") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Value", "", 2.0f, -1.0f, 1.0f).ok());
    CHECK(ui.applyFloatInput("") == PcgPhotoModeInputStatus::Applied);
    CHECK(ui.getValue() == 0.0f);
    CHECK(ui.applyFloatInput("4.25") == PcgPhotoModeInputStatus::Applied);
    CHECK(ui.getValue() == 1.0f);
    CHECK(ui.getUsingSlider());
    CHECK(ui.applyFloatInput("4x") == PcgPhotoModeInputStatus::Ignored);
    CHECK(ui.getValue() == 1.0f);
}

TEST_CASE("pcg.photoModeRuntimeUI.matchesMetricsWrappingQuirks") {
    CHECK(PcgPhotoModeRuntimeUI::updateWrap(std::string(69, 'x')) == std::string(68, 'x') + "\n");
    const std::string spaced = std::string(60, 'a') + " " + std::string(20, 'b');
    CHECK(PcgPhotoModeRuntimeUI::updateWrap(spaced) == std::string(60, 'a') + "\n" + std::string(20, 'b'));
}
