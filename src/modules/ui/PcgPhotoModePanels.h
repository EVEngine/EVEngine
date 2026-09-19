#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "ui/PcgUiStatus.h"
#include <array>
#include <cstdint>
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Seven-panel selection transaction used by Pcg photo mode. */
class EVENGINE_API_WORLD PcgPhotoModePanels {
public:
 /** @brief Select startup panel 0..6 and atomically publish all button/panel states. */
 [[nodiscard]] Result<void> select(int panel);
 /** @brief Return current panel index. */ int getSelected() const noexcept { return selected_; }
 /** @brief Return whether one panel and its button are active. */ bool isActive(int panel) const noexcept;
 /** @brief Consume request to rebuild selected scroll content at its top. */ PcgUiRequestStatus consumeScrollReset() noexcept;
 /** @brief Return monotonically increasing successful selection revision. */ uint64_t getRevision() const noexcept { return revision_; }
private:int selected_=0;std::array<bool,7> active_{{true,false,false,false,false,false,false}};bool scrollReset_=false;uint64_t revision_=0;
};
/** @brief Visual snapshot matching Pcg PhotoModePanelButton. */
class EVENGINE_API_WORLD PcgPhotoModePanelButton {
public:
 /** @brief Configure selected and unselected RGBA colors atomically. */
 [[nodiscard]] Result<void> configure(float nr,float ng,float nb,float na,float sr,float sg,float sb,float sa);
 /** @brief Set selected state and layout enabled state. */ void setSelected(bool selected) noexcept { selected_=selected; layoutEnabled_=selected; }
 /** @brief Return normal red. */ float getNormalR()const noexcept{return selected_?sr_:nr_;}
 /** @brief Return normal green. */ float getNormalG()const noexcept{return selected_?sg_:ng_;}
 /** @brief Return normal blue. */ float getNormalB()const noexcept{return selected_?sb_:nb_;}
 /** @brief Return normal alpha. */ float getNormalA()const noexcept{return selected_?sa_:na_;}
 /** @brief Return highlight red, always selected color. */ float getHighlightR()const noexcept{return sr_;}
 /** @brief Return whether panel layout is enabled. */ bool getLayoutEnabled()const noexcept{return layoutEnabled_;}
 /** @brief Return selected state. */ bool getSelected()const noexcept{return selected_;}
private:float nr_=.5f,ng_=.5f,nb_=.5f,na_=1.f,sr_=1.f,sg_=1.f,sb_=0.f,sa_=1.f;bool selected_=false,layoutEnabled_=false;
};
/** @brief Register Pcg photo-mode panel bindings. */ void exposePcgPhotoModePanelsBindings(ssq::Table& table);
}
