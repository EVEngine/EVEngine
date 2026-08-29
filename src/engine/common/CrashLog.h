#pragma once

/**
 * @file CrashLog.h
 * @brief Persistent, thread-safe record of engine crash and problem events.
 *
 * Unlike zeroerr's structured logging (which the engine only uses for ASSERTs),
 * this facility writes a plain append-only `eve.log` file that survives process
 * death. It is intentionally self-contained (std::ofstream) so crash/error
 * events are captured regardless of where or how the failure occurs, and it
 * works on every platform (not just Windows).
 *
 * Typical wiring:
 *   - src/engine/main.cpp calls initSystemLogging() very early in main().
 *   - The Windows unhandled-exception filter (CrashHandler.h) reports hard
 *     crashes via recordCrashEvent().
 *   - The top-level run() catch blocks report exceptions via recordLogEvent().
 *
 * The log location defaults to `eve.log` in the current working directory and
 * can be overridden with the EVE_LOG_DIR environment variable or an explicit
 * initSystemLogging(dir) argument.
 */

#include "common/Export.h"

#include <string>

namespace eve {

/**
 * @brief Opens (append) the crash/error log file and records a session marker.
 * @param logDir Directory for the `eve.log` file. Empty selects EVE_LOG_DIR or
 *               the current working directory.
 *
 * Idempotent: subsequent calls are no-ops. If the file cannot be opened, logging
 * silently stays disabled (stderr output is unaffected).
 */
EVENGINE_API void initSystemLogging(const std::string& logDir = {});

/**
 * @brief Appends a timestamped event line to the crash/error log.
 * @param level   Severity tag (e.g. "info", "warn", "error").
 * @param message The event text (a trailing newline is added).
 *
 * Thread-safe and blocking so normal error reporting never drops a line. No-op
 * (besides the implicit lazy open) when logging is unavailable.
 */
EVENGINE_API void recordLogEvent(const std::string& level, const std::string& message);

/**
 * @brief Records a crash report, safe to call from a crash/exception handler.
 * @param report Multi-line crash report (e.g. exception code + backtrace).
 *
 * Uses a try-lock so it never deadlocks if the crashing thread already held the
 * log mutex; on contention the report is dropped rather than hanging. Best used
 * by the unhandled-exception filter, which also prints the same report to stderr.
 */
EVENGINE_API void recordCrashEvent(const std::string& report);

/**
 * @brief Path of the open crash/error log file.
 * @return Absolute/relative path as opened; empty when logging is not available.
 */
EVENGINE_API std::string crashLogPath();

}  // namespace eve
