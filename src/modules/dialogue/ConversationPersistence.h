#pragma once
#include "common/Export.h"

#include "common/StateValue.h"
#include "dialogue/Conversation.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::dialogue {

/** @brief Serialize a dialogue state tree as JSON. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<std::string> conversationStateToJson(const StateValue& state);

/** @brief Parse a JSON dialogue state tree. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<StateValue> conversationStateFromJson(const std::string& json);

/** @brief Explicit save migrations from an old asset version to its current version. */
class EVENGINE_API_ORCHESTRATION ConversationSaveMigrations {
public:
    using Resolver = std::function<const ConversationAsset*(const std::string&)>;

    /**
     * @brief Register a direct migration from one saved asset version to the currently loaded asset.
     * @param assetId ID stored by the old save.
     * @param fromVersion Version stored by the old save.
     * @param currentAssetId ID of the currently loaded replacement asset.
     * @param nodeMap Comma-separated old:new node mappings; unchanged IDs need not be listed.
     */
    [[nodiscard]] eve::Result<void> registerMigration(const std::string& assetId, int fromVersion,
                                                      const std::string& currentAssetId,
                                                      const std::string& nodeMap);

    /** @brief Remove all registered migrations. */
    void clear() { rules_.clear(); }

    /** @brief Migrate the current frame and every saved call frame transactionally. */
    [[nodiscard]] eve::Result<StateValue> migrate(const StateValue& state, const Resolver& resolve) const;

private:
    struct Rule {
        std::string                                  assetId;
        int                                          fromVersion = 0;
        std::string                                  currentAssetId;
        std::unordered_map<std::string, std::string> nodes;
    };
    std::vector<Rule> rules_;
};

}  // namespace eve::dialogue
