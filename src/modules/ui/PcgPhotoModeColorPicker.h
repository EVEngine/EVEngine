#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "ui/PcgUiStatus.h"
#include <cstdint>
#include <string>
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Caller-owned runtime model of Pcg PhotoModeColorPicker. */
class EVENGINE_API_WORLD PcgPhotoModeColorPicker {
public:
 /** @brief Open with a source color and preserve Pcg's HDR preprocessing. */ [[nodiscard]] Result<void> open(float r,float g,float b,float a,bool hdr);
 /** @brief Store the color restored by reset. */ [[nodiscard]] Result<void> setLast(float r,float g,float b,float a);
 /** @brief Set the focused display name, exposed with Pcg parentheses. */ void setFocusedName(const std::string& name){focusedName_="("+name+")";}
 /** @brief Close the picker. */ void close()noexcept{open_=false;}
 /** @brief Set one raw channel and publish one change. */ [[nodiscard]] Result<void> setRed(float v);
 /** @brief Set one raw channel and publish one change. */ [[nodiscard]] Result<void> setGreen(float v);
 /** @brief Set one raw channel and publish one change. */ [[nodiscard]] Result<void> setBlue(float v);
 /** @brief Set HDR alpha and publish one change. */ [[nodiscard]] Result<void> setHdr(float v);
 /** @brief Restore last RGBA and reproduce four slider events plus explicit callback. */ void reset()noexcept;
 /** @brief Return raw channel. */ float getRed()const noexcept{return r_;} float getGreen()const noexcept{return g_;} float getBlue()const noexcept{return b_;} float getAlpha()const noexcept{return a_;}
 /** @brief Return displayed/applied RGB after optional HDR multiplication. */ float getPreviewRed()const noexcept; float getPreviewGreen()const noexcept; float getPreviewBlue()const noexcept;
 /** @brief Return HDR slider grayscale swatch. */ float getHdrSwatch()const noexcept;
 /** @brief Return whether HDR controls are active. */ bool getHdrEnabled()const noexcept{return hdr_;}
 /** @brief Return whether picker is open. */ bool getOpen()const noexcept{return open_;}
 /** @brief Return focused name including parentheses. */ const std::string& getFocusedName()const noexcept{return focusedName_;}
 /** @brief Return change callback revision. */ uint64_t getChangeRevision()const noexcept{return revision_;}
 /** @brief Return and clear the SetAsLastSibling request. */ PcgUiRequestStatus consumeBringToFront()noexcept;
private:bool hdrActive()const noexcept;Result<void> setChannel(float& dst,float value);float r_=0,g_=0,b_=0,a_=1,lastR_=0,lastG_=0,lastB_=0,lastA_=1;bool hdr_=false,open_=false,front_=false;uint64_t revision_=0;std::string focusedName_;
};
/** @brief Register Pcg photo-mode color-picker bindings. */ void exposePcgPhotoModeColorPickerBindings(ssq::Table& table);
}
