#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <random>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::housegen {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "housegen.generate"));
}

const HouseComponent *pick(const std::vector<std::reference_wrapper<const HouseComponent>> &choices,
                           std::mt19937                                                    &rng) {
    if (choices.empty()) return nullptr;
    int total = 0;
    for (const auto &choice : choices) total += choice.get().weight;
    std::uniform_int_distribution<int> roll(1, total);
    int target = roll(rng);
    for (const auto &choice : choices) {
        target -= choice.get().weight;
        if (target <= 0) return &choice.get();
    }
    return &choices.back().get();
}

bool has(const std::vector<std::reference_wrapper<const HouseComponent>> &values) { return !values.empty(); }

bool isWindowComponent(const HouseComponent *component) {
    if (!component) return false;
    if (component->id.find("window") != std::string::npos) return true;
    return std::find(component->tags.begin(), component->tags.end(), "window") != component->tags.end();
}

std::vector<std::reference_wrapper<const HouseComponent>> facadeVariant(
    const std::vector<std::reference_wrapper<const HouseComponent>> &choices, bool wantWindow) {
    bool hasWindow = false, hasSolid = false;
    for (const auto &choice : choices) {
        if (isWindowComponent(&choice.get()))
            hasWindow = true;
        else hasSolid = true;
    }
    if (!hasWindow || !hasSolid) return choices;
    std::vector<std::reference_wrapper<const HouseComponent>> filtered;
    for (const auto &choice : choices)
        if (isWindowComponent(&choice.get()) == wantWindow) filtered.push_back(choice);
    return filtered;
}

bool oneOf(const std::string &value, std::initializer_list<const char *> options) {
    for (const char *option : options) if (value == option) return true;
    return false;
}

SocketDirection directionFromName(const std::string &name) {
    if (name == "east") return SocketDirection::East;
    if (name == "south") return SocketDirection::South;
    if (name == "west") return SocketDirection::West;
    return SocketDirection::North;
}

std::string directionName(SocketDirection direction) {
    if (direction == SocketDirection::East) return "east";
    if (direction == SocketDirection::South) return "south";
    if (direction == SocketDirection::West) return "west";
    return "north";
}

int directionRotation(SocketDirection direction) {
    if (direction == SocketDirection::East) return 90;
    if (direction == SocketDirection::South) return 180;
    if (direction == SocketDirection::West) return 270;
    return 0;
}

std::vector<uint8_t> footprintMask(const std::string &shape, int width, int depth, int inset) {
    std::vector<uint8_t> mask(size_t(width * depth), 0);
    const int minX = inset, minY = inset, maxX = width - 1 - inset, maxY = depth - 1 - inset;
    if (minX > maxX || minY > maxY) return mask;
    const int spanX = maxX - minX + 1, spanY = maxY - minY + 1;
    const int cutX = minX + std::max(1, spanX / 2) - 1;
    const int cutY = minY + std::max(1, spanY / 2) - 1;
    const int stemInset = spanX >= 5 ? 1 : 0;
    for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
        bool active = true;
        if (shape == "l_shape") active = !(x > cutX && y > cutY);
        else if (shape == "t_shape") active = y <= cutY || (x >= minX + stemInset && x <= maxX - stemInset);
        if (active) mask[size_t(y * width + x)] = 1;
    }
    return mask;
}

bool active(const std::vector<uint8_t> &mask, int width, int depth, int x, int y) {
    return x >= 0 && y >= 0 && x < width && y < depth && mask[size_t(y * width + x)] != 0;
}

/** @brief Ray-casting point-in-polygon test in corner coordinates. */
bool pointInPolygon(float px, float py, const std::vector<HousePolygonPoint> &poly) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const float xi = poly[i].x, yi = poly[i].y;
        const float xj = poly[j].x, yj = poly[j].y;
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12f) + xi))
            inside = !inside;
    }
    return inside;
}

/** @brief Rasterize an arbitrary closed polygon onto the cell grid (cell centers inside). */
std::vector<uint8_t> polygonMask(const std::vector<HousePolygonPoint> &poly, int width, int depth, int inset) {
    std::vector<uint8_t> mask(size_t(width * depth), 0);
    for (int y = inset; y < depth - inset; ++y)
        for (int x = inset; x < width - inset; ++x)
            if (pointInPolygon(float(x) + 0.5f, float(y) + 0.5f, poly))
                mask[size_t(y * width + x)] = 1;
    return mask;
}

SocketDirection rotate(SocketDirection direction, int degrees) {
    if (direction == SocketDirection::Up || direction == SocketDirection::Down) return direction;
    int side = direction == SocketDirection::North ? 0 : direction == SocketDirection::East ? 1 :
               direction == SocketDirection::South ? 2 : 3;
    side = (side + degrees / 90) % 4;
    return side == 0 ? SocketDirection::North : side == 1 ? SocketDirection::East :
           side == 2 ? SocketDirection::South : SocketDirection::West;
}

std::vector<std::reference_wrapper<const HouseComponent>> compatibleOnFace(
    const std::vector<std::reference_wrapper<const HouseComponent>> &choices, SocketDirection face, int rotation) {
    std::vector<std::reference_wrapper<const HouseComponent>> out;
    for (const auto &choice : choices) {
        const auto &c = choice.get();
        // Components without sockets are intentionally wildcard-compatible. This preserves
        // compatibility with small legacy kits while new player kits opt into strict sockets.
        if (c.sockets.empty()) {
            out.push_back(choice);
            continue;
        }
        for (const auto &socket : c.sockets) {
            if (rotate(socket.direction, rotation) == face && !socket.type.empty()) {
                out.push_back(choice);
                break;
            }
        }
    }
    return out;
}

/** @brief First active cell that is interior (all four orthogonal neighbours active). */
std::optional<std::pair<int, int>> findInteriorCell(const std::vector<uint8_t> &mask, int width, int depth) {
    const int dx[] = {0, 1, 0, -1};
    const int dy[] = {-1, 0, 1, 0};
    for (int y = 1; y < depth - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            if (!active(mask, width, depth, x, y)) continue;
            bool interior = true;
            for (int side = 0; side < 4; ++side)
                interior = interior && active(mask, width, depth, x + dx[side], y + dy[side]);
            if (interior) return std::pair<int, int>{x, y};
        }
    return std::nullopt;
}

/** @brief Axis-aligned interior partition of one floor's active cells into rooms. */
void partitionFloor(int width, int depth, const std::vector<uint8_t> &mask,
                    const std::vector<std::string> &roomTypes, std::mt19937 &rng,
                    const std::vector<std::reference_wrapper<const HouseComponent>> &innerWall,
                    const std::vector<std::reference_wrapper<const HouseComponent>> &innerDoor,
                    std::vector<HouseInstance> &instances, std::vector<HouseRoom> &rooms, int floorZ) {
    struct Rect {
        int x0, y0, x1, y1;
    };
    // Bound the number of rooms by the requested list and the floor area.
    int activeArea = 0;
    for (const uint8_t cell : mask) activeArea += cell != 0 ? 1 : 0;
    const int target = std::clamp(int(roomTypes.size()), 1, std::max(1, activeArea / 3));
    if (target <= 1) return;

    std::vector<Rect> leaves;
    const std::function<void(Rect, int)> split = [&](Rect rect, int count) {
        if (count <= 1 || rect.x1 - rect.x0 < 2 || rect.y1 - rect.y0 < 2) {
            leaves.push_back(rect);
            return;
        }
        const bool vertical = (rect.x1 - rect.x0) >= (rect.y1 - rect.y0);
        if (vertical) {
            const int pos = rect.x0 + (rect.x1 - rect.x0) / 2;
            split({rect.x0, rect.y0, pos - 1, rect.y1}, count / 2);
            split({pos + 1, rect.y0, rect.x1, rect.y1}, count - count / 2);
            // Emit the interior wall run (rotation 90 = runs along Y) with one door gap.
            int gapY = rect.y0 + (rect.y1 - rect.y0) / 2;
            for (int y = rect.y0; y <= rect.y1; ++y) {
                if (!active(mask, width, depth, pos, y)) continue;
                if (y == gapY && active(mask, width, depth, pos - 1, y) && active(mask, width, depth, pos + 1, y)) {
                    if (const auto *door = pick(innerDoor, rng))
                        instances.push_back({door->id, pos, y, floorZ, 90});
                } else if (const auto *wall = pick(innerWall, rng)) {
                    instances.push_back({wall->id, pos, y, floorZ, 90});
                }
            }
        } else {
            const int pos = rect.y0 + (rect.y1 - rect.y0) / 2;
            split({rect.x0, rect.y0, rect.x1, pos - 1}, count / 2);
            split({rect.x0, pos + 1, rect.x1, rect.y1}, count - count / 2);
            int gapX = rect.x0 + (rect.x1 - rect.x0) / 2;
            for (int x = rect.x0; x <= rect.x1; ++x) {
                if (!active(mask, width, depth, x, pos)) continue;
                if (x == gapX && active(mask, width, depth, x, pos - 1) && active(mask, width, depth, x, pos + 1)) {
                    if (const auto *door = pick(innerDoor, rng))
                        instances.push_back({door->id, x, pos, floorZ, 0});
                } else if (const auto *wall = pick(innerWall, rng)) {
                    instances.push_back({wall->id, x, pos, floorZ, 0});
                }
            }
        }
    };
    int minX = width, minY = depth, maxX = -1, maxY = -1;
    for (int y = 0; y < depth; ++y)
        for (int x = 0; x < width; ++x)
            if (active(mask, width, depth, x, y)) {
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
            }
    if (maxX < minX || maxY < minY) return;
    split({minX, minY, maxX, maxY}, target);
    std::sort(leaves.begin(), leaves.end(),
              [](const Rect &a, const Rect &b) { return std::tie(a.y0, a.x0) < std::tie(b.y0, b.x0); });
    for (size_t i = 0; i < leaves.size(); ++i) {
        const auto &rect = leaves[i];
        const std::string type = roomTypes[std::min(i, roomTypes.size() - 1)];
        rooms.push_back({type, rect.x0, rect.y0, floorZ, rect.x1 - rect.x0 + 1, rect.y1 - rect.y0 + 1});
    }
}

}  // namespace

eve::Result<void> HouseGenerator::generate(const HouseRequest &r, HouseLayout &out) const {
    HouseLayout generated;
    if (r.width < 3 || r.depth < 3 || r.floors < 1 || r.maxAttempts < 1) {
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "house needs a 3x3 plot, at least one floor and one attempt");
    }
    if (!oneOf(r.footprint, {"auto", "rectangle", "l_shape", "t_shape", "polygon"}) ||
        !oneOf(r.roof, {"auto", "gable", "flat", "shed"}) ||
        !oneOf(r.entrance, {"auto", "north", "east", "south", "west"})) {
        return failure<void>(eve::DiagnosticCode::Unsupported, "unsupported footprint, roof or entrance mode");
    }
    if (r.footprint == "polygon" && r.perimeter.size() < 3) {
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "polygon footprint needs at least 3 perimeter points");
    }
    const auto foundation = library_.byCategory("foundation", r.style);
    const auto floor      = library_.byCategory("floor", r.style);
    const auto wall       = library_.byCategory("wall", r.style);
    const auto door       = library_.byCategory("door", r.style);
    const auto roof       = library_.byCategory("roof", r.style);
    if (!has(foundation) || !has(floor) || !has(wall) || !has(door) || !has(roof)) {
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "library needs foundation, floor, wall, door and roof categories");
    }
    // A named style must be a complete pack; otherwise the per-category fallback would
    // silently mix it with unstyled components into a broken house.
    if (!r.style.empty() && !library_.hasCompletePack(r.style)) {
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "style pack '" + r.style +
                                 "' is incomplete (needs foundation, floor, wall, door and roof)");
    }
    // Interior partition and stairwell categories are optional: interior walls/doors are used
    // only for an explicit multi-room request, stairs only for multi-floor houses.
    const auto stairs    = library_.byCategory("stairs", r.style);
    const auto innerWall = library_.byCategory("interior_wall", r.style);
    const auto innerDoor = library_.byCategory("interior_door", r.style);
    const bool partitionsInteriors =
        r.requiredRooms.size() > 1 && has(innerWall) && has(innerDoor);

    std::mt19937 rng(r.seed);
    generated.seed                            = r.seed;
    generated.moduleSize                      = r.moduleSize;
    generated.floorHeight                     = r.floorHeight;
    static constexpr const char *shapes[] = {"rectangle", "l_shape", "t_shape"};
    static constexpr const char *roofs[] = {"gable", "flat", "shed"};
    static constexpr SocketDirection sides[] = {SocketDirection::North, SocketDirection::East,
                                                 SocketDirection::South, SocketDirection::West};
    generated.footprintStyle = r.footprint == "auto"   ? shapes[rng() % 3]
                               : r.footprint == "polygon" ? "polygon"
                                                           : r.footprint;
    generated.roofStyle                       = r.roof == "auto" ? roofs[rng() % 3] : r.roof;
    const SocketDirection entranceDirection =
        r.entrance == "auto" ? sides[rng() % 4] : directionFromName(r.entrance);
    generated.entranceSide = directionName(entranceDirection);

    // A multi-floor house needs a vertical stair column through an interior cell.
    const bool isPolygon = generated.footprintStyle == "polygon";
    const auto makeMask = [&](int ins) {
        return isPolygon ? polygonMask(r.perimeter, r.width, r.depth, 0)
                         : footprintMask(generated.footprintStyle, r.width, r.depth, ins);
    };
    const auto baseMask = makeMask(0);
    std::optional<std::pair<int, int>> stairwell;
    if (r.floors > 1) {
        stairwell = findInteriorCell(baseMask, r.width, r.depth);
        if (!stairwell)
            return failure<void>(eve::DiagnosticCode::Unsupported,
                                 "multi-floor house needs an interior cell for a stairwell");
        if (!has(stairs))
            return failure<void>(eve::DiagnosticCode::NotFound,
                                 "multi-floor house needs a stairs category in the library");
    }

    // Grammar pass: construct a connected footprint mask, then emit one module per exposed face.
    // Upper masks are monotonically inset, preserving a direct vertical support chain.
    int inset = 0;
    std::vector<uint8_t> previousMask;
    for (int z = 0; z < r.floors; ++z) {
        // Upper floors may step inward, but can never expand again above an inset floor. This
        // gives every floor cell a direct support chain to the foundation.
        if (!isPolygon && z > 0 && inset == 0 && r.width > 4 && r.depth > 4 && (rng() & 3u) == 0u)
            inset = 1;
        const auto mask = makeMask(inset);
        // Any lower-floor cell not covered by this floor becomes a roof terrace/canopy at the
        // current level. Thus every floor cell is covered by either another floor or a roof.
        if (!previousMask.empty()) {
            for (int y = 0; y < r.depth; ++y) for (int x = 0; x < r.width; ++x) {
                if (active(previousMask, r.width, r.depth, x, y) &&
                    !active(mask, r.width, r.depth, x, y))
                    generated.instances.push_back({pick(roof, rng)->id, x, y, z, 0});
            }
        }
        int minX = r.width, minY = r.depth, maxX = -1, maxY = -1;
        for (int y = 0; y < r.depth; ++y) for (int x = 0; x < r.width; ++x) if (active(mask, r.width, r.depth, x, y)) {
            minX = std::min(minX, x); minY = std::min(minY, y);
            maxX = std::max(maxX, x); maxY = std::max(maxY, y);
            // The stair column replaces foundation+floor on every level it passes through.
            if (stairwell && x == stairwell->first && y == stairwell->second) {
                generated.instances.push_back({pick(stairs, rng)->id, x, y, z, 0});
                continue;
            }
            if (z == 0) generated.instances.push_back({pick(foundation, rng)->id, x, y, z, 0});
            generated.instances.push_back({pick(floor, rng)->id, x, y, z, 0});
        }
        if (maxX < minX || maxY < minY)
            return failure<void>(eve::DiagnosticCode::Failed, "footprint collapsed after inset");
        if (!partitionsInteriors)
            generated.rooms.push_back({z == 0 ? "living" : "upper", minX, minY, z,
                                       maxX - minX + 1, maxY - minY + 1});

        using Face = std::tuple<int, int, SocketDirection>;
        std::vector<Face> faces;
        std::vector<Face> entranceCandidates;
        std::unordered_map<int, int> facesPerCell;
        for (int y = 0; y < r.depth; ++y) for (int x = 0; x < r.width; ++x) {
            if (!active(mask, r.width, r.depth, x, y)) continue;
            const SocketDirection directions[] = {SocketDirection::North, SocketDirection::East,
                                                   SocketDirection::South, SocketDirection::West};
            const int dx[] = {0, 1, 0, -1};
            const int dy[] = {-1, 0, 1, 0};
            for (int side = 0; side < 4; ++side) if (!active(mask, r.width, r.depth, x + dx[side], y + dy[side])) {
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
            const bool isEntrance = z == 0 && faceData == entranceFace;
            const int rotation = directionRotation(face);
            auto choices = compatibleOnFace(isEntrance ? door : wall, face, rotation);
            if (!isEntrance) {
                const bool corner = facesPerCell[y * r.width + x] > 1;
                const bool besideEntrance = z == 0 &&
                    std::abs(x - std::get<0>(entranceFace)) + std::abs(y - std::get<1>(entranceFace)) <= 1;
                const int facadeAxis = (face == SocketDirection::North || face == SocketDirection::South) ? x : y;
                const bool rhythmicWindow = ((facadeAxis + int(r.seed & 1u)) & 1) == 0;
                choices = facadeVariant(choices, !corner && !besideEntrance && rhythmicWindow);
            }
            const auto *selected = pick(choices, rng);
            if (!selected) {
                return failure<void>(
                    eve::DiagnosticCode::NotFound,
                    "no socket-compatible " + std::string(isEntrance ? "door" : "wall") + " component");
            }
            generated.instances.push_back({selected->id, x, y, z, rotation});
        }
        if (partitionsInteriors) {
            partitionFloor(r.width, r.depth, mask, r.requiredRooms, rng, innerWall, innerDoor,
                           generated.instances, generated.rooms, z);
        }
        if (z + 1 == r.floors) {
            for (int y = 0; y < r.depth; ++y) for (int x = 0; x < r.width; ++x)
                    if (active(mask, r.width, r.depth, x, y))
                        generated.instances.push_back({pick(roof, rng)->id, x, y, z + 1, 0});
        }
        previousMask = mask;
    }
    if (!partitionsInteriors && !r.requiredRooms.empty() && r.requiredRooms.front() != "living")
        generated.diagnostics.push_back(
            "multi-room request fell back to a single room per floor (no interior partition components)");
    auto validated = generated.validate(library_);
    if (!validated.ok()) return validated;
    out = std::move(generated);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::housegen
