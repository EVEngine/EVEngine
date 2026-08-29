#pragma once

/** @file GameplayTag.h @brief Stable hierarchical gameplay-tag definitions and registry. */

#include "common/Result.h"
#include "common/StrongUint64.h"
#include "common/Value.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eve::tags {

struct GameplayTagIdTag;
/** @brief Stable FNV-1a identifier derived from a canonical gameplay-tag name. */
using GameplayTagId = eve::detail::StrongUint64<GameplayTagIdTag>;

/** @brief Selects exact or hierarchical descendant matching. */
enum class GameplayTagMatch { Exact, IncludeDescendants };

/** @brief An owning gameplay-tag definition shared by runtime and editor tools. */
struct GameplayTagDefinition {
    GameplayTagId id;
    std::string   name;
    std::string   description;

    bool operator==(const GameplayTagDefinition&) const = default;
};

/** @brief Returns whether a name is a canonical dot-separated gameplay tag. */
[[nodiscard]] bool isValidGameplayTagName(std::string_view name) noexcept;
/** @brief Computes the stable 64-bit FNV-1a identifier of a canonical name. */
[[nodiscard]] GameplayTagId gameplayTagId(std::string_view name) noexcept;
/** @brief Tests exact or dot-boundary descendant membership. */
[[nodiscard]] bool gameplayTagMatches(std::string_view candidate, std::string_view query,
                                      GameplayTagMatch match) noexcept;

/**
 * @brief Canonical owner of versioned gameplay-tag definitions.
 *
 * The registry is owner-thread-only. Returned definitions and collections are
 * owning copies, so callers never retain pointers across mutations or reloads.
 */
class GameplayTagRegistry {
public:
    /** @brief Register a definition, returning NoOp for an identical definition. */
    [[nodiscard]] Result<GameplayTagId> registerTag(std::string name, std::string description = {});
    /** @brief Find an exact definition and return an owning copy. */
    [[nodiscard]] Result<GameplayTagDefinition> find(std::string_view name) const;
    /** @brief Return whether an exact definition exists. */
    [[nodiscard]] bool contains(std::string_view name) const;
    /** @brief Return all definitions in lexical name order. */
    [[nodiscard]] std::vector<GameplayTagDefinition> definitions() const;
    /** @brief Return definitions matching a root and case-insensitive text filter. */
    [[nodiscard]] std::vector<GameplayTagDefinition> search(std::string_view text, std::string_view root = {}) const;
    /** @brief Encode schema eve.gameplay-tags version 1. Unknown fields are ignored on load. */
    [[nodiscard]] Result<Value> toValue() const;
    /** @brief Transactionally decode schema eve.gameplay-tags version 1. */
    [[nodiscard]] static Result<GameplayTagRegistry> fromValue(const Value& value);

private:
    std::map<std::string, GameplayTagDefinition, std::less<>> definitionsByName_;
    std::map<GameplayTagId, std::string>                      nameById_;
};

}  // namespace eve::tags
