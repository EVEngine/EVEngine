#pragma once

#include <cstdint>
#include <cstdio>
#include <span>

namespace test_pixelfx_diagnostic {
// Failure-only, lossless packed-RGBA runs. CI preserves stderr in LastTest.log;
// retaining both planes lets pixel evidence distinguish color from coverage.
inline void dumpPlane(const char* label, double time, const char* plane, std::span<const uint32_t> pixels) {
    std::fprintf(stderr, "PIXELFX_FRAME_BEGIN %s %.17g %s %zu\n", label, time, plane, pixels.size());
    size_t runs = 0;
    for (size_t i = 0; i < pixels.size();) {
        size_t end = i + 1;
        while (end < pixels.size() && pixels[end] == pixels[i]) ++end;
        std::fprintf(stderr, "%zu:%08lx ", end - i, static_cast<unsigned long>(pixels[i]));
        if (++runs % 64 == 0) std::fputc('\n', stderr);
        i = end;
    }
    std::fprintf(stderr, "\nPIXELFX_FRAME_END\n");
}
inline void dumpMismatch(const char* label, double time, std::span<const uint32_t> color,
                         std::span<const uint32_t> light) {
    dumpPlane(label, time, "color", color);
    dumpPlane(label, time, "light", light);
}
}  // namespace test_pixelfx_diagnostic
