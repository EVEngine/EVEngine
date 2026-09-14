#pragma once

#include "common/Export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev::lsp {

struct Position {
    size_t line      = 0;
    size_t character = 0;
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    std::string uri;
    Range       range;
};

struct TextEdit {
    Location    location;
    std::string newText;
};

/** @brief LSP semantic-token type indices (must match the initialize legend). */
namespace SemanticTypes {
constexpr uint32_t Namespace = 0;
constexpr uint32_t Class     = 1;
constexpr uint32_t Function  = 2;
constexpr uint32_t Method    = 3;
constexpr uint32_t Variable  = 4;
constexpr uint32_t Parameter = 5;
constexpr uint32_t Property  = 6;
constexpr uint32_t Keyword   = 7;
}  // namespace SemanticTypes

/** @brief LSP semantic-token modifier bits (must match the initialize legend). */
namespace SemanticMods {
constexpr uint32_t Declaration    = 1u << 0;
constexpr uint32_t Readonly       = 1u << 1;
constexpr uint32_t DefaultLibrary = 1u << 2;
}  // namespace SemanticMods

/** @brief One identifier classified for editor semantic highlighting. */
struct SemanticToken {
    Position    start;
    size_t      length     = 0;
    uint32_t    type       = SemanticTypes::Variable;
    uint32_t    modifiers  = 0;
    std::string name;
};

/** @brief Cross-file semantic index for EveScript source modules. */
class EVENGINE_API WorkspaceIndex {
public:
    WorkspaceIndex();
    ~WorkspaceIndex();
    WorkspaceIndex(const WorkspaceIndex&)            = delete;
    WorkspaceIndex& operator=(const WorkspaceIndex&) = delete;

    /** @brief Adds or replaces one canonical source unit. */
    void update(std::string canonicalUri, std::string clientUri, std::string source);
    /** @brief Removes one source unit from the index. */
    void remove(std::string_view canonicalUri);
    /** @brief Finds the declaration targeted by the identifier at a position. */
    std::optional<Location> definition(std::string_view canonicalUri, Position position) const;
    /** @brief Finds project references for the identifier at a position. */
    std::vector<Location> references(std::string_view canonicalUri, Position position, bool includeDeclaration) const;
    /** @brief Computes a project-wide rename, or nullopt for an invalid/unresolved request. */
    std::optional<std::vector<TextEdit>> rename(std::string_view canonicalUri, Position position,
                                                std::string_view newName) const;
    /** @brief Identifier classifications for LSP semantic highlighting. */
    std::vector<SemanticToken> semanticTokens(std::string_view canonicalUri) const;

private:
    struct Unit;
    struct Target;
    struct Impl;

    const Unit*             unit(std::string_view canonicalUri) const;
    std::optional<Target>   targetAt(const Unit& unit, Position position) const;
    std::optional<Location> targetDefinition(const Target& target) const;
    std::vector<Location>   targetReferences(const Target& target, bool includeDeclaration) const;
    std::unique_ptr<Impl>   impl_;
};

}  // namespace eve::dev::lsp
