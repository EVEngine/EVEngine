#include <fstream>
#include <limits>
#include "ui/PcgPhotoModeSession.h"
#include "ui/PcgPhotoModeValues.h"
#include "zeroerr/unittest.h"

// pcg_photo_mode_session.cpp
using namespace eve::ui;
namespace {
[[maybe_unused]] std::string profile(const char* scene, int64_t light, float fov) {
    PcgPhotoModeValues v;
    v.setString("m_lastSceneName", scene).value();
    v.setBool("m_isUsingPcgLighting", light >= 0).value();
    v.setInt("m_selectedPcgLightingProfile", light).value();
    v.setFloat("m_fieldOfView", fov).value();
    auto j = v.snapshotJson();
    j.value();
    return std::move(j).takeValue();
}
[[maybe_unused]] float fov(const std::string& j) {
    PcgPhotoModeValues v;
    v.restoreJson(j).value();
    auto x = v.getFloat("m_fieldOfView");
    x.value();
    return std::move(x).takeValue();
}
}  // namespace


// pcg_photo_mode_values.cpp
using namespace eve::ui;


TEST_CASE("ui.pcgPhotoModeValues.roundTripsAllFieldsAndRejectsUnknownSchema") {
    PcgPhotoModeValues source;
    REQUIRE(source.setBool("m_showFPS", true).ok());
    REQUIRE(source.setInt("m_targetFPS", 144).ok());
    REQUIRE(source.setFloat("m_pcgWindSpeed", 0.875f).ok());
    REQUIRE(source.setString("m_lastSceneName", "Alpine World").ok());
    REQUIRE(source.setColor("m_sunColor", 1.25f, .5f, .25f, 2.0f).ok());
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "1\\n";
    auto encoded = source.snapshotJson();
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "2\\n";
    REQUIRE(encoded.ok());
    PcgPhotoModeValues restored;
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "3\\n";
    REQUIRE(restored.restoreJson(encoded.value()).ok());
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "4\\n";
    std::ofstream(".local-debug/pvstage.txt", std::ios::app) << "5\\n";
    auto again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), encoded.value());
    auto fps  = restored.getInt("m_targetFPS");
    auto wind = restored.getFloat("m_pcgWindSpeed");
    REQUIRE(fps.ok());
    REQUIRE(wind.ok());
    CHECK_EQ(fps.value(), int64_t(144));
    CHECK_EQ(wind.value(), .875f);
    REQUIRE(restored.selectColor("m_sunColor").ok());
    CHECK_EQ(restored.getColorR(), 1.25f);
    CHECK_EQ(restored.getColorA(), 2.0f);
    const auto  before = again.value();
    std::string wrong  = before;
    auto        pos    = wrong.find("eve.ui.pcg-photo-mode-values");
    REQUIRE(pos != std::string::npos);
    wrong.replace(pos, 29, "eve.ui.wrong-photo-values");
    CHECK(!restored.restoreJson(wrong).ok());
    auto after = restored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before);
}
