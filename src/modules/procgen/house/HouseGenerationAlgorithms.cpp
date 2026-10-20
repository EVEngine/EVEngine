#include "procgen/house/HouseGenerationState.h"

namespace eve::housegen::house_detail {
const HouseComponent *pick(const std::vector<std::reference_wrapper<const HouseComponent>> &choices,
                           std::mt19937                                                    &rng) {
    if (choices.empty()) return nullptr;
    int total = 0;
    for (const auto &choice : choices) total += choice.get().weight;
    std::uniform_int_distribution<int> roll(1, total);
    int                                target = roll(rng);
    for (const auto &choice : choices) {
        target -= choice.get().weight;
        if (target <= 0) return &choice.get();
    }
    return &choices.back().get();
}

bool has(const std::vector<std::reference_wrapper<const HouseComponent>> &values) { return !values.empty(); }

bool isRotationAllowed(const HouseComponent &component, int rotation) {
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
        else
            hasSolid = true;
    }
    if (!hasWindow || !hasSolid) return choices;
    std::vector<std::reference_wrapper<const HouseComponent>> filtered;
    for (const auto &choice : choices)
        if (isWindowComponent(&choice.get()) == wantWindow) filtered.push_back(choice);
    return filtered;
}

bool isOneOf(const std::string &value, std::initializer_list<const char *> options) {
    for (const char *option : options)
        if (value == option) return true;
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
    const int            minX = inset, minY = inset, maxX = width - 1 - inset, maxY = depth - 1 - inset;
    if (minX > maxX || minY > maxY) return mask;
    const int spanX = maxX - minX + 1, spanY = maxY - minY + 1;
    const int cutX      = minX + std::max(1, spanX / 2) - 1;
    const int cutY      = minY + std::max(1, spanY / 2) - 1;
    const int stemInset = spanX >= 5 ? 1 : 0;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            bool active = true;
            if (shape == "l_shape")
                active = !(x > cutX && y > cutY);
            else if (shape == "t_shape")
                active = y <= cutY || (x >= minX + stemInset && x <= maxX - stemInset);
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
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12f) + xi)) inside = !inside;
    }
    return inside;
}

/** @brief Rasterize an arbitrary closed polygon onto the cell grid (cell centers inside). */
std::vector<uint8_t> polygonMask(const std::vector<HousePolygonPoint> &poly, int width, int depth, int inset) {
    std::vector<uint8_t> mask(size_t(width * depth), 0);
    for (int y = inset; y < depth - inset; ++y)
        for (int x = inset; x < width - inset; ++x)
            if (pointInPolygon(float(x) + 0.5f, float(y) + 0.5f, poly)) mask[size_t(y * width + x)] = 1;
    return mask;
}

SocketDirection rotate(SocketDirection direction, int degrees) {
    if (direction == SocketDirection::Up || direction == SocketDirection::Down) return direction;
    int side = direction == SocketDirection::North   ? 0
               : direction == SocketDirection::East  ? 1
               : direction == SocketDirection::South ? 2
                                                     : 3;
    side     = (side + degrees / 90) % 4;
    return side == 0   ? SocketDirection::North
           : side == 1 ? SocketDirection::East
           : side == 2 ? SocketDirection::South
                       : SocketDirection::West;
}

std::vector<std::reference_wrapper<const HouseComponent>> compatibleOnFace(
    const std::vector<std::reference_wrapper<const HouseComponent>> &choices, SocketDirection face, int rotation) {
    std::vector<std::reference_wrapper<const HouseComponent>> out;
    for (const auto &choice : choices) {
        const auto &c = choice.get();
        if (!isRotationAllowed(c, rotation)) continue;
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

void appendRoomRegions(int width, int depth, const std::vector<uint8_t> &mask, const std::string &roomType, int floorZ,
                       std::vector<HouseRoom> &rooms) {
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
eve::Result<void> partitionFloor(int width, int depth, const std::vector<uint8_t> &mask,
                                 const std::vector<std::string> &roomTypes, std::mt19937 &rng,
                                 const std::vector<std::reference_wrapper<const HouseComponent>> &innerWall,
                                 const std::vector<std::reference_wrapper<const HouseComponent>> &innerDoor,
                                 std::vector<HouseInstance> &instances, std::vector<HouseRoom> &rooms, int floorZ) {
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
                                        [](const Rect &a, const Rect &b) { return a.area() < b.area(); });
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
              [](const Rect &a, const Rect &b) { return std::tie(a.y0, a.x0) < std::tie(b.y0, b.x0); });

    std::vector<int> roomAt(size_t(width * depth), -1);
    for (size_t i = 0; i < rects.size(); ++i) {
        const Rect &rect = rects[i];
        rooms.push_back({roomTypes[i], rect.x0, rect.y0, floorZ, rect.x1 - rect.x0 + 1, rect.y1 - rect.y0 + 1});
        for (int y = rect.y0; y <= rect.y1; ++y)
            for (int x = rect.x0; x <= rect.x1; ++x) roomAt[size_t(y * width + x)] = int(i);
    }

    const auto choose = [&](const auto &components, int rotation) -> const HouseComponent * {
        std::vector<std::reference_wrapper<const HouseComponent>> allowed;
        for (const auto &component : components)
            if (isRotationAllowed(component.get(), rotation)) allowed.push_back(component);
        return pick(allowed, rng);
    };
    std::unordered_set<std::string> doorBoundaries;
    const auto emitBoundary = [&](int x, int y, int rotation, int roomA, int roomB) -> eve::Result<void> {
        const int             lo = std::min(roomA, roomB), hi = std::max(roomA, roomB);
        const std::string     boundary = std::to_string(lo) + ":" + std::to_string(hi) + ":" + std::to_string(rotation);
        const bool            door     = doorBoundaries.insert(boundary).second;
        const HouseComponent *component = choose(door ? innerDoor : innerWall, rotation);
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


}  // namespace eve::housegen::house_detail
