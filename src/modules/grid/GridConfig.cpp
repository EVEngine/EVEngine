#include "grid/GridConfig.h"

namespace eve::grid {

const char *GridConfig::layoutName(GridLayout l) {
    switch (l) {
        case GridLayout::Rectangle:
            return "rectangle";
        case GridLayout::Hexagon:
            return "hexagon";
        case GridLayout::Isometric:
            return "isometric";
        case GridLayout::Staggered:
            return "staggered";
        case GridLayout::IsometricZAsY:
            return "isometric-z-as-y";
    }
    return "rectangle";
}

GridLayout GridConfig::layoutFromName(const std::string &name) {
    if (name == "hexagon" || name == "hex") return GridLayout::Hexagon;
    if (name == "isometric" || name == "iso") return GridLayout::Isometric;
    if (name == "staggered") return GridLayout::Staggered;
    if (name == "isometric-z-as-y" || name == "iso-z-as-y") return GridLayout::IsometricZAsY;
    return GridLayout::Rectangle;
}

const char *GridConfig::planeName(GridPlane p) {
    return p == GridPlane::XZ ? "xz" : "xy";
}

GridPlane GridConfig::planeFromName(const std::string &name) {
    return name == "xz" ? GridPlane::XZ : GridPlane::XY;
}

}  // namespace eve::grid
