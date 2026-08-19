#pragma once

#include <chrono>
#include <cstdio>

namespace eve {

// Prints "[startup] <name>: X.X ms" to stderr when the scope exits.
// Temporary diagnostics for startup profiling; remove once startup is fast.
class StartupStage {
public:
    explicit StartupStage(const char* name)
        : name_(name), t0_(std::chrono::steady_clock::now()) {}

    ~StartupStage() {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_)
                .count();
        std::fprintf(stderr, "[startup] %s: %.1f ms\n", name_, ms);
    }

    StartupStage(const StartupStage&) = delete;
    StartupStage& operator=(const StartupStage&) = delete;

private:
    const char* name_;
    std::chrono::steady_clock::time_point t0_;
};

}  // namespace eve
