#include "procgen/TilePreset.h"

#include <algorithm>
#include <array>

namespace eve::procgen {
namespace {

template <std::size_t N>
bool contains(const std::array<std::uint16_t, N>& values, std::uint16_t value) noexcept {
    return std::find(values.begin(), values.end(), value) != values.end();
}

constexpr std::array<std::uint16_t, 32> cornerFill   = {27,  31,  54,  55,  91,  95,  118, 119, 216, 217, 220,
                                                        221, 283, 287, 310, 311, 347, 351, 374, 375, 432, 433,
                                                        436, 437, 472, 473, 476, 477, 496, 497, 500, 501};
constexpr std::array<std::uint16_t, 31> cornerWay    = {26,  30,  50,  51,  90,  94,  114, 115, 152, 153, 156,
                                                        157, 176, 177, 180, 181, 240, 241, 244, 245, 282, 286,
                                                        306, 307, 346, 370, 371, 408, 409, 412, 413};
constexpr std::array<std::uint16_t, 32> edgeWay      = {56,  57,  60,  61,  120, 121, 124, 125, 146, 147, 150,
                                                        151, 210, 211, 214, 215, 312, 313, 316, 317, 376, 377,
                                                        380, 381, 402, 403, 406, 407, 466, 467, 470, 471};
constexpr std::array<std::uint16_t, 16> edgeFill     = {63,  127, 219, 223, 319, 383, 438, 439,
                                                        475, 479, 502, 503, 504, 505, 508, 509};
constexpr std::array<std::uint16_t, 16> threeWay     = {58,  122, 154, 158, 178, 179, 184, 185,
                                                        188, 189, 242, 243, 314, 378, 410, 414};
constexpr std::array<std::uint16_t, 4>  threeWayFill = {191, 251, 446, 506};
constexpr std::array<std::uint16_t, 64> deadEnd      = {
    18,  19,  22,  23,  24,  25,  28,  29,  48,  49,  52,  53,  82,  83,  86,  87,  88,  89,  92,  93,  112, 113,
    116, 117, 144, 145, 148, 149, 208, 209, 212, 213, 274, 275, 278, 279, 280, 281, 284, 285, 304, 305, 308, 309,
    338, 339, 342, 343, 344, 345, 348, 349, 368, 369, 372, 373, 400, 401, 404, 405, 464, 465, 468, 469};
constexpr std::array<std::uint16_t, 16> single         = {16,  17,  20,  21,  80,  81,  84,  85,
                                                          272, 273, 276, 277, 336, 337, 340, 341};
constexpr std::array<std::uint16_t, 32> edgeCornerFill = {59,  62,  123, 126, 155, 159, 182, 183, 218, 222, 246,
                                                          247, 248, 249, 252, 253, 315, 318, 379, 382, 411, 415,
                                                          434, 435, 440, 441, 444, 445, 474, 478, 498, 499};
constexpr std::array<std::uint16_t, 4>  threeCorner    = {187, 190, 250, 442};
constexpr std::array<std::uint16_t, 2>  doubleCorner   = {254, 443};
constexpr std::array<std::uint16_t, 4>  interiorCorner = {255, 447, 507, 510};

constexpr std::array<std::uint16_t, 15> rotationZero = {312, 120, 56,  24,  89,  280, 58, 152,
                                                        186, 95,  218, 222, 434, 474, 498};
constexpr std::array<std::uint16_t, 65> rotation90   = {
    22,  23,  18,  19,  82,  83,  86,  87,  274, 275, 278, 279, 338, 339, 342, 343, 146, 147, 151, 210, 211, 214,
    215, 403, 406, 407, 466, 467, 470, 471, 438, 439, 502, 503, 178, 179, 242, 243, 191, 54,  59,  123, 248, 249,
    252, 253, 315, 379, 55,  118, 310, 311, 374, 375, 26,  30,  90,  94,  119, 282, 286, 346, 507, 254, 250};
constexpr std::array<std::uint16_t, 51> rotation180 = {48,  49,  52,  53,  112, 113, 116, 117, 304, 305, 308, 309, 368,
                                                       369, 372, 373, 50,  51,  114, 115, 306, 307, 370, 371, 504, 505,
                                                       508, 509, 432, 433, 436, 437, 496, 497, 500, 501, 187, 184, 185,
                                                       188, 189, 446, 155, 159, 182, 183, 246, 247, 411, 415, 255};
constexpr std::array<std::uint16_t, 53> rotation270 = {
    144, 145, 148, 149, 208, 209, 212, 213, 400, 401, 404, 405, 464, 465, 468, 469, 154, 158,
    410, 414, 506, 216, 217, 220, 472, 476, 473, 477, 176, 177, 180, 181, 221, 240, 241, 244,
    245, 190, 150, 402, 223, 219, 475, 479, 62,  126, 318, 382, 440, 441, 444, 445, 447};
constexpr std::array<std::uint16_t, 16> mirrorX = {59,  123, 182, 183, 218, 222, 246, 247,
                                                   315, 379, 440, 441, 444, 445, 474, 478};

TilePresetKind classify(std::uint16_t c) noexcept {
    if (contains(cornerWay, c)) return TilePresetKind::CornerWay;
    if (contains(cornerFill, c)) return TilePresetKind::CornerFill;
    if (contains(edgeWay, c)) return TilePresetKind::EdgeWay;
    if (contains(edgeFill, c)) return TilePresetKind::EdgeFill;
    if (c == 511) return TilePresetKind::Fill;
    if (contains(single, c)) return TilePresetKind::Single;
    if (contains(threeWay, c)) return TilePresetKind::ThreeWay;
    if (contains(threeWayFill, c)) return TilePresetKind::ThreeWayFill;
    if (contains(threeCorner, c)) return TilePresetKind::ThreeCorner;
    if (contains(deadEnd, c)) return TilePresetKind::DeadEnd;
    if (c == 186) return TilePresetKind::FourWay;
    if (contains(edgeCornerFill, c)) return TilePresetKind::EdgeCornerFill;
    if (contains(doubleCorner, c)) return TilePresetKind::DoubleCorner;
    if (contains(interiorCorner, c)) return TilePresetKind::InteriorCorner;
    return TilePresetKind::None;
}

}  // namespace

std::uint16_t toTilePresetConfiguration(std::uint8_t mask) noexcept {
    std::uint16_t c = 1u << 4u;
    if (mask & (1u << 0u)) c |= 1u << 1u;  // N -> top
    if (mask & (1u << 1u)) c |= 1u << 5u;  // E -> right
    if (mask & (1u << 2u)) c |= 1u << 7u;  // S -> bottom
    if (mask & (1u << 3u)) c |= 1u << 3u;  // W -> left
    if (mask & (1u << 4u)) c |= 1u << 2u;  // NE -> top-right
    if (mask & (1u << 5u)) c |= 1u << 8u;  // SE -> bottom-right
    if (mask & (1u << 6u)) c |= 1u << 6u;  // SW -> bottom-left
    if (mask & (1u << 7u)) c |= 1u << 0u;  // NW -> top-left
    return c;
}

TilePresetVariant resolveTilePreset(std::uint8_t mask) noexcept {
    const auto c        = toTilePresetConfiguration(mask);
    int        rotation = 0;
    if (contains(rotation90, c))
        rotation = 90;
    else if (contains(rotation180, c))
        rotation = 180;
    else if (contains(rotation270, c))
        rotation = 270;
    else if (contains(rotationZero, c))
        rotation = 0;
    return {classify(c), rotation, contains(mirrorX, c), c};
}

std::string_view tilePresetKindName(TilePresetKind kind) noexcept {
    switch (kind) {
        case TilePresetKind::DeadEnd: return "dead_end";
        case TilePresetKind::Single: return "single";
        case TilePresetKind::Fill: return "fill";
        case TilePresetKind::CornerWay: return "corner_way";
        case TilePresetKind::CornerFill: return "corner_fill";
        case TilePresetKind::InteriorCorner: return "interior_corner";
        case TilePresetKind::DoubleCorner: return "double_corner";
        case TilePresetKind::EdgeWay: return "edge_way";
        case TilePresetKind::EdgeFill: return "edge_fill";
        case TilePresetKind::ThreeWay: return "three_way";
        case TilePresetKind::ThreeWayFill: return "three_way_fill";
        case TilePresetKind::EdgeCornerFill: return "edge_corner_fill";
        case TilePresetKind::ThreeCorner: return "three_corner";
        case TilePresetKind::FourWay: return "four_way";
        case TilePresetKind::None: return "none";
    }
    return "none";
}

}  // namespace eve::procgen
