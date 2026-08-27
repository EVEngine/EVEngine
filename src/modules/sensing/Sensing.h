#pragma once

#include "common/BorrowedRef.h"
#include "common/Module.h"
#include "common/Result.h"
#include "common/SquirrelOwnership.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace eve::sensing {

/** @brief Mirrored gameplay facts used by candidate queries. */
struct Subject {
    std::string           id;
    float                 x = 0, y = 0;
    std::string           faction;
    std::set<std::string> tags;
    std::set<std::string> visibleTo;
};

/** @brief A deterministic candidate returned by a spatial query. */
struct Candidate {
    std::string id;
    float       x = 0, y = 0, distance = 0;
};

/** @brief Gameplay-facing 2D candidate query service; it never values or selects targets. */
class SensingWorld {
public:
    /** @brief Inserts or replaces mirrored facts. CSV fields contain comma-separated stable keys. */
    [[nodiscard]] eve::Result<void> upsert(std::string_view id, float x, float y, std::string_view faction,
                                           std::string_view tagsCsv, std::string_view visibleToCsv);
    /** @brief Removes mirrored facts, or returns NotFound when the id is absent. */
    [[nodiscard]] eve::Result<void> remove(std::string_view id);
    /** @brief Queries a circle. Filters are CSV; empty fields disable that filter. */
    [[nodiscard]] eve::Result<int> circle(float x, float y, float radius, std::string_view requireTagsCsv,
                                          std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                                          std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit);
    /** @brief Queries an axis-aligned box with the same filters as circle(). */
    [[nodiscard]] eve::Result<int> box(float minX, float minY, float maxX, float maxY, std::string_view requireTagsCsv,
                                       std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                                       std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit);
    /**
     * @brief Returns a candidate from the most recent query, or null for an invalid index.
     * @return Borrowed nullable candidate owned by the query result cache.
     * @ownership SensingWorld owns the candidate cache; callers must not delete or mutate it.
     * @lifetime Valid until the next circle/box query, restore, or world destruction; copy it for later use.
     * @thread Call on the sensing world's owning simulation thread.
     * @reentrancy Do not retain across a callback or another query.
     */
    [[nodiscard]] eve::OptionalRef<const Candidate> resultAt(int index) const;
    /** @brief Exports deterministic JSON. */
    std::string snapshotJson() const;
    /** @brief Restores a snapshot transactionally. */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

private:
    bool accepts(const Subject&, const std::set<std::string>&, const std::set<std::string>&,
                 const std::set<std::string>&, const std::set<std::string>&, std::string_view) const;
    int  finish(float x, float y, int limit);
    std::map<std::string, Subject> subjects_;
    std::vector<Candidate>         results_;
};

/** @brief Handle domain for module-owned sensing worlds. */
struct SensingWorldHandleTag {};
/** @brief Generation- and module-epoch-qualified sensing-world reference. */
using SensingWorldHandleRef = eve::script::RuntimeHandleRef<SensingWorldHandleTag>;

/** @brief Script factory for independent sensing worlds. */
class Sensing : public Module {
public:
    Module_REG(Sensing);
    /**
     * @brief Script factory for independent sensing worlds.
     * @return A generation-qualified reference to a module-owned world.
     * @ownership Sensing retains sole ownership; callers retain the reference and release it explicitly.
     * @lifetime The reference becomes stale after release, module unload, or registry replacement.
     * @thread Call on the Sensing module's owning thread.
     * @reentrancy The factory invokes no callbacks.
     */
    [[nodiscard]] static eve::Result<SensingWorldHandleRef> newWorld();
    /** @brief Resolves a live sensing world as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<SensingWorld> resolve(SensingWorldHandleRef reference) noexcept;
    /** @brief Releases a module-owned sensing world and invalidates its handle. */
    [[nodiscard]] static eve::Result<void> release(SensingWorldHandleRef reference);
    /** @brief Reports whether a world reference is stale. */
    [[nodiscard]] static bool isStale(SensingWorldHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<SensingWorld, SensingWorldHandleTag> worlds_;
};
}  // namespace eve::sensing
