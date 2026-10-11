#include "zeroerr/unittest.h"
#include "ui/PcgPhotoModeValues.h"
#include <limits>
#include <fstream>
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModeValues.containsCompleteExactDefaults") { PcgPhotoModeValues v; CHECK_EQ(v.getFieldCount(),uint64_t(102)); auto first=v.getFieldName(0);auto last=v.getFieldName(101);REQUIRE(first.ok());REQUIRE(last.ok());CHECK_EQ(first.value(),"m_isUsingPcgLighting");CHECK_EQ(last.value(),"m_cameraCellSubdivision"); auto fov=v.getFloat("m_fieldOfView");auto fps=v.getInt("m_targetFPS");auto png=v.getInt("m_screenshotImageFormat");auto load=v.getBool("m_loadSavedSettings");REQUIRE(fov.ok());REQUIRE(fps.ok());REQUIRE(png.ok());REQUIRE(load.ok());CHECK_EQ(fov.value(),60.0f);CHECK_EQ(fps.value(),int64_t(-1));CHECK_EQ(png.value(),int64_t(2));CHECK(load.value()); REQUIRE(v.selectColor("m_ambientSkyColor").ok());CHECK_EQ(v.getColorR(),0.7027151f);CHECK_EQ(v.getColorA(),0.4192761f); }
TEST_CASE("ui.pcgPhotoModeValues.enforcesSchemaTypesAtomically") { PcgPhotoModeValues v;REQUIRE(v.setFloat("m_fieldOfView",90.0f).ok());CHECK(!v.setInt("m_fieldOfView",90).ok());CHECK(!v.setFloat("m_fieldOfView",std::numeric_limits<float>::infinity()).ok());auto f=v.getFloat("m_fieldOfView");REQUIRE(f.ok());CHECK_EQ(f.value(),90.0f);CHECK(!v.setBool("missing",true).ok());CHECK(!v.getFieldName(102).ok());REQUIRE(v.setColor("m_fogColor",.1f,.2f,.3f,.4f).ok());REQUIRE(v.selectColor("m_fogColor").ok());CHECK_EQ(v.getColorB(),.3f);v.resetDefaults();REQUIRE(v.selectColor("m_fogColor").ok());CHECK_EQ(v.getColorR(),0.0f);CHECK_EQ(v.getColorA(),1.0f); }

TEST_CASE("ui.pcgPhotoModeValues.roundTripsAllFieldsAndRejectsUnknownSchema") {
    PcgPhotoModeValues source;
    REQUIRE(source.setBool("m_showFPS",true).ok());
    REQUIRE(source.setInt("m_targetFPS",144).ok());
    REQUIRE(source.setFloat("m_pcgWindSpeed",0.875f).ok());
    REQUIRE(source.setString("m_lastSceneName","Alpine World").ok());
    REQUIRE(source.setColor("m_sunColor",1.25f,.5f,.25f,2.0f).ok());
    std::ofstream(".local-debug/pvstage.txt",std::ios::app)<<"1\\n";auto encoded=source.snapshotJson();std::ofstream(".local-debug/pvstage.txt",std::ios::app)<<"2\\n";REQUIRE(encoded.ok());
    PcgPhotoModeValues restored;std::ofstream(".local-debug/pvstage.txt",std::ios::app)<<"3\\n";REQUIRE(restored.restoreJson(encoded.value()).ok());std::ofstream(".local-debug/pvstage.txt",std::ios::app)<<"4\\n";
    std::ofstream(".local-debug/pvstage.txt",std::ios::app)<<"5\\n";auto again=restored.snapshotJson();REQUIRE(again.ok());CHECK_EQ(again.value(),encoded.value());
    auto fps=restored.getInt("m_targetFPS");auto wind=restored.getFloat("m_pcgWindSpeed");REQUIRE(fps.ok());REQUIRE(wind.ok());CHECK_EQ(fps.value(),int64_t(144));CHECK_EQ(wind.value(),.875f);
    REQUIRE(restored.selectColor("m_sunColor").ok());CHECK_EQ(restored.getColorR(),1.25f);CHECK_EQ(restored.getColorA(),2.0f);
    const auto before=again.value();std::string wrong=before;auto pos=wrong.find("eve.ui.pcg-photo-mode-values");REQUIRE(pos!=std::string::npos);wrong.replace(pos,29,"eve.ui.wrong-photo-values");CHECK(!restored.restoreJson(wrong).ok());auto after=restored.snapshotJson();REQUIRE(after.ok());CHECK_EQ(after.value(),before);
}
