#pragma once
#include "Object.h"
#include "common/AssetReloader.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <string>
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
     * the same concrete type as `this`.
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
	 */
	Resource *get(std::string key);

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
    const char* reloadKind() const override { return "cache"; }
    bool handlesPath(const std::string& normPath) const override;
    bool reload(const std::string& normPath) override;
    Resource* load(const std::string& key) override { return nullptr; }

protected:
    ResourceManager() = default;

    void ensureRegistered();
    bool refreshEntry(const std::string& key, std::set<std::string>& visited);
    void refreshDependents(Resource* updated, std::set<std::string>& visited);

    std::map<std::string, ref<Resource>> resources;
    mutable std::mutex mu_;
    bool registered_ = false;
};

}  // namespace eve
