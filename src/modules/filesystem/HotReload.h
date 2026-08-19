#pragma once

#include "common/Module.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace eve::filesystem {

/**
 * @brief Path→reload dispatcher for soft hot reload.
 * Driven from load.nut via pollWatch → tryReload; also used by watchTree.
 *
 * What a given file means is not known here. Modules that own an asset kind
 * register an eve::caps::IAssetReloader (see common/AssetReloader.h) and this
 * only routes paths to them, so reloadable kinds grow with the linked module
 * set rather than with this file.
 *
 * On top of the local watchers, HotReload can act as a remote hot-reload client:
 * a background thread polls a dev server (`eve dev`) manifest, downloads changed
 * files into a writable overlay directory mounted at "/" (see
 * Filesystem::mountRealDirectory), and queues the changed virtual paths for the
 * script layer to `dofile` / reload. This is what makes hot reload work on
 * read-only mobile bundles (iOS app bundle / Android APK).
 */
class HotReload : public Module {
public:
    Module_REG(HotReload);

    HotReload() = default;
    ~HotReload() override;

    /**
     * @brief Pin a path to one reloader kind ("particle" / "tilemap" / "texture" /
     * whatever a linked module registers), bypassing extension matching.
     * "auto" (the default) lets every reloader claim the path itself.
     */
    void bind(std::string path, std::string kind = "auto");
    void unbind(std::string path);

    /** @brief Offer a (normalized) path to the registered reloaders; true if any reloaded. */
    bool tryReload(std::string path);

    /** @brief Recursively watch root and all subdirectories. Returns number of watches added. */
    int watchTree(std::string root = ".");

    /**
     * Start remote hot reload against a dev server. Background thread polls
     * <url>/manifest and downloads changed files via <url>/raw/<path> into a
     * writable overlay directory (mounted in front of the bundled game source),
     * then queues the virtual paths. Returns false if already running.
     * @param url Base URL, e.g. "http://192.168.1.5:8765".
     * @param pollMs Poll interval in milliseconds (default 1000).
     */
    bool startRemoteSync(std::string url, int pollMs = 1000);

    /** Stop the remote sync thread (joining it). Safe to call twice. */
    void stopRemoteSync();

    /**
     * Override the writable overlay directory used to stage remote-sync
     * downloads (default: platform appdata/EVE/hotreload, or the internal
     * storage hotreload dir on Android). Must be called before startRemoteSync.
     */
    void setRemoteHotDir(std::string dir);

    /** Whether the remote sync thread is currently running. */
    bool isRemoteSyncing() const;

    /** Last sync status: "idle" | "syncing" | "synced" | "error:<reason>". */
    std::string remoteSyncStatus() const;

    /**
     * Pop the next changed virtual path reported by remote sync.
     * Returns empty string when the queue is drained.
     */
    std::string pollRemoteChange();

    static std::string normalizePath(std::string path);

private:
    struct RemoteFile {
        std::string path;
        int64_t     size  = -1;
        int64_t     mtime = -1;
    };

    // --- Remote sync internals ---

    /** Resolve + create the writable overlay directory for downloaded files. */
    std::string ensureHotDir();

    /** Mount the overlay directory in front of "/" so synced files shadow the bundle. */
    bool mountHotDir();

    /** Fetch + parse the server manifest. Returns false on any failure. */
    bool fetchManifest(std::vector<RemoteFile> &out);

    /** Download one file from the server into the overlay dir. */
    bool downloadFile(const std::string &relPath);

    /** Compare manifest against the local record; download/add/remove as needed. */
    void applyManifest(const std::vector<RemoteFile> &manifest);

    /** Load/persist the local sync record (.eve-manifest.json) under the hot dir. */
    std::map<std::string, std::pair<int64_t, int64_t>> loadRecord() const;
    void saveRecord(const std::map<std::string, std::pair<int64_t, int64_t>> &record) const;

    void syncLoop(int pollMs);

    void queueChange(std::string path);

    std::unordered_map<std::string, std::string> bindings_;  // norm path → kind

    // Remote sync state (guarded by syncMu_).
    mutable std::mutex              syncMu_;
    std::thread             syncThread_;
    std::condition_variable syncCv_;
    std::atomic<bool>       syncRunning_{false};
    bool                    syncStarted_ = false;
    bool                    hotDirMounted_ = false;
    std::string             syncUrl_;
    mutable std::string     syncStatus_ = "idle";
    mutable std::deque<std::string> changedQueue_;
    std::string             hotDir_;
};

}  // namespace eve::filesystem
