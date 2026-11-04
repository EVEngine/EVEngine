// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "probe",
  "default": "render-window",
  "module": "asset_graphics"
}
*/
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string_view>
#include "RoadSideParityProbe.inc"
#include "common/CrashHandler.h"

int main(int argc, char** argv) try {
    eve::installCrashHandler();
    if (argc != 3 ||
        (std::string_view(argv[1]) != "--road-side-parity" && std::string_view(argv[1]) != "--road-side-parity-yaw45" &&
         std::string_view(argv[1]) != "--road-side-parity-yaw45-wrong-sign" &&
         std::string_view(argv[1]) != "--road-side-parity-grade" &&
         std::string_view(argv[1]) != "--road-side-parity-curve")) {
        std::cerr
            << "usage: road_side_graphics_probe "
               "(--road-side-parity|--road-side-parity-yaw45|--road-side-parity-yaw45-wrong-sign) <output-prefix>\n";
        return 2;
    }
    const float yaw   = std::string_view(argv[1]) == "--road-side-parity-yaw45"              ? -45.f
                        : std::string_view(argv[1]) == "--road-side-parity-yaw45-wrong-sign" ? 45.f
                                                                                             : 0.f;
    const int   shape = std::string_view(argv[1]) == "--road-side-parity-grade"   ? 1
                        : std::string_view(argv[1]) == "--road-side-parity-curve" ? 2
                                                                                  : 0;
    return runRoadSideParity(argv[2], yaw, shape);
} catch (const std::exception& error) {
    std::cerr << "road side graphics probe failed: " << error.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "road side graphics probe failed: unclassified native exception\n";
    return 1;
}
