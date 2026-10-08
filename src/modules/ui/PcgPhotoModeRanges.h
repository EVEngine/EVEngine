#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace ssq { class Table; }
namespace eve::ui {
struct PcgPhotoModeRange { double minimum=0,maximum=1;bool integral=false; };
/** @brief Complete 57-range contract from Pcg PhotoModeMinAndMaxValues. */
class EVENGINE_API_WORLD PcgPhotoModeRanges {
public:
 PcgPhotoModeRanges();
 /** @brief Restore all Pcg range defaults. */ void resetDefaults();
 /** @brief Return range count. */ uint64_t getCount()const noexcept;
 /** @brief Return range name in declaration order. */ [[nodiscard]] Result<std::string> getName(uint64_t index)const;
 /** @brief Select a range for component reads. */ [[nodiscard]] Result<void> select(const std::string&name);
 /** @brief Replace one range; integer ranges require integral endpoints. */ [[nodiscard]] Result<void> setRange(const std::string&name,double minimum,double maximum);
 /** @brief Clamp a finite value to the named range. */ [[nodiscard]] Result<double> clamp(const std::string&name,double value)const;
 double getMinimum()const noexcept{return selected_.minimum;} double getMaximum()const noexcept{return selected_.maximum;} bool getIntegral()const noexcept{return selected_.integral;}
private:std::unordered_map<std::string,PcgPhotoModeRange> ranges_;PcgPhotoModeRange selected_{};
};
/** @brief Register Pcg photo-mode range bindings. */ void exposePcgPhotoModeRangesBindings(ssq::Table&table);
}
