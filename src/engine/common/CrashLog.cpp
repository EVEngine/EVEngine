#include "common/CrashLog.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace eve {

namespace {

std::mutex g_logMutex;
std::ofstream g_logFile;
std::string g_logPath;
bool g_open = false;
bool g_explicitInit = false;

std::string nowStamp() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string resolvePath(const std::string& logDir) {
    std::string dir = logDir;
    if (dir.empty()) {
        const char* env = std::getenv("EVE_LOG_DIR");
        if (env) dir = env;
    }
    if (dir.empty()) return "eve.log";
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + "eve.log" : dir + "/eve.log";
}

void openLocked(const std::string& path, bool explicitInit) {
    g_logFile.open(path, std::ios::app);
    if (!g_logFile) return;
    g_open = true;
    g_explicitInit = explicitInit;
    g_logPath = path;
    g_logFile << "==== " << nowStamp() << " EVEngine session start ====\n";
    g_logFile.flush();
}

// Open on first use with the default/env path. Never overrides an explicit
// initSystemLogging(dir) that failed to open.
void ensureOpenLocked() {
    if (g_open || g_explicitInit) return;
    openLocked(resolvePath({}), /*explicitInit=*/false);
}

}  // namespace

void initSystemLogging(const std::string& logDir) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_open) return;
    openLocked(resolvePath(logDir), /*explicitInit=*/true);
}

void recordLogEvent(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    ensureOpenLocked();
    if (!g_open || !g_logFile) return;
    g_logFile << "[" << nowStamp() << "] " << level << " | " << message << "\n";
    g_logFile.flush();
}

void recordCrashEvent(const std::string& report) {
    std::unique_lock<std::mutex> lock(g_logMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    ensureOpenLocked();
    if (!g_open || !g_logFile) return;
    g_logFile << "[" << nowStamp() << "] crash | ";
    g_logFile.write(report.data(), static_cast<std::streamsize>(report.size()));
    if (report.empty() || report.back() != '\n') g_logFile << "\n";
    g_logFile.flush();
}

std::string crashLogPath() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_logPath;
}

}  // namespace eve
