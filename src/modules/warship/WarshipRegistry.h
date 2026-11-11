#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "warship/WarshipTypes.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace eve::warship {

/**
 * @brief Process-level catalogues for hull templates and compartment functions.
 * @ownership Registry owns the definition tables; WarshipEntity borrows pointers.
 * @thread Owner / composition thread only (not concurrent).
 */
class EVENGINE_API_DOMAINS WarshipRegistry {
public:
    [[nodiscard]] static Result<int> loadHullsFromJson(std::string_view json);
    [[nodiscard]] static Result<int> loadFunctionsFromJson(std::string_view json);
    static void                      clearHulls();
    static void                      clearFunctions();
    [[nodiscard]] static bool        hasHull(const std::string& id);
    [[nodiscard]] static bool        hasFunction(const std::string& id);
    [[nodiscard]] static int         hullCount();
    [[nodiscard]] static int         functionCount();
    /**
     * @brief Lookup a hull template.
     * @ownership Borrowed; valid until the next clear / replace of that id.
     * @nullable Yes when unknown.
     */
    [[nodiscard]] static const WarshipDefinition* findHull(const std::string& id);
    /**
     * @brief Lookup a compartment function.
     * @ownership Borrowed; valid until the next clear / replace of that id.
     * @nullable Yes when unknown.
     */
    [[nodiscard]] static const CompartmentFunctionDef* findFunction(const std::string& id);

private:
    static std::unordered_map<std::string, WarshipDefinition>&      hullTable();
    static std::unordered_map<std::string, CompartmentFunctionDef>& functionTable();
};

}  // namespace eve::warship
