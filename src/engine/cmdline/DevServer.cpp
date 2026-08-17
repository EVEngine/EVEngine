#include <CLI11.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "cmdline.h"
// EVENGINE_WEBGPU comes from common/config.h (cmakedefine), so it must be
// included before any Poco headers the #ifndef guard depends on.
#include "common/config.h"

#ifndef EVENGINE_WEBGPU
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/NetworkInterface.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>
#include <Poco/URI.h>
#endif

using namespace std::filesystem;
using namespace std;

namespace eve::cmd {

#ifndef EVENGINE_WEBGPU

namespace {

/**
 * Minimal static-file server for `eve dev` remote hot reload.
 * Serves the game directory over HTTP so a device (iOS/Android) or another
 * desktop host can poll a manifest and pull changed files:
 *   GET /manifest          → JSON [{path,size,mtime}, ...] (recursive, sorted)
 *   GET /raw/<relpath>     → file bytes (404 when missing)
 *   GET /ping              → "ok"
 */
class DevFileServer {
public:
    ~DevFileServer() { stop(); }

    bool start(uint16_t port, std::string root) {
        stop();
        root_ = std::move(root);
        if (root_.empty()) root_ = ".";
        // Bind all interfaces so mobile devices on the same LAN can connect.
        try {
            Poco::Net::SocketAddress addr(port);
            server_ = std::make_unique<Poco::Net::ServerSocket>(addr);
        } catch (...) {
            return false;
        }
        boundPort_ = static_cast<uint16_t>(server_->address().port());
        running_.store(true);
        thread_ = std::thread([this]() { serveLoop(); });
        return true;
    }

    void stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        running_.store(false);
        if (server_) {
            try {
                server_->close();  // unblocks acceptConnection
            } catch (...) {
            }
        }
        if (thread_.joinable()) thread_.join();
    }

    uint16_t port() const { return boundPort_; }

private:
    void serveLoop() {
        while (running_.load()) {
            Poco::Net::StreamSocket sock;
            try {
                sock = server_->acceptConnection();
            } catch (...) {
                continue;  // closed socket during stop()
            }
            sock.setReceiveTimeout(Poco::Timespan(3, 0));
            sock.setSendTimeout(Poco::Timespan(3, 0));
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

    // Read the request line + headers (up to "\r\n\r\n") from a single client.
    std::string readRequest(Poco::Net::StreamSocket& sock) {
        std::string buf;
        char chunk[4096];
        while (buf.find("\r\n\r\n") == std::string::npos && buf.size() < 1u << 16) {
            int n = sock.receiveBytes(chunk, sizeof(chunk));
            if (n <= 0) return {};
            buf.append(chunk, static_cast<size_t>(n));
        }
        return buf;
    }

    void sendResponse(Poco::Net::StreamSocket& sock, int status, const string& reason,
                      const string& contentType, const string& body) {
        std::ostringstream head;
        head << "HTTP/1.1 " << status << " " << reason << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Cache-Control: no-store\r\n"
             << "\r\n";
        const string header = head.str();
        sock.sendBytes(header.data(), static_cast<int>(header.size()));
        if (!body.empty()) sock.sendBytes(body.data(), static_cast<int>(body.size()));
    }

    static string urlDecode(const string& s) {
        try {
            string out;
            Poco::URI::decode(s, out);
            return out;
        } catch (...) {
            return s;
        }
    }

    bool safeRelPath(const string& rel, string& out) const {
        if (rel.empty()) return false;
        // Reject traversal / absolute / backslash forms.
        if (rel[0] == '/' || rel.find("..") != string::npos || rel.find('\\') != string::npos) return false;
        path base = path(root_) / path(rel).lexically_normal();
        if (base.empty()) return false;
        // Confirm the resolved path stays inside the root.
        const path rootAbs = absolute(path(root_)).lexically_normal();
        const path target = absolute(base).lexically_normal();
        auto rp = rootAbs.begin();
        auto tp = target.begin();
        for (; rp != rootAbs.end() && tp != target.end(); ++rp, ++tp) {
            if (*rp != *tp) return false;
        }
        if (rp != rootAbs.end()) return false;
        out = target.string();
        return true;
    }

    void handle(Poco::Net::StreamSocket& sock) {
        const string req = readRequest(sock);
        if (req.empty()) return;

        // Request line: METHOD SP PATH SP HTTP/x.y
        const size_t sp1 = req.find(' ');
        if (sp1 == string::npos) return;
        const size_t sp2 = req.find(' ', sp1 + 1);
        if (sp2 == string::npos) return;
        const string method = req.substr(0, sp1);
        string path = req.substr(sp1 + 1, sp2 - sp1 - 1);
        if (method != "GET") {
            sendResponse(sock, 405, "Method Not Allowed", "text/plain", "only GET supported\n");
            return;
        }

        if (path == "/ping") {
            sendResponse(sock, 200, "OK", "text/plain", "ok\n");
            return;
        }
        if (path == "/" || path == "/manifest") {
            sendResponse(sock, 200, "OK", "application/json", manifestJson());
            return;
        }
        if (path.rfind("/raw/", 0) == 0) {
            const string rel = urlDecode(path.substr(5));
            string real;
            if (!safeRelPath(rel, real)) {
                sendResponse(sock, 400, "Bad Request", "text/plain", "bad path\n");
                return;
            }
            std::error_code ec;
            if (!is_regular_file(real, ec)) {
                sendResponse(sock, 404, "Not Found", "text/plain", "not found\n");
                return;
            }
            std::ifstream ifs(real, std::ios::binary);
            if (!ifs) {
                sendResponse(sock, 404, "Not Found", "text/plain", "not found\n");
                return;
            }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            sendResponse(sock, 200, "OK", "application/octet-stream", oss.str());
            return;
        }
        sendResponse(sock, 404, "Not Found", "text/plain", "unknown endpoint\n");
    }

    string manifestJson() {
        Poco::JSON::Array::Ptr arr = new Poco::JSON::Array();
        std::error_code ec;
        if (is_directory(path(root_), ec)) {
            std::vector<path> stack{path(root_)};
            while (!stack.empty()) {
                const path dir = stack.back();
                stack.pop_back();
                std::vector<path> children;
                std::copy(directory_iterator(dir, ec), directory_iterator(), back_inserter(children));
                if (ec) {
                    ec.clear();
                    continue;
                }
                for (const auto& p : children) {
                    const file_status st = symlink_status(p, ec);
                    if (ec) {
                        ec.clear();
                        continue;
                    }
                    if (is_directory(st)) {
                        stack.push_back(p);
                    } else if (is_regular_file(st)) {
                        const std::string rel = relative(p, path(root_)).generic_string();
                        if (rel == ".eve-manifest.json") continue;
                        std::error_code fec;
                        const auto sz = file_size(p, fec);
                        if (fec) continue;
                        std::error_code mec;
                        const auto mt = last_write_time(p, mec);
                        if (mec) continue;
                        Poco::JSON::Object::Ptr o = new Poco::JSON::Object();
                        o->set("path", rel);
                        o->set("size", static_cast<Poco::Int64>(sz));
                        o->set("mtime", static_cast<Poco::Int64>(
                                           std::chrono::duration_cast<std::chrono::seconds>(
                                               mt.time_since_epoch())
                                               .count()));
                        arr->add(o);
                    }
                }
            }
        }
        std::ostringstream oss;
        Poco::JSON::Stringifier::stringify(arr, oss, 0, 0);
        return oss.str();
    }

    std::string root_;
    std::unique_ptr<Poco::Net::ServerSocket> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    uint16_t boundPort_ = 0;
};

std::vector<string> lanIPv4s() {
    std::vector<string> out;
    try {
        const auto list = Poco::Net::NetworkInterface::list();
        for (const auto& ni : list) {
            if (!ni.isUp() || ni.isLoopback()) continue;
            const auto addrs = ni.addressList();
            for (const auto& a : addrs) {
                const auto& addr = a.get<0>();
                if (addr.family() == Poco::Net::IPAddress::IPv4 && !addr.isLoopback())
                    out.push_back(addr.toString());
            }
        }
    } catch (...) {
    }
    return out;
}

}  // namespace

struct DevServerArgs : Handler {
    int port = 8765;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("dev", "Start a development server for the current game");
        create->allow_extras();
        create->add_option("--port", port, "HTTP port to listen on (default 8765)");
        create->formatter(formatter);
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto create = app.get_subcommand("dev");
        if (create->parsed()) {
            string path = cmd.get_remaining(create, ".");
            int    res  = cmd.DevServer(path, port);
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(DevServerArgs);


int Cmdline::DevServer(std::string path, int port) {

    if (!path.empty() && path != ".") {
        std::error_code ec;
        if (!is_directory(path, ec)) {
            cerr << "eve dev: not a directory: " << path << endl;
            return 2;
        }
    }

    DevFileServer server;
    if (!server.start(port, path.empty() ? std::string(".") : path)) {
        cerr << "eve dev: cannot bind port " << port << endl;
        return 2;
    }

    cout << "eve dev: serving '" << (path.empty() ? std::string(".") : path) << "' on port "
         << server.port() << endl;
    const auto ips = lanIPv4s();
    for (const auto& ip : ips)
        cout << "  device config.devServer = \"http://" << ip << ":" << server.port() << "\"" << endl;
    cout << "  Ctrl+C to stop" << endl;

    // Block until interrupted; the serving happens on a background thread.
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [] { return false; });
    return 0;
}

#else  // EVENGINE_WEBGPU

struct DevServerArgs : Handler {
    int port = 8765;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto create = app.add_subcommand("dev", "Start a development server for the current game");
        create->allow_extras();
        create->add_option("--port", port, "HTTP port to listen on (default 8765)");
        create->formatter(formatter);
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto create = app.get_subcommand("dev");
        if (create->parsed()) {
            string path = cmd.get_remaining(create, ".");
            int    res  = cmd.DevServer(path, port);
            return res;
        }
        return -1;  // not handle
    }
};

CMD_REG(DevServerArgs);

int Cmdline::DevServer(std::string, int) {
    cerr << "eve dev: not supported on this platform" << endl;
    return 2;
}

#endif  // EVENGINE_WEBGPU

}  // namespace eve::cmd