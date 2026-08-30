#pragma once

#include "procgen/urban/UrbanTypes.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace eve::procgen::urban {

/**
 * @brief Hierarchical co-generation of parcels and streets (paper Section 4) plus
 * the global geometric optimization (paper Section 5).
 *
 * At each hierarchical level every splittable parcel is binary-partitioned by the
 * best streamline chosen with the quality metric of Eq. (2); short edges in the
 * parcel mesh are removed; unreachable parcels are grouped and given street access
 * (I/L-shaped accesses + turn-aware Dijkstra connections, paper Section 4.2).
 * Finally the vertex positions are optimized with the five-term energy of Eq. (3).
 */
class UrbanGenerator {
public:
    explicit UrbanGenerator(UrbanOptions opts);

    /** @brief Run the pipeline. On failure `error` receives a human-readable reason. */
    bool generate(std::string* error = nullptr);

    const UrbanLayout& layout() const { return layout_; }

    struct GenState;

private:
    bool buildBoundaryStreets(GenState& st);
    bool generateLevel(GenState& st, int level, std::string* error);
    void splitAllParcels(GenState& st, std::vector<Polygon>& nextParcels);
    bool splitOneParcel(const GenState& st, const Polygon& poly, Polygon& outA, Polygon& outB) const;
    void removeShortEdges(GenState& st);
    void generateStreets(GenState& st, int level);
    void connectAccessToNetwork(GenState& st, const std::vector<int>& accessVertices,
                                const std::vector<char>* excludeTargets = nullptr);
    void reconnectStreetEnds(GenState& st, int level);
    void decomposeStreets(GenState& st);
    void finalizeStats(GenState& st);

    UrbanOptions opts_;
    UrbanLayout  layout_;
    std::mt19937 rng_;
};

}  // namespace eve::procgen::urban
