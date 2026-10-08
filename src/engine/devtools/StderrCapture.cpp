#include "devtools/StderrCapture.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace eve::dev {
namespace {

// 64 KiB keeps a chatty validation run from blocking the writing thread while
// the drain thread is between reads.
constexpr int kPipeBytes = 64 * 1024;

std::mutex        g_mutex;
std::thread       g_thread;
StderrLineSink    g_sink;
std::atomic<bool> g_active{false};
int               g_savedFd          = -1;
int               g_readFd           = -1;
bool              g_atexitRegistered = false;

int stderrFd() {
#if defined(_WIN32)
    return _fileno(stderr);
#else
    return fileno(stderr);
#endif
}

void closeFd(int fd) {
#if defined(_WIN32)
    _close(fd);
#else
    close(fd);
#endif
}

int duplicateFd(int fd) {
#if defined(_WIN32)
    return _dup(fd);
#else
    return dup(fd);
#endif
}

int replaceFd(int source, int target) {
#if defined(_WIN32)
    return _dup2(source, target);
#else
    return dup2(source, target);
#endif
}

int writeFd(int fd, const char* data, int size) {
#if defined(_WIN32)
    return _write(fd, data, static_cast<unsigned>(size));
#else
    return static_cast<int>(write(fd, data, static_cast<std::size_t>(size)));
#endif
}

int readFd(int fd, char* data, int size) {
#if defined(_WIN32)
    return _read(fd, data, static_cast<unsigned>(size));
#else
    return static_cast<int>(read(fd, data, static_cast<std::size_t>(size)));
#endif
}

/** Emit every complete line in `pending`, keeping the trailing partial one. */
void flushCompleteLines(std::string& pending) {
    for (;;) {
        const auto newline = pending.find('\n');
        if (newline == std::string::npos) break;
        std::string line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (g_sink) g_sink(line);
    }
}

void drainLoop() {
    std::string pending;
    char        buffer[4096];
    for (;;) {
        const int read = readFd(g_readFd, buffer, static_cast<int>(sizeof(buffer)));
        if (read <= 0) break;
        // Preserve the stream first: diagnostics must never be swallowed just
        // because the console sink is slow or detached.
        if (g_savedFd >= 0) {
            int offset = 0;
            while (offset < read) {
                const int written = writeFd(g_savedFd, buffer + offset, read - offset);
                if (written <= 0) break;
                offset += written;
            }
        }
        pending.append(buffer, static_cast<std::size_t>(read));
        flushCompleteLines(pending);
    }
    if (!pending.empty()) {
        std::string line = std::move(pending);
        pending.clear();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (g_sink) g_sink(line);
    }
}

}  // namespace

StderrCaptureStatus startStderrCapture(StderrLineSink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_active.load()) return StderrCaptureStatus::Active;
    if (!sink) return StderrCaptureStatus::Rejected;

    if (!g_atexitRegistered) {
        // Restore the real stderr before static destructors run, so a late
        // exit-time message cannot land in a pipe whose reader is gone.
        std::atexit(&stopStderrCapture);
        g_atexitRegistered = true;
    }

    int fds[2] = {-1, -1};
#if defined(_WIN32)
    if (_pipe(fds, kPipeBytes, _O_BINARY | _O_NOINHERIT) != 0) return StderrCaptureStatus::Unavailable;
#else
    if (pipe(fds) != 0) return StderrCaptureStatus::Unavailable;
#endif

    std::fflush(stderr);
    const int target = stderrFd();
    const int saved  = duplicateFd(target);
    if (saved < 0) {
        closeFd(fds[0]);
        closeFd(fds[1]);
        return StderrCaptureStatus::Unavailable;
    }
    if (replaceFd(fds[1], target) < 0) {
        closeFd(saved);
        closeFd(fds[0]);
        closeFd(fds[1]);
        return StderrCaptureStatus::Unavailable;
    }
    // Descriptor 2 now owns the write end; keeping this extra handle would hold
    // the pipe open forever and hide EOF from the drain thread.
    closeFd(fds[1]);

    g_savedFd = saved;
    g_readFd  = fds[0];
    g_sink    = std::move(sink);
    g_active.store(true);
    g_thread = std::thread(drainLoop);
    return StderrCaptureStatus::Active;
}

void stopStderrCapture() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active.load()) return;

    // Restore descriptor 2 first: that releases the last pipe writer, so the
    // drain thread observes EOF and can be joined instead of racing a closed
    // handle. The saved descriptor stays open across the join so the remaining
    // buffered lines still reach the original stream.
    std::fflush(stderr);
    const int saved = g_savedFd;
    if (saved >= 0) replaceFd(saved, stderrFd());
    if (g_thread.joinable()) g_thread.join();
    if (saved >= 0) closeFd(saved);
    if (g_readFd >= 0) closeFd(g_readFd);

    g_savedFd = -1;
    g_readFd  = -1;
    g_sink    = StderrLineSink{};
    g_active.store(false);
}

bool stderrCaptureActive() { return g_active.load(); }

}  // namespace eve::dev
