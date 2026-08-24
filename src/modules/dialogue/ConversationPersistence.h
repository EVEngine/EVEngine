#pragma once

#include "common/StateValue.h"
#include "dialogue/Conversation.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::dialogue {

/** @brief Serialize a dialogue state tree as JSON. */
std::string conversationStateToJson(const StateValue& state, std::string* error = nullptr);

/** @brief Parse a JSON dialogue state tree. */
bool conversationStateFromJson(const std::string& json, StateValue& state, std::string* error = nullptr);

/** @brief Explicit save migrations from an old asset version to its current version. */
class ConversationSaveMigrations {
public:
    using Resolver = std::function<const ConversationAsset*(const std::string&)>;

    /**
     * @brief Register a direct migration from one saved asset version to the currently loaded asset.
     * @param assetId ID stored by the old save.
     * @param fromVersion Version stored by the old save.
     * @param currentAssetId ID of the currently loaded replacement asset.
     * @param nodeMap Comma-separated old:new node mappings; unchanged IDs need not be listed.
     */
    bool registerMigration(const std::string& assetId, int fromVersion, const std::string& currentAssetId,
                           const std::string& nodeMap, std::string* error = nullptr);

    /** @brief Remove all registered migrations. */
    void clear() { rules_.clear(); }

    /** @brief Migrate the current frame and every saved call frame transactionally. */
    bool migrate(StateValue& state, const Resolver& resolve, std::string* error = nullptr) const;

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
