#pragma once
#include "Object.h"
#include "common/AssetReloader.h"
#include "common/BorrowedRef.h"
#include "common/Result.h"

#include <cstddef>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve {

class Resource;

/**
 * @brief Resource is a game object that is managed by the ResourceManager.
 * It can be loaded from a file or generated at runtime.
 * If the resource is loaded from file, a monitor will be created to watch the file changes.
 * When the file is modified, the resource will be reloaded.
 * If the resource is generated at runtime, it can depend on a few other resources
 * and the ResourceManager will update the resource when the dependencies are changed.
 *
 * Resource ID format:
 * - File: file://path/to/file
 * - Generated: res://category/name
 * - Online: http://example.com/path/to/resource
 * - Save: save://path/name
 * - Config: config://name
 *
 * The ResourceManager cache keys file-backed resources by their normalized VFS
 * path, optionally with `?param=value` suffixes for parameters that change the
 * decoded asset (e.g. a font's pixel size). See ResourceManager::makeKey.
 */
class Resource : public Object {
public:
    virtual ~Resource() {}

    std::string getUri() const { return uri; }
	std::vector<eve::ref<Resource>> getDependencies() const { return dependencies; }

	virtual void addDependency(eve::ref<Resource> resource) { dependencies.push_back(resource); }

    /**
     * @brief Replace this instance's contents with `replacement`'s.
     * The ResourceManager keeps instance identity stable across reloads, so
     * existing holders (raw pointers, refs, script instances) keep a valid
     * object. Subclasses move the payload out of `replacement`, which is
     * drained and destroyed by the caller afterwards. `replacement` must be
     * the same concrete type as `this`. Implementations must use a reversible
     * swap and either complete without throwing or leave both objects unchanged;
     * ResourceManager uses the drained candidate to roll back a later failure
     * in the same dependency transaction.
     */
    virtual void adopt(eve::Resource& replacement) = 0;

protected:
    Resource(std::string uri) : uri(uri) {}

    /** @brief Set the resource URI (the cache key). Used by ResourceManager. */
    void setUri(std::string value) { uri = std::move(value); }

    friend class ResourceManager;

    std::string uri;
	std::vector<eve::ref<Resource>> dependencies;
};


/**
 * @brief ResourceManager is a singleton that manages all resources in the game.
 * It provides a way to load, reload, and unload resources.
 *
 * The manager is a unified cache for CPU-side assets (ImageData, FontData,
 * ModelData, SoundData, ...). Keys are normalized VFS paths, optionally with
 * `?param=value` entries ("fonts/a.ttf?size=16"). On a miss it asks the
 * registered eve::caps::IAssetReloader providers which one claims the key and
 * can `load()` it; the first hit is cached and returned. On a file change the
 * manager itself participates in hot reload as the first IAssetReloader
 * listener (kCache priority): every cached entry for that path is re-loaded
 * and refreshed in place through Resource::adopt, so holders keep a valid
 * pointer. Entries that list the refreshed resource as a dependency are
 * refreshed too (transitively, cycle-safe).
 *
 * The cache itself is thread-safe: map access is mutex-guarded and the heavy
 * provider load() runs outside the lock, so sceneloader's background decode
 * threads can insert entries while the main thread reads them. Reloads (adopt)
 * are expected on the main thread during hot-reload dispatch.
 *
 * `request()` / `waitFor()` are the asynchronous programming interface: request
 * queues CPU decode on `caps::IAsyncWorkExecutor` (the thread module) and
 * waitFor joins that job. `get()` waits for an in-flight request of the same
 * key so a prefetch cannot race a later synchronous load into a double decode.
 */
class ResourceManager : public eve::caps::IAssetReloader {
public:
	static ResourceManager& getInstance();

    /** @brief Normalize a VFS path: backslashes to '/', strip leading "./" and trailing '/'. */
    static std::string normalizePath(std::string path);

    /** @brief Build a cache key from a path and optional `?query` parameters. */
    static std::string makeKey(const std::string& path, const std::string& query = "");

    /** @brief The path part of a cache key (everything before the first '?'). */
    static std::string pathOfKey(const std::string& key);

    /**
     * @brief Get the cached resource for `key`; on a miss, load it through the
     * registered IAssetReloader providers and cache the result.
     * @param key A normalized VFS path, optionally with `?params`.
     * @return The shared resource, or nullptr when no provider claims the key.
     *         Loader errors propagate to the caller. The returned pointer is
     *         owned by the cache (kept alive while the entry exists); callers
     *         must not delete it, and it stays valid until unload() or process
     *         exit.
     * @ownership Borrowed from the cache; ResourceManager is the owning authority.
     * @nullable Yes when no registered provider claims `key`.
     * @lifetime Valid while the cache entry remains loaded and until manager teardown;
     *           callers must not retain it across unload or reload replacement.
     * @thread Lookup may be called by concurrent readers; cache mutation and reload
     *         commit are serialized by ResourceManager.
     * @reentrancy The returned resource must not be used to re-enter cache mutation
     *             while a ResourceManager operation is holding its internal lock.
     */
    [[nodiscard("resource lookup ownership must be retained or explicitly handled")]] Resource* get(std::string key);

    /**
     * @brief Queue a cache miss load without blocking for decode to finish.
     * @param key Normalized VFS path, optionally with `?params`.
     * @return `NoOp` if already cached, `Pending` if a load is already in flight,
     *         `Applied` if this call queued or completed a load, `NotFound` if no
     *         provider claims the key, or `Failed` when inline load throws.
     * @ownership Does not transfer a resource; the cache remains the owner.
     * @lifetime The pending job keeps the key alive until waitFor/get/clear.
     * @thread Safe to call from the game thread. Decode runs on a worker when
     *         `IAsyncWorkExecutor` is registered, otherwise inline.
     * @reentrancy Must not be called from `IAssetReloader::load()`.
     */
    [[nodiscard("async resource request outcome must be checked")]] eve::Result<void> request(
        std::string key);

    /**
     * @brief `request()` every key; stops at the first hard failure.
     * @param keys Borrowed list of cache keys; duplicates share one in-flight job.
     */
    [[nodiscard("async resource request outcome must be checked")]] eve::Result<void> requestAll(
        std::span<const std::string> keys);

    /**
     * @brief Cache lookup that never starts a load.
     * @return Empty when missing or still decoding; borrowed cache entry otherwise.
     * @ownership Borrowed from the cache.
     * @nullable Yes — empty optional means not ready.
     * @lifetime Valid until unload/clear of that entry; do not retain across reload.
     * @thread Concurrent readers are serialized with the cache mutex.
     */
    [[nodiscard]] OptionalRef<Resource> peek(const std::string& key);

    /**
     * @brief Block until `key` is cached, failed, or unclaimed.
     * @param key Cache key; starts a load if neither cached nor in flight.
     * @return Borrowed resource on success; `NotFound` / `Failed` otherwise.
     * @ownership Borrowed from the cache on success.
     * @lifetime Same as get().
     * @thread Game thread; waits for a worker if request() is in flight.
     * @reentrancy Must not be called from a worker that this wait would join
     *             on the same pool (deadlock).
     */
    [[nodiscard("resource wait outcome must be checked")]] ResultRef<Resource> waitFor(std::string key);

    /** @brief Number of in-flight `request()` jobs (including failed-not-yet-consumed). */
    size_t pendingCount() const;

    /**
     * @brief Drop the exact cache entry `key` (parameters included).
     * The resource stays alive while other holders still reference it.
     */
    void unload(std::string key);

    /** @brief Drop every cache entry whose path matches, whatever its parameters. */
    void unloadPath(const std::string& path);

    /** @brief Number of cached entries. */
    size_t count() const;

    /** @brief Drop every entry and re-arm lazy registration (tests / teardown). */
    void clear();

    // eve::caps::IAssetReloader -- the cache participates in hot reload as the
    // first listener, refreshing CPU resources before GPU/consumers re-bind.
    /**
     * @brief Stable capability name for the cache reloader.
     * @return Borrowed, non-null, null-terminated static text.
     * @ownership Borrowed; not allocated and not caller-owned.
     * @nullable No.
     * @lifetime Static for the process lifetime.
     * @thread Thread-safe and side-effect free.
     * @reentrancy Does not invoke callbacks.
     */
    const char* reloadKind() const override { return "cache"; }
    bool handlesPath(const std::string& normPath) const override;
    [[nodiscard("resource reload outcome must be checked")]] eve::Result<bool> reload(
        const std::string& normPath) override;
    /**
     * @brief Cache provider entry point; this implementation does not claim keys.
     * @return Always nullptr for the cache provider.
     * @ownership Borrowed/null; no resource is created by this override.
     * @nullable Yes.
     * @lifetime No returned object; the call is main-thread affine during reload dispatch.
     * @thread Main/reload dispatch thread.
     * @reentrancy Does not invoke external callbacks.
     */
    [[nodiscard("loaded resource ownership must be retained or explicitly handled")]] Resource* load(
        const std::string& key) override {
        return nullptr;
    }

protected:
    ResourceManager() = default;

    void ensureRegistered();
    Resource* loadReplacement(const std::string& key);
    Resource* loadUncached(const std::string& norm);
    void runLoadJob(std::string norm, uint64_t epoch);

    struct AsyncLoad {
        bool done = false;
        bool failed = false;
        std::string error;
        uint64_t epoch = 0;
    };

    std::map<std::string, ref<Resource>> resources;
    std::unordered_map<std::string, std::shared_ptr<AsyncLoad>> pending_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    uint64_t epoch_ = 1;
    bool registered_ = false;
};

}  // namespace eve
