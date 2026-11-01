#pragma once
#include <cstddef>
#include <string_view>

namespace eve::graphics {
class Graphics;
}

/** @brief Run one isolated native 3D scene/animation/render workload; throws on invalid output. */
int runGameplayBenchmark(eve::graphics::Graphics& graphics, std::string_view workload, int frames, std::size_t props);
