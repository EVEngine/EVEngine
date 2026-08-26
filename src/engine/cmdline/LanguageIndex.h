#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::cmd::lsp {

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

/** @brief Cross-file semantic index for EveScript source modules. */
class WorkspaceIndex {
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

}  // namespace eve::cmd::lsp
