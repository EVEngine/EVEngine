#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include <cstdint>
#include <string>
#include <vector>
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Stored Pcg tooltip profile row. */ struct PcgTooltipEntry{std::string id,header,text;};
/** @brief Caller-owned runtime model of Pcg Tooltip, TooltipManager, Profile and Trigger. */
class EVENGINE_API_WORLD PcgTooltipManager {
public:
 /** @brief Configure interaction mode (0 UI, 1 scene, 2 both), theme (0 light, 1 dark), delay and layout. */ [[nodiscard]] Result<void> configure(int interactionMode,int theme,float delay,float bottomOffset,float topOffset,int wrapLimit);
 /** @brief Append a tooltip profile entry. */ [[nodiscard]] Result<void> addTooltip(const std::string&id,const std::string&header,const std::string&text);
 /** @brief Remove first entry whose id contains the query. */ [[nodiscard]] Result<bool> removeTooltip(const std::string&query);
 /** @brief Schedule the first profile entry whose id contains query. source is 0 UI or 1 scene. */ [[nodiscard]] Result<bool> enter(int source,const std::string&query,float now);
 /** @brief Schedule direct trigger content, matching TooltipTrigger. */ [[nodiscard]] Result<void> enterText(int source,const std::string&content,const std::string&header,float now);
 /** @brief Cancel matching hover and hide tooltip. */ [[nodiscard]] Result<void> exit(int source);
 /** @brief Update mouse placement, delayed visibility, and input dismissal. */ [[nodiscard]] Result<void> tick(float now,float mouseX,float mouseY,float screenWidth,float screenHeight,bool anyInput);
 /** @brief Hide and cancel all pending tooltip work. */ void hide()noexcept{visible_=false;pending_=false;}
 /** @brief Return visible state. */ bool getVisible()const noexcept{return visible_;}
 /** @brief Return pending-delay state. */ bool getPending()const noexcept{return pending_;}
 /** @brief Return active header. */ const std::string& getHeader()const noexcept{return header_;}
 /** @brief Return active content. */ const std::string& getText()const noexcept{return text_;}
 /** @brief Return whether header should be shown. */ bool getHeaderVisible()const noexcept{return !header_.empty();}
 /** @brief Return whether layout wrapping is enabled. */ bool getWrapEnabled()const noexcept{return int(header_.size())>wrapLimit_||int(text_.size())>wrapLimit_;}
 /** @brief Return pointer pivot X. */ float getPivotX()const noexcept{return pivotX_;}
 /** @brief Return pointer pivot Y. */ float getPivotY()const noexcept{return pivotY_;}
 /** @brief Return background red for active theme. */ float getBackgroundR()const noexcept{return theme_==0?1.f:0.1981132f;}
 /** @brief Return foreground intensity for active theme. */ float getForeground()const noexcept{return theme_==0?0.f:1.f;}
 /** @brief Return profile row count. */ int getCount()const noexcept{return int(entries_.size());}
private:bool accepts(int source)const noexcept;std::vector<PcgTooltipEntry>entries_;int mode_=2,theme_=1,wrapLimit_=40,source_=-1;float delay_=2,bottomOffset_=2,topOffset_=2,showAt_=0,pivotX_=0,pivotY_=0;bool visible_=false,pending_=false;std::string header_,text_;
};
/** @brief Register Pcg tooltip bindings. */ void exposePcgTooltipBindings(ssq::Table& table);
}
