#pragma once

/**
 * @file StderrCapture.hpp
 * @brief Mirror process stderr into an in-process line sink without hiding it.
 *
 * The engine writes diagnostics through two disjoint sinks: C++ streams
 * (`std::cerr`, 27 call sites) and C stdio (`fprintf(stderr, ...)`, 39 call
 * sites). A streambuf swap would only cover the first, so this capture works one
 * level lower: descriptor 2 is redirected into a pipe and a drain thread copies
 * every byte back to the original descriptor while also splitting the stream
 * into lines for the sink. `printf`-style startup lines, Vulkan/driver warnings
 * and validation-layer messages therefore become readable through MCP without
 * changing any call site.
 *
 * Contracts:
 *  - Bytes reach the original stderr unchanged and in order.
 *  - Partial lines are held until their newline, or until the capture stops, so
 *    the sink only ever sees whole lines.
 *  - Stopping restores descriptor 2 first, which closes the last pipe writer; the
 *    drain thread then observes EOF and is joined. A capture is never left
 *    half-installed.
 *
 * @ownership The sink is copied into process-global state and stays owned by the
 *            capture until it stops; callers keep ownership of anything the
 *            closure itself captures.
 * @lifetime The sink is alive from a successful start until the capture stops;
 *            it must not be invoked after stop() returns.
 * @thread start/stop are process-global and must be driven by one owner, the
 *         DevTools console, from a single thread. The sink runs on the drain
 *         thread, so it must be thread-safe and must never write to stderr.
 * @reentrancy The sink does not run while start/stop hold the capture lock;
 *             registering no callback from inside the sink.
 */

#include <functional>
#include <string>

namespace eve::dev {

/** @brief Sink invoked on the drain thread for every complete stderr line. */
using StderrLineSink = std::function<void(const std::string&)>;

/** @brief Outcome of a capture request. */
enum class StderrCaptureStatus {
    /** Capture is running: started now, or already active. */
    Active,
    /** No sink was supplied; nothing was changed. */
    Rejected,
    /** The platform pipe could not be created; stderr is untouched. */
    Unavailable,
};

/**
 * @brief Start mirroring stderr into `sink`.
 * @param sink Invoked once per complete line on the drain thread.
 * @return A named status: `Active` also covers the already-active case, so the
 *         call is idempotent. `Unavailable` means stderr was left untouched and
 *         the console will only see script output.
 */
[[nodiscard]] StderrCaptureStatus startStderrCapture(StderrLineSink sink);

/** @brief Restore stderr and join the drain thread. No-op when inactive. */
void stopStderrCapture();

/** @brief Whether stderr is currently mirrored into a sink. */
[[nodiscard]] bool stderrCaptureActive();

}  // namespace eve::dev
