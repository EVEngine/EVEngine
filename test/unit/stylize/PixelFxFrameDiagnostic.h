#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>
#include "stylize/PixelFx.h"

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
struct ExpectedFrame {
    double   time;
    uint32_t color, light;
};
inline uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t hash = 2166136261u;
    for (const auto pixel : pixels)
        for (unsigned shift = 0; shift < 32; shift += 8) hash = (hash ^ ((pixel >> shift) & 255u)) * 16777619u;
    return hash;
}
// Inspect the whole pinned scenario on an independent instance after a failure.
// The caller's original fatal assertions and frame remain untouched.
template <class FrameAt>
void dumpSequence(const char* label, const eve::stylize::PixelFx& source, size_t count, FrameAt frameAt) {
    using eve::stylize::PixelFx;
    const auto kind = source.info().kind;
    PixelFx    probe(kind);
    auto       configuration = PixelFx::prepareConfiguration(kind, source.settings());
    if (!configuration || !probe.commitConfiguration(std::move(configuration).takeValue())) {
        std::fprintf(stderr, "PIXELFX_FRAME_DIAGNOSTIC_FAILED %s configuration\n", label);
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const auto frame = frameAt(i);
        if (probe.isSimulated()) {
            auto seek = probe.prepareSeek(frame.time);
            if (!seek || !probe.commitSeek(std::move(seek).takeValue())) {
                std::fprintf(stderr, "PIXELFX_FRAME_DIAGNOSTIC_FAILED %s seek %.17g\n", label, frame.time);
                return;
            }
            probe.draw();
        } else {
            probe.render(frame.time);
        }
        if (checksum(probe.color()) != frame.color || checksum(probe.light()) != frame.light)
            dumpMismatch(label, frame.time, probe.color(), probe.light());
    }
}
}  // namespace test_pixelfx_diagnostic
