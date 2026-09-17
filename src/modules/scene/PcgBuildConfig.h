#pragma once
#include "common/Result.h"
#include <cstdint>
#include <string>
#include <vector>
namespace ssq { class Table; }
namespace eve::scene {
enum class PcgPublicationType { Addressables=0, RegularBuild=1 };
enum class PcgBuildLogCategory { Impostors=0, ServerScene=1, CreatedAddressableConfig=2, AddressableBundles=3, ProjectBuild=4, ColliderBaking=5, UpdatedAddressableConfig=6 };
struct PcgBuildLogEntry { PcgBuildLogCategory category=PcgBuildLogCategory::Impostors; std::string sceneName; int64_t timestamp=0; };
/** @brief Caller-owned Pcg publication configuration and ordered build history. */
class PcgBuildConfig {
public:
 /** @brief Set publication type, where 0 is Addressables and 1 is RegularBuild. */ [[nodiscard]] Result<void> setPublicationType(int type);
 /** @brief Return publication type. */ int getPublicationType()const noexcept{return static_cast<int>(publicationType_);}
 /** @brief Append one ordered build history entry. */ [[nodiscard]] Result<void> addHistory(int category,const std::string&sceneName,int64_t timestamp);
 /** @brief Clear all history entries. */ void clearHistory()noexcept{history_.clear();}
 /** @brief Return history entry count. */ uint64_t getHistoryCount()const noexcept{return history_.size();}
 /** @brief Return category at index. */ [[nodiscard]] Result<int> getHistoryCategory(uint64_t index)const;
 /** @brief Return scene name at index. */ [[nodiscard]] Result<std::string> getHistorySceneName(uint64_t index)const;
 /** @brief Return Unix timestamp at index. */ [[nodiscard]] Result<int64_t> getHistoryTimestamp(uint64_t index)const;
private: PcgPublicationType publicationType_=PcgPublicationType::RegularBuild; std::vector<PcgBuildLogEntry> history_;
};
/** @brief Register Pcg build-config bindings. */ void exposePcgBuildConfigBindings(ssq::Table&table);
}
