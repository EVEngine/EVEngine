#include <cmath>
#include <limits>
#include <utility>
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

namespace eve::procgen_editing::detail {
using RoadCodecValue = EditorValue;
#include "procgen/road/RoadPointCodec.inc"
}  // namespace eve::procgen_editing::detail
