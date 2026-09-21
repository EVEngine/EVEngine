#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <random>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::housegen {
namespace {

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

bool allowsRotation(const HouseComponent& component, int rotation) {
    const int normalized = (rotation % 360 + 360) % 360;
    return std::any_of(component.rotations.begin(), component.rotations.end(),
                       [&](int allowed) { return (allowed % 360 + 360) % 360 == normalized; });
}

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
        if (!allowsRotation(c, rotation)) continue;
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

void appendRoomRegions(int width, int depth, const std::vector<uint8_t>& mask, const std::string& roomType, int floorZ,
                       std::vector<HouseRoom>& rooms) {
    std::vector<uint8_t> claimed(mask.size(), 0);
    for (int y = 0; y < depth; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t origin = size_t(y * width + x);
            if (!mask[origin] || claimed[origin]) continue;
            int x1 = x;
            while (x1 + 1 < width && mask[size_t(y * width + x1 + 1)] && !claimed[size_t(y * width + x1 + 1)]) ++x1;
            int  y1     = y;
            bool extend = true;
            while (extend && y1 + 1 < depth) {
                for (int xx = x; xx <= x1; ++xx) {
                    if (!mask[size_t((y1 + 1) * width + xx)] || claimed[size_t((y1 + 1) * width + xx)]) {
                        extend = false;
                        break;
                    }
                }
                if (extend) ++y1;
            }
            rooms.push_back({roomType, x, y, floorZ, x1 - x + 1, y1 - y + 1});
            for (int yy = y; yy <= y1; ++yy)
                for (int xx = x; xx <= x1; ++xx) claimed[size_t(yy * width + xx)] = 1;
        }
    }
}

/** @brief Partition active cells into exact, rectangular rooms and emit mask-safe boundaries. */
eve::Result<void> partitionFloor(int width, int depth, const std::vector<uint8_t>& mask,
                                 const std::vector<std::string>& roomTypes, std::mt19937& rng,
                                 const std::vector<std::reference_wrapper<const HouseComponent>>& innerWall,
                                 const std::vector<std::reference_wrapper<const HouseComponent>>& innerDoor,
                                 std::vector<HouseInstance>& instances, std::vector<HouseRoom>& rooms, int floorZ) {
    struct Rect {
        int x0, y0, x1, y1;
        int area() const { return (x1 - x0 + 1) * (y1 - y0 + 1); }
    };
    int activeArea = 0;
    for (const uint8_t cell : mask) activeArea += cell != 0 ? 1 : 0;
    const int target = std::max(1, int(roomTypes.size()));
    if (target > std::max(1, activeArea / 3))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                                 "required room count cannot fit the active footprint",
                                                                 "requiredRooms", {}, "housegen.generate"));
    if (target <= 1) return eve::Result<void>::success();

    // Greedily decompose the mask into non-overlapping, fully active rectangles.
    std::vector<uint8_t> claimed(mask.size(), 0);
    std::vector<Rect>    rects;
    for (int y = 0; y < depth; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t origin = size_t(y * width + x);
            if (!mask[origin] || claimed[origin]) continue;
            int x1 = x;
            while (x1 + 1 < width && mask[size_t(y * width + x1 + 1)] && !claimed[size_t(y * width + x1 + 1)]) ++x1;
            int  y1     = y;
            bool extend = true;
            while (extend && y1 + 1 < depth) {
                for (int xx = x; xx <= x1; ++xx)
                    if (!mask[size_t((y1 + 1) * width + xx)] || claimed[size_t((y1 + 1) * width + xx)]) {
                        extend = false;
                        break;
                    }
                if (extend) ++y1;
            }
            rects.push_back({x, y, x1, y1});
            for (int yy = y; yy <= y1; ++yy)
                for (int xx = x; xx <= x1; ++xx) claimed[size_t(yy * width + xx)] = 1;
        }
    }
    if (int(rects.size()) > target)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported,
            "requested room count cannot represent the irregular footprint without outside cells", "requiredRooms", {},
            "housegen.generate"));

    while (int(rects.size()) < target) {
        auto splitIt = std::max_element(rects.begin(), rects.end(),
                                        [](const Rect& a, const Rect& b) { return a.area() < b.area(); });
        if (splitIt == rects.end() || splitIt->area() < 2)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "required room count cannot fit the active footprint",
                "requiredRooms", {}, "housegen.generate"));
        const Rect source = *splitIt;
        rects.erase(splitIt);
        if ((source.x1 - source.x0) >= (source.y1 - source.y0) && source.x0 < source.x1) {
            const int cut = source.x0 + (source.x1 - source.x0) / 2;
            rects.push_back({source.x0, source.y0, cut, source.y1});
            rects.push_back({cut + 1, source.y0, source.x1, source.y1});
        } else {
            const int cut = source.y0 + (source.y1 - source.y0) / 2;
            rects.push_back({source.x0, source.y0, source.x1, cut});
            rects.push_back({source.x0, cut + 1, source.x1, source.y1});
        }
    }
    std::sort(rects.begin(), rects.end(),
              [](const Rect& a, const Rect& b) { return std::tie(a.y0, a.x0) < std::tie(b.y0, b.x0); });

    std::vector<int> roomAt(size_t(width * depth), -1);
    for (size_t i = 0; i < rects.size(); ++i) {
        const Rect& rect = rects[i];
        rooms.push_back({roomTypes[i], rect.x0, rect.y0, floorZ, rect.x1 - rect.x0 + 1, rect.y1 - rect.y0 + 1});
        for (int y = rect.y0; y <= rect.y1; ++y)
            for (int x = rect.x0; x <= rect.x1; ++x) roomAt[size_t(y * width + x)] = int(i);
    }

    const auto choose = [&](const auto& components, int rotation) -> const HouseComponent* {
        std::vector<std::reference_wrapper<const HouseComponent>> allowed;
        for (const auto& component : components)
            if (allowsRotation(component.get(), rotation)) allowed.push_back(component);
        return pick(allowed, rng);
    };
    std::unordered_set<std::string> doorBoundaries;
    const auto emitBoundary = [&](int x, int y, int rotation, int roomA, int roomB) -> eve::Result<void> {
        const int             lo = std::min(roomA, roomB), hi = std::max(roomA, roomB);
        const std::string     boundary = std::to_string(lo) + ":" + std::to_string(hi) + ":" + std::to_string(rotation);
        const bool            door     = doorBoundaries.insert(boundary).second;
        const HouseComponent* component = choose(door ? innerDoor : innerWall, rotation);
        if (!component)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                       std::string("no interior ") + (door ? "door" : "wall") +
                                           " component allows rotation " + std::to_string(rotation),
                                       "rotations", {}, "housegen.generate"));
        instances.push_back({component->id, x, y, floorZ, rotation});
        return eve::Result<void>::success();
    };

    for (int y = 0; y < depth; ++y) {
        for (int x = 0; x < width; ++x) {
            const int room = roomAt[size_t(y * width + x)];
            if (room < 0) continue;
            if (x + 1 < width) {
                const int neighbour = roomAt[size_t(y * width + x + 1)];
                if (neighbour >= 0 && neighbour != room) {
                    auto emitted = emitBoundary(x + 1, y, 90, room, neighbour);
                    if (!emitted.ok()) return emitted;
                }
            }
            if (y + 1 < depth) {
                const int neighbour = roomAt[size_t((y + 1) * width + x)];
                if (neighbour >= 0 && neighbour != room) {
                    auto emitted = emitBoundary(x, y + 1, 0, room, neighbour);
                    if (!emitted.ok()) return emitted;
                }
            }
        }
    }
    return eve::Result<void>::success();
}

}  // namespace

eve::Result<void> HouseGenerator::generate(const HouseRequest &r, HouseLayout &out) const {
    HouseLayout generated;
    if (r.width < 3 || r.depth < 3 || r.floors < 1 || r.maxAttempts < 1) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "house needs a 3x3 plot, at least one floor and one attempt", {}, {},
            "housegen.generate"));
    }
    if (!std::isfinite(r.moduleSize) || r.moduleSize <= 0.f || !std::isfinite(r.floorHeight) || r.floorHeight <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "moduleSize and floorHeight must be finite and positive", {}, {},
            "housegen.generate"));
    if (!oneOf(r.footprint, {"auto", "rectangle", "l_shape", "t_shape", "polygon"}) ||
        !oneOf(r.roof, {"auto", "gable", "flat", "shed"}) ||
        !oneOf(r.entrance, {"auto", "north", "east", "south", "west"})) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                                 "unsupported footprint, roof or entrance mode", {}, {},
                                                                 "housegen.generate"));
    }
    if (r.footprint == "polygon" && r.perimeter.size() < 3) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "polygon footprint needs at least 3 perimeter points", {}, {},
            "housegen.generate"));
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
    const auto stairs    = library_.byCategory("stairs", r.style);
    const auto innerWall = library_.byCategory("interior_wall", r.style);
    const auto innerDoor = library_.byCategory("interior_door", r.style);
    const bool partitionsInteriors = r.requiredRooms.size() > 1;
    if (partitionsInteriors && (!has(innerWall) || !has(innerDoor)))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "multi-room generation needs interior_wall and interior_door categories",
            "requiredRooms", {}, "housegen.generate"));

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
    generated.footprintWidth = r.width;
    generated.footprintDepth = r.depth;
    generated.footprintMask  = baseMask;
    std::optional<std::pair<int, int>> stairwell;
    if (r.floors > 1 && has(stairs)) {
        stairwell = findInteriorCell(baseMask, r.width, r.depth);
        if (!stairwell)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "multi-floor house needs an interior cell for a stairwell", {},
                {}, "housegen.generate"));
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
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "footprint collapsed after inset", {}, {}, "housegen.generate"));
        if (!partitionsInteriors)
            appendRoomRegions(r.width, r.depth, mask, z == 0 ? "living" : "upper", z, generated.rooms);

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
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::NotFound,
                    "no socket-compatible " + std::string(isEntrance ? "door" : "wall") + " component", {}, {},
                    "housegen.generate"));
            }
            generated.instances.push_back({selected->id, x, y, z, rotation});
        }
        if (partitionsInteriors) {
            auto partitioned = partitionFloor(r.width, r.depth, mask, r.requiredRooms, rng, innerWall, innerDoor,
                                              generated.instances, generated.rooms, z);
            if (!partitioned.ok()) return eve::Result<void>::failure(partitioned.status());
        }
        if (z + 1 == r.floors) {
            for (int y = 0; y < r.depth; ++y) for (int x = 0; x < r.width; ++x)
                    if (active(mask, r.width, r.depth, x, y))
                        generated.instances.push_back({pick(roof, rng)->id, x, y, z + 1, 0});
        }
        previousMask = mask;
    }
    // Expand the canonical footprint to every cell occupied by rotated multi-cell components.
    int occupiedWidth = r.width, occupiedDepth = r.depth;
    for (const HouseInstance& instance : generated.instances) {
        const auto component = library_.find(instance.componentId);
        if (!component) continue;
        const int  rotation = (instance.rotationDeg % 360 + 360) % 360;
        const bool quarter  = (rotation / 90) % 2 != 0;
        occupiedWidth =
            std::max(occupiedWidth, instance.x + (quarter ? component->get().depth : component->get().width));
        occupiedDepth =
            std::max(occupiedDepth, instance.y + (quarter ? component->get().width : component->get().depth));
    }
    std::vector<uint8_t> occupiedMask(size_t(occupiedWidth * occupiedDepth), 0);
    for (int y = 0; y < r.depth; ++y)
        for (int x = 0; x < r.width; ++x)
            occupiedMask[size_t(y * occupiedWidth + x)] = baseMask[size_t(y * r.width + x)];
    for (const HouseInstance& instance : generated.instances) {
        const auto component = library_.find(instance.componentId);
        if (!component) continue;
        const int  rotation  = (instance.rotationDeg % 360 + 360) % 360;
        const bool quarter   = (rotation / 90) % 2 != 0;
        const int  cellWidth = quarter ? component->get().depth : component->get().width;
        const int  cellDepth = quarter ? component->get().width : component->get().depth;
        for (int y = 0; y < cellDepth; ++y)
            for (int x = 0; x < cellWidth; ++x)
                occupiedMask[size_t((instance.y + y) * occupiedWidth + instance.x + x)] = 1;
    }
    generated.footprintWidth = occupiedWidth;
    generated.footprintDepth = occupiedDepth;
    generated.footprintMask  = std::move(occupiedMask);
    auto validated = generated.validate(library_);
    if (!validated.ok()) return validated;
    out = std::move(generated);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::housegen
