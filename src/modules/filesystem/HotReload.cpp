#include "filesystem/HotReload.h"

#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#ifndef EVENGINE_WEBGPU
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleEmitter.h"
#endif

#ifndef EVENGINE_WEBGPU
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <Poco/Timespan.h>
#include <Poco/URI.h>
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#if defined(EVENGINE_ANDROID)
#include "android/android.h"
#include <SDL2/SDL.h>
#elif defined(EVENGINE_IOS)
#include "ios/ios.h"
#endif

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace eve::filesystem {
namespace {

std::string toLower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extensionOf(const std::string &path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return {};
    return toLower(path.substr(pos));
}

std::string joinDir(const std::string &dir, const std::string &name) {
    if (dir.empty() || dir == ".") return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

// --- Remote sync helpers ---

#ifndef EVENGINE_WEBGPU
std::string stripTrailingSlash(std::string s) {
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// Path-escape for use inside a URL path segment (spaces, '#', '?', etc.).
std::string urlPathEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    const char hex[] = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// HTTP GET; returns 200 response body or empty on failure.
std::string httpGet(const std::string &url, int timeoutMs) {
    try {
        Poco::URI uri(url);
        Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
        session.setTimeout(Poco::Timespan(timeoutMs / 1000, (timeoutMs % 1000) * 1000));
        std::string path = uri.getPathAndQuery();
        if (path.empty()) path = "/";
        Poco::Net::HTTPRequest req(Poco::Net::HTTPRequest::HTTP_GET, path,
                                   Poco::Net::HTTPMessage::HTTP_1_1);
        req.setHost(uri.getHost());
        std::ostream &os = session.sendRequest(req);
        (void)os;
        Poco::Net::HTTPResponse resp;
        std::istream &is = session.receiveResponse(resp);
        if (resp.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) return {};
        std::ostringstream oss;
        Poco::StreamCopier::copyStream(is, oss);
        return oss.str();
    } catch (...) {
        return {};
    }
}

bool writeFileBytes(const std::string &realPath, const std::string &data) {
    std::error_code ec;
    std::filesystem::path p(realPath);
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(realPath, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}
#endif  // !EVENGINE_WEBGPU

}  // namespace

Module_IMPL(HotReload, new HotReload());

HotReload::~HotReload() { stopRemoteSync(); }

std::string HotReload::normalizePath(std::string path) {
    for (char &c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

void HotReload::bind(std::string path, std::string kind) {
    path = normalizePath(std::move(path));
    if (path.empty()) return;
    if (kind.empty()) kind = "auto";
    bindings_[path] = kind;
}

void HotReload::unbind(std::string path) {
    bindings_.erase(normalizePath(std::move(path)));
}

bool HotReload::isImagePath(const std::string &normPath) const {
    const std::string ext = extensionOf(normPath);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
           ext == ".gif" || ext == ".webp" || ext == ".exr" || ext == ".hdr";
}

bool HotReload::isJsonPath(const std::string &normPath) const {
    return extensionOf(normPath) == ".json";
}

#ifndef EVENGINE_WEBGPU
bool HotReload::reloadParticles(const std::string &normPath) {
    if (ecs::current()->getManager<particles::ParticleEmitter>() == nullptr) return false;

    int reloaded = 0;
    auto view = ecs::View<particles::ParticleEmitter, particles::ParticleEmitter::Config,
                          particles::ParticleEmitter::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!cfg->entity || res->path.empty()) continue;
        if (normalizePath(res->path) != normPath) continue;
        if (particles::reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded > 0;
}

bool HotReload::reloadTilemaps(const std::string &normPath) {
    if (ecs::current()->getManager<map::TileLayer>() == nullptr) return false;

    int reloaded = 0;
    auto view = ecs::View<map::TileLayer, map::TileLayer::Config, map::TileLayer::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!cfg->entity || res->path.empty()) continue;
        if (normalizePath(res->path) != normPath) continue;
        if (map::reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded > 0;
}
#else
bool HotReload::reloadParticles(const std::string &) { return false; }
bool HotReload::reloadTilemaps(const std::string &) { return false; }
#endif

bool HotReload::reloadTextures(const std::string &normPath) {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) return false;

    bool ok = gfx->reloadTextureFromFile(normPath);

#ifndef EVENGINE_WEBGPU
    // Emitters that reference this texture path: force re-bind via config reload or setTexture.
    if (ecs::current()->getManager<particles::ParticleEmitter>() != nullptr) {
        auto view = ecs::View<particles::ParticleEmitter, particles::ParticleEmitter::Config,
                              particles::ParticleEmitter::Resource>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg, res] = *it;
            if (!cfg->entity || res->texturePath.empty()) continue;
            if (normalizePath(res->texturePath) != normPath) continue;
            try {
                graphics::Texture *tex = gfx->newTextureFromFile(normPath);
                cfg->entity->setTexture(tex);
                ok = true;
            } catch (...) {
            }
        }
    }

    // Tile layers that reference this atlas path.
    if (ecs::current()->getManager<map::TileLayer>() != nullptr) {
        auto view = ecs::View<map::TileLayer, map::TileLayer::Config, map::TileLayer::Resource,
                              map::TileLayer::Tileset>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg, res, ts] = *it;
            if (!cfg->entity || res->texturePath.empty()) continue;
            if (normalizePath(res->texturePath) != normPath) continue;
            try {
                graphics::Texture *tex = gfx->newTextureFromFile(normPath);
                cfg->entity->setTileset(tex, ts->firstGid, ts->columns, ts->margin, ts->spacing);
                ok = true;
            } catch (...) {
            }
        }
    }
#endif
    return ok;
}

bool HotReload::tryReload(std::string path) {
    const std::string norm = normalizePath(std::move(path));
    if (norm.empty()) return false;

    std::string kind = "auto";
    auto bit = bindings_.find(norm);
    if (bit != bindings_.end()) kind = bit->second;

    bool any = false;
    const bool wantParticle = (kind == "auto" || kind == "particle") && isJsonPath(norm);
    const bool wantTilemap = (kind == "auto" || kind == "tilemap") && isJsonPath(norm);
    const bool wantTexture = (kind == "auto" || kind == "texture") && isImagePath(norm);

    if (wantParticle) any = reloadParticles(norm) || any;
    if (wantTilemap) any = reloadTilemaps(norm) || any;
    if (wantTexture) any = reloadTextures(norm) || any;

    // Bound but unknown extension: still try when kind is explicit.
    if (kind == "particle" && !wantParticle) any = reloadParticles(norm) || any;
    if (kind == "tilemap" && !wantTilemap) any = reloadTilemaps(norm) || any;
    if (kind == "texture" && !wantTexture) any = reloadTextures(norm) || any;

    return any;
}

int HotReload::watchTree(std::string root) {
    auto *fs = Filesystem::create();
    if (!fs) return 0;
    root = normalizePath(std::move(root));
    if (root.empty()) root = ".";

    int added = 0;
    std::vector<std::string> stack;
    stack.push_back(root == "." ? std::string(".") : root);

    while (!stack.empty()) {
        std::string dir = stack.back();
        stack.pop_back();
        if (fs->watch(dir)) ++added;

        std::vector<std::string> items;
        try {
            if (dir == "." || dir.empty())
                items = fs->getDirectoryItems("");
            else
                items = fs->getDirectoryItems(dir);
        } catch (...) {
            continue;
        }

        for (const auto &name : items) {
            if (name.empty() || name == "." || name == "..") continue;
            const std::string child = (dir == "." || dir.empty()) ? name : joinDir(dir, name);
            Filesystem::Info info{};
            if (!fs->getInfo(child, info)) continue;
            if (info.type == "directory") stack.push_back(child);
        }
    }
    return added;
}

// --- Remote hot reload (dev-server sync) ---
// Poco (HTTP) is not available in the Emscripten/WebGPU build; the API is
// stubbed there so scripts still compile, and startRemoteSync just fails.

#ifndef EVENGINE_WEBGPU

std::string HotReload::ensureHotDir() {
    if (!hotDir_.empty()) return hotDir_;

    std::string base;
#if defined(EVENGINE_ANDROID)
    base = eve::android::getHotReloadDirectory();
#elif defined(EVENGINE_IOS)
    base = eve::ios::getHotReloadDirectory();
#else
    {
        auto *fs = Filesystem::create();
        base = fs ? fs->getAppdataDirectory() : std::string(".");
        if (!base.empty() && base.back() == '/') base.pop_back();
        base += "/EVE/hotreload";
    }
#endif
    if (base.empty()) base = ".";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    hotDir_ = base;
    return hotDir_;
}

void HotReload::setRemoteHotDir(std::string dir) {
    if (syncStarted_) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    hotDir_ = std::move(dir);
}

bool HotReload::mountHotDir() {
    if (hotDirMounted_) return true;
    auto *fs = Filesystem::create();
    if (!fs) return false;
    const std::string dir = ensureHotDir();
    if (!fs->mountRealDirectory(dir, "/", /*appendToPath=*/false)) return false;
    hotDirMounted_ = true;
    return true;
}

bool HotReload::fetchManifest(std::vector<RemoteFile> &out) {
    const std::string url = stripTrailingSlash(syncUrl_) + "/manifest";
    const std::string body = httpGet(url, 2000);
    if (body.empty()) return false;

    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(body);
        Poco::JSON::Array::Ptr arr = result.extract<Poco::JSON::Array::Ptr>();
        if (!arr) return false;
        for (size_t i = 0; i < arr->size(); ++i) {
            Poco::JSON::Object::Ptr o = arr->getObject(i);
            if (!o) continue;
            RemoteFile f;
            f.path = o->optValue<std::string>("path", "");
            if (f.path.empty()) continue;
            f.size = o->optValue<int64_t>("size", -1);
            f.mtime = o->optValue<int64_t>("mtime", -1);
            f.path = normalizePath(std::move(f.path));
            if (!f.path.empty()) out.push_back(std::move(f));
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool HotReload::downloadFile(const std::string &relPath) {
    const std::string url = stripTrailingSlash(syncUrl_) + "/raw/" + urlPathEscape(relPath);
    const std::string body = httpGet(url, 5000);
    if (body.empty()) return false;
    const std::string real = joinDir(ensureHotDir(), relPath);
    return writeFileBytes(real, body);
}

std::map<std::string, std::pair<int64_t, int64_t>> HotReload::loadRecord() const {
    std::map<std::string, std::pair<int64_t, int64_t>> record;
    const std::string real = joinDir(hotDir_, ".eve-manifest.json");
    std::ifstream ifs(real, std::ios::binary);
    if (!ifs) return record;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(oss.str());
        Poco::JSON::Array::Ptr arr = result.extract<Poco::JSON::Array::Ptr>();
        if (!arr) return record;
        for (size_t i = 0; i < arr->size(); ++i) {
            Poco::JSON::Object::Ptr o = arr->getObject(i);
            if (!o) continue;
            const std::string path = o->optValue<std::string>("path", "");
            if (path.empty()) continue;
            record[path] = {o->optValue<int64_t>("size", -1), o->optValue<int64_t>("mtime", -1)};
        }
    } catch (...) {
    }
    return record;
}

void HotReload::saveRecord(const std::map<std::string, std::pair<int64_t, int64_t>> &record) const {
    Poco::JSON::Array::Ptr arr = new Poco::JSON::Array();
    for (const auto &kv : record) {
        Poco::JSON::Object::Ptr o = new Poco::JSON::Object();
        o->set("path", kv.first);
        o->set("size", kv.second.first);
        o->set("mtime", kv.second.second);
        arr->add(o);
    }
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(arr, oss, 0, 0);
    const std::string real = joinDir(hotDir_, ".eve-manifest.json");
    writeFileBytes(real, oss.str());
}

void HotReload::queueChange(std::string path) {
    std::lock_guard<std::mutex> lock(syncMu_);
    changedQueue_.push_back(std::move(path));
}

void HotReload::applyManifest(const std::vector<RemoteFile> &manifest) {
    auto record = loadRecord();
    std::map<std::string, bool> seen;
    for (const auto &file : manifest) {
        seen[file.path] = true;
        auto it = record.find(file.path);
        if (it != record.end() && it->second.first == file.size && it->second.second == file.mtime)
            continue;  // unchanged
        if (downloadFile(file.path)) {
            record[file.path] = {file.size, file.mtime};
            queueChange(file.path);
        }
    }
    // Remove files that disappeared from the server.
    for (auto it = record.begin(); it != record.end();) {
        if (seen.find(it->first) != seen.end()) {
            ++it;
            continue;
        }
        const std::string real = joinDir(hotDir_, it->first);
        std::error_code ec;
        std::filesystem::remove(real, ec);
        queueChange(it->first);
        it = record.erase(it);
    }
    saveRecord(record);
}

void HotReload::syncLoop(int pollMs) {
    int failStreak = 0;
    while (syncRunning_.load()) {
        std::vector<RemoteFile> manifest;
        if (fetchManifest(manifest)) {
            failStreak = 0;
            {
                std::lock_guard<std::mutex> l(syncMu_);
                syncStatus_ = "syncing";
            }
            applyManifest(manifest);
            {
                std::lock_guard<std::mutex> l(syncMu_);
                syncStatus_ = "synced";
            }
        } else {
            ++failStreak;
            {
                std::lock_guard<std::mutex> l(syncMu_);
                syncStatus_ = failStreak > 3 ? "error:unreachable" : "idle";
            }
        }

        std::unique_lock<std::mutex> l(syncMu_);
        syncCv_.wait_for(l, std::chrono::milliseconds(pollMs), [this]() { return !syncRunning_.load(); });
    }
    {
        std::lock_guard<std::mutex> l(syncMu_);
        syncStatus_ = "idle";
    }
}

bool HotReload::startRemoteSync(std::string url, int pollMs) {
    std::lock_guard<std::mutex> lock(syncMu_);
    if (syncStarted_) return false;
    url = stripTrailingSlash(std::move(url));
    if (url.empty()) return false;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) url = "http://" + url;

    syncUrl_ = std::move(url);
    syncStatus_ = "syncing";
    syncStarted_ = true;
    hotDirMounted_ = false;
    // Mount the overlay dir from the calling (main) thread; PhysFS mount is not
    // guaranteed thread-safe, so never touch it from the sync thread.
    if (!mountHotDir()) {
        syncStarted_ = false;
        syncStatus_ = "error:mount";
        return false;
    }
    syncRunning_.store(true);
    syncThread_ = std::thread([this, pollMs]() { syncLoop(pollMs); });
    return true;
}

void HotReload::stopRemoteSync() {
    {
        std::lock_guard<std::mutex> lock(syncMu_);
        if (!syncStarted_) return;
        syncRunning_.store(false);
    }
    syncCv_.notify_all();
    if (syncThread_.joinable()) syncThread_.join();
    std::lock_guard<std::mutex> lock(syncMu_);
    syncStarted_ = false;
    changedQueue_.clear();
}

bool HotReload::isRemoteSyncing() const { return syncRunning_.load(); }

std::string HotReload::remoteSyncStatus() const {
    std::lock_guard<std::mutex> lock(syncMu_);
    return syncStatus_;
}

std::string HotReload::pollRemoteChange() {
    std::lock_guard<std::mutex> lock(syncMu_);
    if (changedQueue_.empty()) return {};
    std::string p = std::move(changedQueue_.front());
    changedQueue_.pop_front();
    return p;
}

#endif  // !EVENGINE_WEBGPU

#ifdef EVENGINE_WEBGPU
// WebGPU (browser) build: no Poco HTTP client / threads for remote sync.
std::string HotReload::ensureHotDir() { return {}; }
bool HotReload::mountHotDir() { return false; }
bool HotReload::fetchManifest(std::vector<RemoteFile> &) { return false; }
bool HotReload::downloadFile(const std::string &) { return false; }
void HotReload::applyManifest(const std::vector<RemoteFile> &) {}
std::map<std::string, std::pair<int64_t, int64_t>> HotReload::loadRecord() const { return {}; }
void HotReload::saveRecord(const std::map<std::string, std::pair<int64_t, int64_t>> &) const {}
void HotReload::syncLoop(int) {}
void HotReload::queueChange(std::string) {}
bool HotReload::startRemoteSync(std::string, int) { return false; }
void HotReload::stopRemoteSync() {}
void HotReload::setRemoteHotDir(std::string) {}
bool HotReload::isRemoteSyncing() const { return false; }
std::string HotReload::remoteSyncStatus() const { return "idle"; }
std::string HotReload::pollRemoteChange() { return {}; }
#endif  // EVENGINE_WEBGPU

void HotReload::expose(ssq::Table &table) {
    auto cls = table.addClass(name, HotReload::create, false);
    expose(cls);
}

void HotReload::expose(ssq::Class &cls) {
    cls.addFunc("getName", &HotReload::getName);
    cls.addFunc("bind", &HotReload::bind);
    cls.addFunc("unbind", &HotReload::unbind);
    cls.addFunc("tryReload", &HotReload::tryReload);
    cls.addFunc("watchTree", &HotReload::watchTree);
    cls.addFunc("startRemoteSync", &HotReload::startRemoteSync);
    cls.addFunc("stopRemoteSync", &HotReload::stopRemoteSync);
    cls.addFunc("isRemoteSyncing", &HotReload::isRemoteSyncing);
    cls.addFunc("remoteSyncStatus", &HotReload::remoteSyncStatus);
    cls.addFunc("pollRemoteChange", &HotReload::pollRemoteChange);
}

}  // namespace eve::filesystem
