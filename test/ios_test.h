#pragma once
// test/ios_test.h
//
// Objective-C++ helpers used by the iOS test app runner (test/main.cpp).
// Kept out of main.cpp so the runner itself stays plain C++ on every platform;
// the .mm implementation is compiled only when EVENGINE_IOS_TEST_APP=ON.

#include <string>

namespace eve {
namespace ios_test {

/**
 * @brief Forwards one line of test output to unified logging (os_log) so
 * `log stream` can show zeroerr results from a device.
 */
void logLine(const char *line);

/**
 * @brief Stages the bundled test/ + examples/ trees into a writable location.
 *
 * The .app bundle is read-only on iOS, and the suite writes output (out/)
 * plus needs a cwd it can resolve test assets from, so the runner wipes and
 * re-copies the bundled trees into the app's Caches directory on every
 * launch (mirrors EVTestActivity's unpacking on Android).
 *
 * @return the staged root directory, or an empty string on failure.
 */
std::string stagedTestRoot();

/**
 * @brief Resolves the zeroerr test filter for this launch.
 *
 * Priority: `-evengine.test.filter <pattern>` launch argument
 * (NSArgumentDomain, read through NSUserDefaults, e.g. passed by `devicectl
 * device process launch` or ios-deploy), then `--testcase=<pattern>` on the
 * process argument list. Mirrors the `evengine.test.filter` intent extra on
 * Android.
 *
 * @return the filter pattern, or an empty string to run the full suite.
 */
std::string launchFilter();

} // namespace ios_test
} // namespace eve
