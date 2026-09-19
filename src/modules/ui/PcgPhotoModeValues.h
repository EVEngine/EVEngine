#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
namespace ssq { class Table; }
namespace eve::ui {
struct PcgPhotoModeColor { float r=0,g=0,b=0,a=1; bool operator==(const PcgPhotoModeColor&) const = default; };
enum class PcgPhotoModeValueType { Bool=0, Int=1, Float=2, String=3, Color=4 };
using PcgPhotoModeValue=std::variant<bool,int64_t,float,std::string,PcgPhotoModeColor>;
struct PcgPhotoModeField { std::string name; PcgPhotoModeValueType type; PcgPhotoModeValue defaultValue; };
/** @brief Complete typed value set from Pcg PhotoModeProfile with exact defaults. */
class EVENGINE_API_WORLD PcgPhotoModeValues {
public:
 PcgPhotoModeValues();
 /** @brief Restore all 102 Pcg defaults atomically. */ void resetDefaults();
 /** @brief Return stable schema field count. */ uint64_t getFieldCount()const noexcept;
 /** @brief Return schema field name by declaration order. */ [[nodiscard]] Result<std::string> getFieldName(uint64_t index)const;
 /** @brief Return field type: bool=0, int=1, float=2, string=3, color=4. */ [[nodiscard]] Result<int> getFieldType(const std::string&name)const;
 /** @brief Set a typed boolean field. */ [[nodiscard]] Result<void> setBool(const std::string&name,bool value);
 /** @brief Set a typed integer or enum field. */ [[nodiscard]] Result<void> setInt(const std::string&name,int64_t value);
 /** @brief Set a finite typed float field. */ [[nodiscard]] Result<void> setFloat(const std::string&name,float value);
 /** @brief Set a typed string field. */ [[nodiscard]] Result<void> setString(const std::string&name,const std::string&value);
 /** @brief Set a finite typed RGBA field. */ [[nodiscard]] Result<void> setColor(const std::string&name,float r,float g,float b,float a);
 /** @brief Read a typed boolean field. */ [[nodiscard]] Result<bool> getBool(const std::string&name)const;
 /** @brief Read a typed integer field. */ [[nodiscard]] Result<int64_t> getInt(const std::string&name)const;
 /** @brief Read a typed float field. */ [[nodiscard]] Result<float> getFloat(const std::string&name)const;
 /** @brief Read a typed string field. */ [[nodiscard]] Result<std::string> getString(const std::string&name)const;
 /** @brief Read a field as its schema-declared native variant. */ [[nodiscard]] Result<PcgPhotoModeValue> getValue(const std::string&name)const;
 /** @brief Select a color field for component reads. */ [[nodiscard]] Result<void> selectColor(const std::string&name);
 /** @brief Encode schema 1 JSON containing every field. */ [[nodiscard]] Result<std::string> snapshotJson() const;
 /** @brief Atomically restore schema 1 JSON; unknown or missing fields are rejected. */ [[nodiscard]] Result<void> restoreJson(const std::string&json);
 float getColorR()const noexcept{return selectedColor_.r;} float getColorG()const noexcept{return selectedColor_.g;}
 float getColorB()const noexcept{return selectedColor_.b;} float getColorA()const noexcept{return selectedColor_.a;}
private: std::unordered_map<std::string,PcgPhotoModeValue> values_; PcgPhotoModeColor selectedColor_{};
};
/** @brief Register complete Pcg photo-mode value bindings. */ void exposePcgPhotoModeValuesBindings(ssq::Table&table);
}
