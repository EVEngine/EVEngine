#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include <string>
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Caller-owned notification state ported from Pcg ScreenshotSavedManager. */
class EVENGINE_API_WORLD PcgScreenshotSavedNotice {
public:
 /** @brief Configure enable state and visible duration atomically. */ [[nodiscard]] Result<void> configure(bool enabled,float showSeconds);
 /** @brief Cancel an old notice and schedule a new one for the next end-of-frame. */ [[nodiscard]] Result<void> request(const std::string& savedPath);
 /** @brief Publish a pending notice after rendering this frame. */ [[nodiscard]] Result<void> endFrame(float unscaledNow);
 /** @brief Advance expiration using explicit unscaled time. */ [[nodiscard]] Result<void> tick(float unscaledNow);
 /** @brief Return whether the saved label is visible. */ bool getVisible()const noexcept{return visible_;}
 /** @brief Return whether the path label is visible. */ bool getPathVisible()const noexcept{return visible_&&!path_.empty();}
 /** @brief Return last saved path. */ const std::string& getPath()const noexcept{return path_;}
 /** @brief Return whether publication is waiting for end-of-frame. */ bool getPending()const noexcept{return pending_;}
private:bool enabled_=true,visible_=false,pending_=false;float duration_=1.f,hideAt_=0.f;std::string path_;
};
/** @brief Register Pcg screenshot-notice bindings. */ void exposePcgScreenshotSavedNoticeBindings(ssq::Table& table);
}
