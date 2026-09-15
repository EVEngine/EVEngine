#include "ui/PcgPhotoModeRuntimeUI.h"
#include "zeroerr/unittest.h"
using namespace eve::ui;

TEST_CASE("pcg.photoModeRuntimeUI.configuresWidgetFamilies") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Exposure", "1.5", 1.5f, 0.0f, 4.0f).ok());
    CHECK(ui.getLabelVisible()); CHECK(ui.getSliderVisible()); CHECK(ui.getInputVisible());
    CHECK(!ui.getButtonVisible()); CHECK(ui.getInitialCallbackRevision() == 1);
    REQUIRE(ui.configure(13, "Position", "", 0.0f, -10.0f, 10.0f).ok());
    CHECK(ui.getVector3Visible()); CHECK(!ui.getSliderVisible()); CHECK(ui.getInitialCallbackRevision() == 2);
    REQUIRE(ui.configure(11, "Banner", "", 0.0f, 0.0f, 0.0f, false).ok());
    CHECK(!ui.getImageVisible()); CHECK(ui.getInitialCallbackRevision() == 2);
}

TEST_CASE("pcg.photoModeRuntimeUI.preservesSliderInputSynchronization") {
    PcgPhotoModeRuntimeUI ui; REQUIRE(ui.configure(1, "Value", "2.0", 2.0f, 0.0f, 10.0f).ok());
    REQUIRE(ui.setSliderValue(3.5f).ok()); CHECK(ui.getInputRefresh()); CHECK(ui.getValueText() == "3.5"); CHECK(!ui.getUsingSlider());
    REQUIRE(ui.setSliderValue(4.0f).ok()); CHECK(!ui.getInputRefresh()); CHECK(ui.getValueText() == "3.5");
    ui.markSliderUsed(); REQUIRE(ui.setSliderValue(5.0f).ok()); CHECK(ui.getInputRefresh());
}

TEST_CASE("pcg.photoModeRuntimeUI.parsesAndClampsFloatInput") {
    PcgPhotoModeRuntimeUI ui; REQUIRE(ui.configure(1, "Value", "", 2.0f, -1.0f, 1.0f).ok());
    CHECK(ui.applyFloatInput("") == PcgPhotoModeInputStatus::Applied); CHECK(ui.getValue() == 0.0f);
    CHECK(ui.applyFloatInput("4.25") == PcgPhotoModeInputStatus::Applied); CHECK(ui.getValue() == 1.0f); CHECK(ui.getUsingSlider());
    CHECK(ui.applyFloatInput("4x") == PcgPhotoModeInputStatus::Ignored); CHECK(ui.getValue() == 1.0f);
}

TEST_CASE("pcg.photoModeRuntimeUI.matchesMetricsWrappingQuirks") {
    CHECK(PcgPhotoModeRuntimeUI::updateWrap(std::string(69, 'x')) == std::string(68, 'x') + "\n");
    const std::string spaced = std::string(60, 'a') + " " + std::string(20, 'b');
    CHECK(PcgPhotoModeRuntimeUI::updateWrap(spaced) == std::string(60, 'a') + "\n" + std::string(20, 'b'));
}
