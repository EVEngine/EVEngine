#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "filesystem/Filesystem.h"
#include "filesystem/HotReload.h"

#include <Poco/Exception.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include "filesystem/FileData.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

eve::filesystem::Filesystem *fs() { return eve::filesystem::Filesystem::create(); }

void useIdentity(const char *id) {
    auto *f = fs();
    REQUIRE(f->setIdentity(id, true));
    REQUIRE(f->setupWriteDirectory());
}

// Minimal HTTP file server matching the `eve dev` protocol:
//   GET /manifest → JSON [{path,size,mtime}, ...]
//   GET /raw/<rel> → file bytes
class TestHttpServer {
public:
    ~TestHttpServer() { stop(); }

    bool start(std::string root) {
        try {
            server_ = std::make_unique<Poco::Net::ServerSocket>(Poco::Net::SocketAddress(0));
            port_   = static_cast<uint16_t>(server_->address().port());
        } catch (...) {
            return false;
        }
        root_    = std::move(root);
        running_.store(true);
        thread_ = std::thread([this]() { loop(); });
        return true;
    }

    void stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        running_.store(false);
        // On Linux, closing a socket does NOT wake a thread blocked in accept().
        // A self-connection is the reliable cross-platform way to unblock it.
        try {
            Poco::Net::StreamSocket wake(Poco::Net::SocketAddress("127.0.0.1", port_));
        } catch (...) {
        }
        if (thread_.joinable()) thread_.join();
    }

    uint16_t port() const { return port_; }

private:
    void loop() {
        while (running_.load()) {
            Poco::Net::StreamSocket sock;
            try {
                sock = server_->acceptConnection();
            } catch (...) {
                if (!running_.load()) break;
                continue;
            }
            // Swallow any per-connection error (e.g. receiveBytes timeout on a
            // half-open client) so a bad request cannot crash the server thread.
            try {
                handle(sock);
            } catch (...) {
            }
            try {
                sock.close();
            } catch (...) {
            }
        }
    }

    void handle(Poco::Net::StreamSocket &sock) {
        sock.setReceiveTimeout(Poco::Timespan(2, 0));
        std::string req;
        char buf[4096];
        while (req.find("\r\n\r\n") == std::string::npos) {
            int n = sock.receiveBytes(buf, sizeof(buf));
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
        }

            std::string path;
            if (!req.empty()) {
                const size_t sp1 = req.find(' ');
                const size_t sp2 = req.find(' ', sp1 + 1);
                if (sp1 != std::string::npos && sp2 != std::string::npos)
                    path = req.substr(sp1 + 1, sp2 - sp1 - 1);
            }

            std::string body;
            int status = 404;
            if (path == "/manifest") {
                body   = manifest();
                status = 200;
            } else if (path.rfind("/raw/", 0) == 0) {
                const std::string real = root_ + "/" + path.substr(5);
                std::error_code ec;
                if (std::filesystem::is_regular_file(real, ec)) {
                    std::ifstream ifs(real, std::ios::binary);
                    std::ostringstream oss;
                    oss << ifs.rdbuf();
                    body   = oss.str();
                    status = 200;
                }
            }

            std::ostringstream head;
            head << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Not Found") << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n";
            const std::string out = head.str() + body;
            try {
                sock.sendBytes(out.data(), static_cast<int>(out.size()));
            } catch (...) {
            }
            try {
                sock.close();
            } catch (...) {
            }
    }

    std::string manifest() {
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
             it != std::filesystem::recursive_directory_iterator() && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                const std::string rel =
                    std::filesystem::relative(it->path(), root_).generic_string();
                if (rel == ".eve-manifest.json") continue;
                const auto mt = std::filesystem::last_write_time(it->path(), ec);
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!first) oss << ",";
                first = false;
                oss << "{\"path\":\"" << rel << "\",\"size\":"
                    << static_cast<long long>(it->file_size(ec)) << ",\"mtime\":"
                    << static_cast<long long>(
                           std::chrono::duration_cast<std::chrono::seconds>(
                               mt.time_since_epoch())
                               .count())
                    << "}";
            }
        }
        oss << "]";
        return oss.str();
    }

    std::string root_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    uint16_t port_ = 0;
};

std::filesystem::path tempDir(const char *tag) {
    const auto p = std::filesystem::temp_directory_path() / tag;
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p, ec);
    return p;
}

void writeFile(const std::filesystem::path &p, const std::string &content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << content;
}

bool waitFor(const std::function<bool()> &cond, int ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

}  // namespace

TEST_CASE("hotreload.mountRealDirectoryShadow") {
    useIdentity("ev_ut_hot_overlay");
    auto *f = fs();

    const char *name = "overlay_test.nut";
    const char *bundle = "bundle-version";
    f->write(name, bundle, std::strlen(bundle));

    const auto overlay = tempDir("eve_ut_hot_overlay");
    writeFile(overlay / name, "overlay-version");

    REQUIRE(f->mountRealDirectory(overlay.string(), "/", false));
    eve::filesystem::FileData *fd = f->read(name);
    REQUIRE(fd != nullptr);
    const std::string shadowed(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(shadowed == "overlay-version");
    delete fd;

    REQUIRE(f->unmountRealDirectory(overlay.string()));
    fd = f->read(name);
    REQUIRE(fd != nullptr);
    const std::string back(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(back == "bundle-version");
    delete fd;

    std::error_code ec;
    std::filesystem::remove_all(overlay, ec);
}

TEST_CASE("hotreload.remoteSyncDownload") {
    useIdentity("ev_ut_hot_remote");
    auto *f = fs();

    const auto root = tempDir("eve_ut_hot_remote_root");
    writeFile(root / "main.nut", "print('main v1')\n");
    writeFile(root / "scripts" / "a.nut", "a <- 1\n");
    writeFile(root / "fx.json", R"({"emissionRate":5,"particleLifetime":[1,1]})");

    TestHttpServer server;
    REQUIRE(server.start(root.string()));

    const auto overlay = tempDir("eve_ut_hot_remote_hot");

    auto *hot = eve::filesystem::HotReload::create();
    hot->setRemoteHotDir(overlay.string());
    REQUIRE(hot->startRemoteSync("http://127.0.0.1:" + std::to_string(server.port()), 60));

    REQUIRE(waitFor([&]() { return hot->remoteSyncStatus() == "synced"; }));

    // All three files should be queued as changes.
    std::vector<std::string> changes;
    while (true) {
        const std::string p = hot->pollRemoteChange();
        if (p.empty()) break;
        changes.push_back(p);
    }
    std::sort(changes.begin(), changes.end());
    REQUIRE(changes.size() == 3);
    CHECK(changes[0] == "fx.json");
    CHECK(changes[1] == "main.nut");
    CHECK(changes[2] == "scripts/a.nut");

    // Files landed in the overlay dir.
    REQUIRE(std::filesystem::is_regular_file(overlay / "main.nut"));
    REQUIRE(std::filesystem::is_regular_file(overlay / "scripts" / "a.nut"));
    REQUIRE(std::filesystem::is_regular_file(overlay / "fx.json"));

    // Overlay is mounted at "/" so the virtual FS resolves the synced copy.
    eve::filesystem::FileData *fd = f->read("main.nut");
    REQUIRE(fd != nullptr);
    const std::string synced(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(synced.find("v1") != std::string::npos);
    delete fd;

    // Touch main.nut on the server → a new change event after the next poll.
    // Use a different length so the manifest (size + second-granularity mtime)
    // reliably reports a change even when both edits land in the same second.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    writeFile(root / "main.nut", "print('main v2 -- longer edit')\n");
    REQUIRE(waitFor([&]() {
        while (true) {
            const std::string p = hot->pollRemoteChange();
            if (p.empty()) return false;
            if (p == "main.nut") return true;
        }
    }));

    fd = f->read("main.nut");
    REQUIRE(fd != nullptr);
    const std::string v2(static_cast<char *>(fd->getData()), fd->getSize());
    CHECK(v2.find("v2") != std::string::npos);
    delete fd;

    hot->stopRemoteSync();
    f->unmountRealDirectory(overlay.string());
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::remove_all(overlay, ec);
}

TEST_CASE("hotreload.remoteSyncUnreachableRecovers") {
    useIdentity("ev_ut_hot_remote_unreach");
    const auto overlay = tempDir("eve_ut_hot_remote_unreach_hot");

    auto *hot = eve::filesystem::HotReload::create();
    hot->setRemoteHotDir(overlay.string());
    // Nothing listening on this port → sync should start but stay not-synced.
    REQUIRE(hot->startRemoteSync("http://127.0.0.1:1", 50));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(hot->remoteSyncStatus() != "synced");
    hot->stopRemoteSync();

    std::error_code ec;
    std::filesystem::remove_all(overlay, ec);
}
