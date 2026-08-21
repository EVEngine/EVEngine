#pragma once

// Shared test fixtures for EVEngine unit tests.
//
// Historically every GPU test file hand-rolled its own openGfxWindow() helper
// (16 copies with subtly different defaults). Tests should use the inline
// helpers here so window size overrides, cleanup and future headless/offscreen
// switches stay in one place.

#include "zeroerr/assert.h"

#include "graphics/Graphics.h"
#include "window/Window.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

/**
 * @brief Opens a real SDL window + Graphics pair (windowed backend).
 * @param win Out-parameter receiving the Window singleton (owned by the module).
 * @param gfx Out-parameter receiving the Graphics singleton (owned by the module).
 * @param w Logical width in pixels (env EVENGINE_TEST_VIEW_W overrides).
 * @param h Logical height in pixels (env EVENGINE_TEST_VIEW_H overrides).
 */
inline void openGfxWindow(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w = 320,
                          int h = 240) {
    const char *viewW = std::getenv("EVENGINE_TEST_VIEW_W");
    const char *viewH = std::getenv("EVENGINE_TEST_VIEW_H");
    if (viewW && viewW[0]) w = std::atoi(viewW);
    if (viewH && viewH[0]) h = std::atoi(viewH);
    if (w <= 0 || h <= 0) {
        // Invalid EVENGINE_TEST_VIEW_W/H; fail the test that requested a window.
        REQUIRE(false);
        return;
    }

    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = static_cast<uint16_t>(w);
    s.height   = static_cast<uint16_t>(h);
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

/** @brief RAII window + graphics pair; closes the window on destruction. */
struct GfxFixture {
    eve::window::Window      *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;

    explicit GfxFixture(int w = 320, int h = 240) { openGfxWindow(win, gfx, w, h); }
    ~GfxFixture() {
        if (win) win->close();
    }
};

/** @brief Unique per-test temporary directory, removed on destruction. */
class TempDir {
public:
    TempDir() {
        const auto base = std::filesystem::temp_directory_path();
        static std::atomic<uint64_t> counter{0};
        const uint64_t unique = (uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) << 16) ^
                                counter.fetch_add(1);
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto candidate = base / ("eve-test-" + std::to_string(unique) + "-" +
                                           std::to_string(attempt));
            if (!std::filesystem::exists(candidate)) {
                std::filesystem::create_directories(candidate);
                path_ = candidate;
                break;
            }
        }
        // Could not allocate a unique temp directory; fail loudly.
        REQUIRE(!path_.empty());
    }

    ~TempDir() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    const std::filesystem::path &path() const { return path_; }
    std::string                  str() const { return path_.string(); }

    TempDir(const TempDir &)            = delete;
    TempDir &operator=(const TempDir &) = delete;

private:
    std::filesystem::path path_;
};
