#pragma once

#include <vector>

namespace eve::procgen {

struct CaveDetachmentResult {
    int unsupportedVoxels = 0;
    int detachedVoxels    = 0;
};

CaveDetachmentResult detachUnsupportedCaveFragments(std::vector<float>& density, int nx, int ny, int nz,
                                                    float strength);

}  // namespace eve::procgen
