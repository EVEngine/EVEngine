#pragma once

#include "common/Module.h"

#include <map>
#include <memory>
#include <set>
#include <string>
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
    bool upsert(const std::string& id, float x, float y, const std::string& faction, const std::string& tagsCsv,
                const std::string& visibleToCsv);
    /** @brief Removes mirrored facts. */
    bool remove(const std::string& id);
    /** @brief Queries a circle. Filters are CSV; empty fields disable that filter. */
    int circle(float x, float y, float radius, const std::string& requireTagsCsv, const std::string& excludeTagsCsv,
               const std::string& includeFactionsCsv, const std::string& excludeFactionsCsv,
               const std::string& visibleTo, int limit);
    /** @brief Queries an axis-aligned box with the same filters as circle(). */
    int box(float minX, float minY, float maxX, float maxY, const std::string& requireTagsCsv,
            const std::string& excludeTagsCsv, const std::string& includeFactionsCsv,
            const std::string& excludeFactionsCsv, const std::string& visibleTo, int limit);
    /** @brief Returns a result from the most recent query. */
    const Candidate* resultAt(int index) const;
    /** @brief Exports deterministic JSON. */
    std::string snapshotJson() const;
    /** @brief Restores a snapshot transactionally. */
    bool restoreJson(const std::string& json);
    /** @brief Returns the last validation error. */
    const std::string& lastError() const { return lastError_; }

private:
    bool accepts(const Subject&, const std::set<std::string>&, const std::set<std::string>&,
                 const std::set<std::string>&, const std::set<std::string>&, const std::string&) const;
    int  finish(float x, float y, int limit);
    std::map<std::string, Subject> subjects_;
    std::vector<Candidate>         results_;
    std::string                    lastError_;
};

/** @brief Script factory for independent sensing worlds. */
class Sensing : public Module {
public:
    Module_REG(Sensing);
    SensingWorld* newWorld();

private:
    std::vector<std::unique_ptr<SensingWorld>> worlds_;
};
}  // namespace eve::sensing
