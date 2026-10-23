#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseGeneration.h"
#include "procgen/house/HouseLayout.h"

namespace eve::housegen {
eve::Result<void> HouseGenerator::generate(const HouseRequest& request, HouseLayout& out) const {
    HouseGeneration generation;
    auto            result = generation.configure(request, library_);
    if (!result.ok()) return result;
    for (int floor = 0; floor < request.floors; ++floor) {
        auto grid = generation.nextFloorGrid();
        if (!grid.ok()) return eve::Result<void>::failure(grid.status());
        result = generation.emitFloor(grid.value(), request.requiredRooms.size() <= 1
                                                        ? std::vector<std::string>{floor == 0 ? "living" : "upper"}
                                                        : request.requiredRooms);
        if (!result.ok()) return result;
    }
    auto layout = generation.finish();
    if (!layout.ok()) return eve::Result<void>::failure(layout.status());
    out = std::move(layout).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace eve::housegen
