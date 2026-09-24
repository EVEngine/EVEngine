#pragma once

#include <cstdint>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
#include "ui/PcgPhotoModeValues.h"
namespace ssq { class Table; }
namespace eve::ui {
enum class PcgPhotoModeLoadDecision { Current=0, Saved=1, SaveCurrent=2 };
/** @brief Transactional lifecycle state for Pcg PhotoMode setup, disable and destroy. */
class EVENGINE_API_WORLD PcgPhotoModeSession {
public:
 /** @brief Begin from captured engine values and optionally select a compatible saved profile. */
 [[nodiscard]] Result<void> begin(const std::string&capturedJson,const std::string&savedJson,bool loadSaved,bool savedEver,int currentPipeline,int savedPipeline,const std::string&sceneName,int lightingProfile);
 /** @brief Replace the working values after a UI or engine edit. */ [[nodiscard]] Result<void> replaceWorkingJson(const std::string&json);
 /** @brief End the session; restore captured settings only when both flags are true. */ [[nodiscard]] Result<void> end(bool resetOnDisable,bool applicationPlaying);
 /** @brief Encode the currently selected working profile. */ [[nodiscard]] Result<std::string> workingJson()const;
 /** @brief Encode values that the caller must apply after end. */ [[nodiscard]] Result<std::string> outputJson()const;
 bool getActive()const noexcept{return active_;} bool getRestoreRequested()const noexcept{return restoreRequested_;}
 bool getRemovePhotoCameraRequested()const noexcept{return removePhotoCameraRequested_;}
 bool getUnfreezePlayerRequested()const noexcept{return unfreezePlayerRequested_;}
 int getLoadDecision()const noexcept{return static_cast<int>(loadDecision_);} uint64_t getRevision()const noexcept{return revision_;}
private: PcgPhotoModeValues captured_,working_,output_;bool active_=false,restoreRequested_=false,removePhotoCameraRequested_=false,unfreezePlayerRequested_=false;PcgPhotoModeLoadDecision loadDecision_=PcgPhotoModeLoadDecision::Current;uint64_t revision_=0;
};
/** @brief Register Pcg photo-mode session bindings. */ void exposePcgPhotoModeSessionBindings(ssq::Table&table);
}
