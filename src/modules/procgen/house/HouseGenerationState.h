#pragma once
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

namespace eve::housegen::house_detail {
/** @brief Borrow a weighted component from library-backed choices.
 * @ownership Borrowed; the session's library owns the component.
 * @nullable Yes when no compatible component exists.
 * @lifetime Until the owning session library is replaced or destroyed; used only within one stage.
 * @thread Session owner thread only; no callbacks.
 */
const HouseComponent* pick(const std::vector<std::reference_wrapper<const HouseComponent>>& choices, std::mt19937& rng);
bool                  has(const std::vector<std::reference_wrapper<const HouseComponent>>& values);
bool                  isRotationAllowed(const HouseComponent& component, int rotation);
/** @brief Internal facadeVariant stage helper.
 * @cost Linear in component choices; amortized across one facade placement.
 */
std::vector<std::reference_wrapper<const HouseComponent>> facadeVariant(
    const std::vector<std::reference_wrapper<const HouseComponent>>& choices, bool wantWindow);
bool            isOneOf(const std::string& value, std::initializer_list<const char*> options);
SocketDirection directionFromName(const std::string& name);
std::string     directionName(SocketDirection direction);
int             directionRotation(SocketDirection direction);
/** @brief Internal footprintMask stage helper.
 * @cost Linear in plot cells; reuse for one floor emission.
 */
std::vector<uint8_t> footprintMask(const std::string& shape, int width, int depth, int inset);
bool                 active(const std::vector<uint8_t>& mask, int width, int depth, int x, int y);
/** @brief Internal polygonMask stage helper.
 * @cost Plot cells times polygon vertices; prepare once per floor.
 */
std::vector<uint8_t> polygonMask(const std::vector<HousePolygonPoint>& poly, int width, int depth, int inset);
/** @brief Internal compatibleOnFace stage helper.
 * @cost Component choices times sockets; used for one facade placement.
 */
std::vector<std::reference_wrapper<const HouseComponent>> compatibleOnFace(
    const std::vector<std::reference_wrapper<const HouseComponent>>& choices, SocketDirection face, int rotation);
/** @brief Internal findInteriorCell stage helper.
 * @cost Linear in plot cells; find one stair column per house.
 */
std::optional<std::pair<int, int>> findInteriorCell(const std::vector<uint8_t>& mask, int width, int depth);
void appendRoomRegions(int width, int depth, const std::vector<uint8_t>& mask, const std::string& roomType, int floorZ,
                       std::vector<HouseRoom>& rooms);
eve::Result<void> partitionFloor(int width, int depth, const std::vector<uint8_t>& mask,
                                 const std::vector<std::string>& roomTypes, std::mt19937& rng,
                                 const std::vector<std::reference_wrapper<const HouseComponent>>& innerWall,
                                 const std::vector<std::reference_wrapper<const HouseComponent>>& innerDoor,
                                 std::vector<HouseInstance>& instances, std::vector<HouseRoom>& rooms, int floorZ);

struct State {
    HouseRequest                       r;
    HouseComponentLibrary              library_;
    HouseLayout                        generated;
    std::mt19937                       rng;
    bool                               isPolygon = false;
    std::vector<uint8_t>               baseMask, previousMask;
    std::optional<std::pair<int, int>> stairwell;
    std::optional<procgen::Grid2D>     planned;
    int                                inset = 0, z = 0;
    [[nodiscard]] eve::Result<void>    configure();
    [[nodiscard]] eve::Result<void>    plan();
    [[nodiscard]] eve::Result<void>    floor(const procgen::Grid2D& grid, const std::vector<std::string>& rooms);
    [[nodiscard]] eve::Result<void>    finish();
    /** @brief Internal makeMask stage helper.
     * @cost Linear in plot cells times polygon vertices for polygon footprints; reuse per floor.
     */
    [[nodiscard]] std::vector<uint8_t> makeMask(int ins) const;
};
}  // namespace eve::housegen::house_detail
