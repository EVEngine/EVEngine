#include "procgen/house/HouseGenerationState.h"

namespace eve::housegen::house_detail {
eve::Result<void> State::floor(const procgen::Grid2D &grid, const std::vector<std::string> &roomTypes) {
    const auto           foundation          = library_.byCategory("foundation", r.style);
    const auto           floor               = library_.byCategory("floor", r.style);
    const auto           wall                = library_.byCategory("wall", r.style);
    const auto           door                = library_.byCategory("door", r.style);
    const auto           roof                = library_.byCategory("roof", r.style);
    const auto           stairs              = library_.byCategory("stairs", r.style);
    const auto           innerWall           = library_.byCategory("interior_wall", r.style);
    const auto           innerDoor           = library_.byCategory("interior_door", r.style);
    const bool           partitionsInteriors = roomTypes.size() > 1;
    const auto           entranceDirection   = directionFromName(generated.entranceSide);
    std::vector<uint8_t> mask(grid.cells().size());
    std::transform(grid.cells().begin(), grid.cells().end(), mask.begin(),
                   [](auto cell) { return uint8_t(cell != 0); });
    // Any lower-floor cell not covered by this floor becomes a roof terrace/canopy at the
    // current level. Thus every floor cell is covered by either another floor or a roof.
    if (!previousMask.empty()) {
        for (int y = 0; y < r.depth; ++y)
            for (int x = 0; x < r.width; ++x) {
                if (active(previousMask, r.width, r.depth, x, y) && !active(mask, r.width, r.depth, x, y))
                    generated.instances.push_back({pick(roof, rng)->id, x, y, z, 0});
            }
    }
    int minX = r.width, minY = r.depth, maxX = -1, maxY = -1;
    for (int y = 0; y < r.depth; ++y)
        for (int x = 0; x < r.width; ++x)
            if (active(mask, r.width, r.depth, x, y)) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                // The stair column replaces foundation+floor on every level it passes through.
                if (stairwell && x == stairwell->first && y == stairwell->second) {
                    generated.instances.push_back({pick(stairs, rng)->id, x, y, z, 0});
                    continue;
                }
                if (z == 0) generated.instances.push_back({pick(foundation, rng)->id, x, y, z, 0});
                generated.instances.push_back({pick(floor, rng)->id, x, y, z, 0});
            }
    if (maxX < minX || maxY < minY)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "footprint collapsed after inset", {}, {}, "housegen.generate"));
    if (!partitionsInteriors) appendRoomRegions(r.width, r.depth, mask, roomTypes.front(), z, generated.rooms);

    using Face = std::tuple<int, int, SocketDirection>;
    std::vector<Face>            faces;
    std::vector<Face>            entranceCandidates;
    std::unordered_map<int, int> facesPerCell;
    for (int y = 0; y < r.depth; ++y)
        for (int x = 0; x < r.width; ++x) {
            if (!active(mask, r.width, r.depth, x, y)) continue;
            const SocketDirection directions[] = {SocketDirection::North, SocketDirection::East, SocketDirection::South,
                                                  SocketDirection::West};
            const int             dx[]         = {0, 1, 0, -1};
            const int             dy[]         = {-1, 0, 1, 0};
            for (int side = 0; side < 4; ++side)
                if (!active(mask, r.width, r.depth, x + dx[side], y + dy[side])) {
                    Face face{x, y, directions[side]};
                    faces.push_back(face);
                    ++facesPerCell[y * r.width + x];
                    if (z == 0 && directions[side] == entranceDirection) entranceCandidates.push_back(face);
                }
        }
    std::sort(entranceCandidates.begin(), entranceCandidates.end(), [](const Face &a, const Face &b) {
        return std::tie(std::get<1>(a), std::get<0>(a)) < std::tie(std::get<1>(b), std::get<0>(b));
    });
    Face entranceFace{-1, -1, entranceDirection};
    if (z == 0 && !entranceCandidates.empty()) entranceFace = entranceCandidates[entranceCandidates.size() / 2];
    for (const auto &faceData : faces) {
        const auto [x, y, face] = faceData;
        const bool isEntrance   = z == 0 && faceData == entranceFace;
        const int  rotation     = directionRotation(face);
        auto       choices      = compatibleOnFace(isEntrance ? door : wall, face, rotation);
        if (!isEntrance) {
            const bool corner = facesPerCell[y * r.width + x] > 1;
            const bool besideEntrance =
                z == 0 && std::abs(x - std::get<0>(entranceFace)) + std::abs(y - std::get<1>(entranceFace)) <= 1;
            const int  facadeAxis     = (face == SocketDirection::North || face == SocketDirection::South) ? x : y;
            const bool rhythmicWindow = ((facadeAxis + int(r.seed & 1u)) & 1) == 0;
            choices                   = facadeVariant(choices, !corner && !besideEntrance && rhythmicWindow);
        }
        const auto *selected = pick(choices, rng);
        if (!selected) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound,
                "no socket-compatible " + std::string(isEntrance ? "door" : "wall") + " component", {}, {},
                "housegen.generate"));
        }
        generated.instances.push_back({selected->id, x, y, z, rotation});
    }
    if (partitionsInteriors) {
        auto partitioned = partitionFloor(r.width, r.depth, mask, roomTypes, rng, innerWall, innerDoor,
                                          generated.instances, generated.rooms, z);
        if (!partitioned.ok()) return eve::Result<void>::failure(partitioned.status());
    }
    if (z + 1 == r.floors) {
        for (int y = 0; y < r.depth; ++y)
            for (int x = 0; x < r.width; ++x)
                if (active(mask, r.width, r.depth, x, y))
                    generated.instances.push_back({pick(roof, rng)->id, x, y, z + 1, 0});
    }
    previousMask = mask;
    ++z;
    planned.reset();
    return eve::Result<void>::success();
}
}  // namespace eve::housegen::house_detail
