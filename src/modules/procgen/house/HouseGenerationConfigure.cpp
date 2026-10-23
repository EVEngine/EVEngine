#include "procgen/house/HouseGenerationState.h"

namespace eve::housegen::house_detail {
std::vector<uint8_t> State::makeMask(int ins) const {
    return isPolygon ? polygonMask(r.perimeter, r.width, r.depth, 0)
                     : footprintMask(generated.footprintStyle, r.width, r.depth, ins);
}


eve::Result<void> State::configure() {
    if (r.width < 3 || r.depth < 3 || r.floors < 1 || r.maxAttempts < 1) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "house needs a 3x3 plot, at least one floor and one attempt", {}, {},
            "housegen.generate"));
    }
    if (!std::isfinite(r.moduleSize) || r.moduleSize <= 0.f || !std::isfinite(r.floorHeight) || r.floorHeight <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "moduleSize and floorHeight must be finite and positive", {}, {},
            "housegen.generate"));
    if (!isOneOf(r.footprint, {"auto", "rectangle", "l_shape", "t_shape", "polygon"}) ||
        !isOneOf(r.roof, {"auto", "gable", "flat", "shed"}) ||
        !isOneOf(r.entrance, {"auto", "north", "east", "south", "west"})) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                                 "unsupported footprint, roof or entrance mode", {}, {},
                                                                 "housegen.generate"));
    }
    if (r.footprint == "polygon" && r.perimeter.size() < 3) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "polygon footprint needs at least 3 perimeter points",
                                                                 {}, {}, "housegen.generate"));
    }
    const auto foundation = library_.byCategory("foundation", r.style);
    const auto floor      = library_.byCategory("floor", r.style);
    const auto wall       = library_.byCategory("wall", r.style);
    const auto door       = library_.byCategory("door", r.style);
    const auto roof       = library_.byCategory("roof", r.style);
    if (!has(foundation) || !has(floor) || !has(wall) || !has(door) || !has(roof)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "library needs foundation, floor, wall, door and roof categories", {}, {},
            "housegen.generate"));
    }
    // A named style must be a complete pack; otherwise per-category selection would
    // silently mix it with unstyled components into a broken house.
    if (!r.style.empty() && !library_.hasCompletePack(r.style)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound,
            "style pack '" + r.style + "' is incomplete (needs foundation, floor, wall, door and roof)", {}, {},
            "housegen.generate"));
    }
    // Interior partition and stairwell categories are optional: interior walls/doors are used
    // only for an explicit multi-room request, stairs only for multi-floor houses.
    const auto stairs              = library_.byCategory("stairs", r.style);
    const auto innerWall           = library_.byCategory("interior_wall", r.style);
    const auto innerDoor           = library_.byCategory("interior_door", r.style);
    const bool partitionsInteriors = r.requiredRooms.size() > 1;
    if (partitionsInteriors && (!has(innerWall) || !has(innerDoor)))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "multi-room generation needs interior_wall and interior_door categories",
            "requiredRooms", {}, "housegen.generate"));

    rng.seed(r.seed);
    generated.seed                            = r.seed;
    generated.moduleSize                      = r.moduleSize;
    generated.floorHeight                     = r.floorHeight;
    static constexpr const char     *shapes[] = {"rectangle", "l_shape", "t_shape"};
    static constexpr const char     *roofs[]  = {"gable", "flat", "shed"};
    static constexpr SocketDirection sides[]  = {SocketDirection::North, SocketDirection::East, SocketDirection::South,
                                                 SocketDirection::West};
    generated.footprintStyle                  = r.footprint == "auto"      ? shapes[rng() % 3]
                                                : r.footprint == "polygon" ? "polygon"
                                                                           : r.footprint;
    generated.roofStyle                       = r.roof == "auto" ? roofs[rng() % 3] : r.roof;
    const SocketDirection entranceDirection   = r.entrance == "auto" ? sides[rng() % 4] : directionFromName(r.entrance);
    generated.entranceSide                    = directionName(entranceDirection);

    // A multi-floor house needs a vertical stair column through an interior cell.
    isPolygon                = generated.footprintStyle == "polygon";
    baseMask                 = makeMask(0);
    generated.footprintWidth = r.width;
    generated.footprintDepth = r.depth;
    generated.footprintMask  = baseMask;

    if (r.floors > 1 && has(stairs)) {
        stairwell = findInteriorCell(baseMask, r.width, r.depth);
        if (!stairwell)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "multi-floor house needs an interior cell for a stairwell", {}, {},
                "housegen.generate"));
    }

    return eve::Result<void>::success();
}
}  // namespace eve::housegen::house_detail
