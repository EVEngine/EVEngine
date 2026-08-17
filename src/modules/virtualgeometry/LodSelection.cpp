#include "virtualgeometry/LodSelection.h"

#include <algorithm>
#include <cmath>

namespace eve::virtualgeometry {

int selectClusters(const VirtualGeometryAsset &asset, float dist, float projScale, float errorPx,
                   std::vector<std::uint32_t> &outSelected) {
    outSelected.clear();
    const std::vector<VgCluster> &clusters = asset.clusters;
    if (clusters.empty() || dist <= 0.f) return 0;

    const float d = std::max(dist, 1e-4f);
    for (std::size_t cid = 0; cid < clusters.size(); ++cid) {
        const VgCluster &c = clusters[cid];
        const float screenC = c.errorR * projScale / d;
        const bool isLeaf = c.childCount == 0;

        // Parent's screen error; +inf for roots so the root can be selected.
        float screenP = 1e30f;
        if (c.parent != 0xFFFFFFFFu && c.parent < clusters.size())
            screenP = clusters[c.parent].errorR * projScale / d;

        const bool accept = (screenC <= errorPx || isLeaf) && (screenP > errorPx);
        if (accept) outSelected.push_back(static_cast<std::uint32_t>(cid));
    }
    return static_cast<int>(outSelected.size());
}

void lodHistogram(const VirtualGeometryAsset &asset, const std::vector<std::uint32_t> &selected,
                  std::vector<int> &perLevelCount) {
    int maxLod = 0;
    for (const auto &c : asset.clusters) maxLod = std::max(maxLod, static_cast<int>(c.lodLevel));
    perLevelCount.assign(static_cast<std::size_t>(maxLod + 1), 0);
    for (std::uint32_t cid : selected)
        if (cid < asset.clusters.size())
            ++perLevelCount[asset.clusters[cid].lodLevel];
}

}  // namespace eve::virtualgeometry
