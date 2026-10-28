#pragma once
#include "common/Export.h"


#include <vector>

namespace eve::procgen {

/** @brief CaveDetachmentResult public API. */
struct CaveDetachmentResult {
    int unsupportedVoxels = 0;
    int detachedVoxels    = 0;
};

/** @brief Detaches unsupported cave fragments. */
EVENGINE_API_DOMAINS CaveDetachmentResult detachUnsupportedCaveFragments(std::vector<float>& density, int nx, int ny,
                                                                         int nz, float strength);

}  // namespace eve::procgen
