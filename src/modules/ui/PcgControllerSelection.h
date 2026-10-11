#pragma once
#include "common/Result.h"
#include <string>
#include <vector>
namespace ssq { class Table; }
namespace eve::ui {
/** @brief One configured controller-description row from Pcg UIControllerType. */ struct PcgControllerTypeRow{std::string name,widgetId;int controllerType=0;bool visible=false;};
/** @brief Caller-owned visibility selection ported from Pcg UIControllerSelection. */
class PcgControllerSelection {
public:
 /** @brief Append one controller type and its stable UI widget id. */ [[nodiscard]] Result<void> add(const std::string&name,int controllerType,const std::string&widgetId);
 /** @brief Select first matching type; no match preserves the previous visibility snapshot. */ [[nodiscard]] Result<bool> refresh(int currentController);
 /** @brief Return configured row count. */ int getCount()const noexcept{return int(rows_.size());}
 /** @brief Return selected row or -1. */ int getSelectedIndex()const noexcept{return selected_;}
 /** @brief Return row visibility. */ [[nodiscard]] Result<bool> getVisible(int index)const;
 /** @brief Return stable widget id. */ [[nodiscard]] Result<std::string> getWidgetId(int index)const;
private:std::vector<PcgControllerTypeRow>rows_;int selected_=-1;
};
/** @brief Register Pcg controller-selection bindings. */ void exposePcgControllerSelectionBindings(ssq::Table& table);
}
