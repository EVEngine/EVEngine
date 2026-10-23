#pragma once

#include "common/Export.h"
#include "common/Result.h"
namespace ssq { class Table; }
namespace eve::procgen {
/** @brief Terrain quality values selected by Pcg's six-level frame-rate manager. */
struct PcgTerrainQualityPreset {
 float treeDistance=250,treeBillboardDistance=30,treeCrossFadeLength=5; int treeMaximumFullLodCount=5;
 float detailObjectDistance=40,detailObjectDensity=.1f,heightmapPixelError=20; int heightmapMaximumLod=1; float basemapDistance=100;
};
/** @brief Caller-owned deterministic Pcg frame-rate sampling and quality policy. */
class EVENGINE_API_DOMAINS PcgFrameRateManager {
public:
 /** @brief Configure target, check interval, inclusive quality range, and initial level atomically. */
 [[nodiscard]] Result<void> configure(int target,float interval,int minimum,int maximum,int current);
 /** @brief Add one rendered frame from caller-provided timing and return the selected level. */
 [[nodiscard]] Result<int> update(float dt,float timeScale);
 /** @brief Enable or disable automatic changes. */ void setAutomatic(bool v) noexcept { automatic_=v; }
 /** @brief Return whether automatic changes are enabled. */ bool getAutomatic()const noexcept{return automatic_;}
 /** @brief Select a level and disable automatic changes. */ [[nodiscard]] Result<void> selectManualQuality(int q);
 /** @brief Return current quality. */ int getQuality()const noexcept{return quality_;}
 /** @brief Return last completed half-second FPS sample. */ float getFps()const noexcept{return fps_;}
 /** @brief Return whether the last operation changed quality. */ bool getQualityChanged()const noexcept{return changed_;}
 /** @brief Copy the current Pcg terrain preset. */ PcgTerrainQualityPreset getPreset()const;
private:
 int target_=60,min_=0,max_=5,quality_=0,frames_=0; float interval_=10,left_=10,fpsLeft_=.5f,accum_=0,fps_=0; bool automatic_=true,changed_=false;
};
/** @brief Register Pcg frame-rate manager bindings. */ EVENGINE_API_DOMAINS void exposePcgFrameRateManagerBindings(
    ssq::Table& table);
}  // namespace eve::procgen
