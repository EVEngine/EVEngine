#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

#include <algorithm>
#include <random>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::housegen {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message,
                      std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "housegen.generate"));
}

const HouseComponent *pick(
    const std::vector<std::reference_wrapper<const HouseComponent>> &choices,
    std::mt19937 &rng) {
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

bool has(const std::vector<std::reference_wrapper<const HouseComponent>> &values) {
    return !values.empty();
}

bool isWindowComponent(const HouseComponent *component) {
    if (!component) return false;
    if (component->id.find("window") != std::string::npos) return true;
    return std::find(component->tags.begin(), component->tags.end(), "window") != component->tags.end();
}

std::vector<std::reference_wrapper<const HouseComponent>> facadeVariant(
    const std::vector<std::reference_wrapper<const HouseComponent>> &choices,
    bool wantWindow) {
    bool hasWindow = false, hasSolid = false;
    for (const auto &choice : choices) {
        if (isWindowComponent(&choice.get())) hasWindow = true;
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

SocketDirection rotate(SocketDirection direction, int degrees) {
    if (direction == SocketDirection::Up || direction == SocketDirection::Down) return direction;
    int side = direction == SocketDirection::North ? 0 : direction == SocketDirection::East ? 1 :
               direction == SocketDirection::South ? 2 : 3;
    side = (side + degrees / 90) % 4;
    return side == 0 ? SocketDirection::North : side == 1 ? SocketDirection::East :
           side == 2 ? SocketDirection::South : SocketDirection::West;
}

std::vector<std::reference_wrapper<const HouseComponent>> compatibleOnFace(
    const std::vector<std::reference_wrapper<const HouseComponent>> &choices,
    SocketDirection face, int rotation) {
    std::vector<std::reference_wrapper<const HouseComponent>> out;
    for (const auto &choice : choices) {
        const auto &c = choice.get();
        // Components without sockets are intentionally wildcard-compatible. This preserves
        // compatibility with small legacy kits while new player kits opt into strict sockets.
        if (c.sockets.empty()) { out.push_back(choice); continue; }
        for (const auto &socket : c.sockets) {
            if (rotate(socket.direction, rotation) == face && !socket.type.empty()) {
                out.push_back(choice);
                break;
            }
        }
    }
    return out;
}

}  // namespace

eve::Result<void> HouseGenerator::generate(const HouseRequest &r, HouseLayout &out) const {
    HouseLayout generated;
    if (r.width < 3 || r.depth < 3 || r.floors < 1 || r.maxAttempts < 1) {
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "house needs a 3x3 plot, at least one floor and one attempt");
    }
    if (!oneOf(r.footprint, {"auto", "rectangle", "l_shape", "t_shape"}) ||
        !oneOf(r.roof, {"auto", "gable", "flat", "shed"}) ||
        !oneOf(r.entrance, {"auto", "north", "east", "south", "west"})) {
        return failure<void>(eve::DiagnosticCode::Unsupported,
                             "unsupported footprint, roof or entrance mode");
    }
    const auto foundation = library_.byCategory("foundation", r.style);
    const auto floor = library_.byCategory("floor", r.style);
    const auto wall = library_.byCategory("wall", r.style);
    const auto door = library_.byCategory("door", r.style);
    const auto roof = library_.byCategory("roof", r.style);
    if (!has(foundation) || !has(floor) || !has(wall) || !has(door) || !has(roof)) {
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "library needs foundation, floor, wall, door and roof categories");
    }

    std::mt19937 rng(r.seed);
    generated.seed = r.seed;
    generated.moduleSize = r.moduleSize;
    generated.floorHeight = r.floorHeight;
    static constexpr const char *shapes[] = {"rectangle", "l_shape", "t_shape"};
    static constexpr const char *roofs[] = {"gable", "flat", "shed"};
    static constexpr SocketDirection sides[] = {SocketDirection::North, SocketDirection::East,
                                                 SocketDirection::South, SocketDirection::West};
    generated.footprintStyle = r.footprint == "auto" ? shapes[rng() % 3] : r.footprint;
    generated.roofStyle = r.roof == "auto" ? roofs[rng() % 3] : r.roof;
    const SocketDirection entranceDirection =
        r.entrance == "auto" ? sides[rng() % 4] : directionFromName(r.entrance);
    generated.entranceSide = directionName(entranceDirection);

    // Grammar pass: construct a connected footprint mask, then emit one module per exposed face.
    // Upper masks are monotonically inset, preserving a direct vertical support chain.
    int inset = 0;
    std::vector<uint8_t> previousMask;
    for (int z = 0; z < r.floors; ++z) {
        // Upper floors may step inward, but can never expand again above an inset floor. This
        // gives every floor cell a direct support chain to the foundation.
        if (z > 0 && inset == 0 && r.width > 4 && r.depth > 4 && (rng() & 3u) == 0u)
            inset = 1;
        const auto mask = footprintMask(generated.footprintStyle, r.width, r.depth, inset);
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
            if (z == 0) generated.instances.push_back({pick(foundation, rng)->id, x, y, z, 0});
            generated.instances.push_back({pick(floor, rng)->id, x, y, z, 0});
        }
        if (maxX < minX || maxY < minY)
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "footprint collapsed after inset");
        generated.rooms.push_back({z == 0 ? "living" : "upper", minX, minY,
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
                return failure<void>(eve::DiagnosticCode::NotFound,
                                     "no socket-compatible " +
                                         std::string(isEntrance ? "door" : "wall") + " component");
            }
            generated.instances.push_back({selected->id, x, y, z, rotation});
        }
        if (z + 1 == r.floors) {
            for (int y = 0; y < r.depth; ++y) for (int x = 0; x < r.width; ++x)
                if (active(mask, r.width, r.depth, x, y)) generated.instances.push_back({pick(roof, rng)->id, x, y, z + 1, 0});
        }
        previousMask = mask;
    }
    if (!r.requiredRooms.empty() && r.requiredRooms.front() != "living")
        generated.diagnostics.push_back(
            "requested room types beyond living/upper are approximated by the base grammar");
    auto validated = generated.validate(library_);
    if (!validated.ok()) return validated;
    out = std::move(generated);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::housegen
