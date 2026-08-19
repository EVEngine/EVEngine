#include "common/Resource.h"
#include "common/Capability.h"

#include <mutex>

namespace eve {

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    instance.ensureRegistered();
    return instance;
}

std::string ResourceManager::normalizePath(std::string path) {
    for (char &c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

std::string ResourceManager::makeKey(const std::string &path, const std::string &query) {
    std::string key = normalizePath(path);
    if (!query.empty()) {
        key += '?';
        key += query;
    }
    return key;
}

std::string ResourceManager::pathOfKey(const std::string &key) {
    const auto q = key.find('?');
    return normalizePath(q == std::string::npos ? key : key.substr(0, q));
}

void ResourceManager::ensureRegistered() {
    std::lock_guard<std::mutex> lock(mu_);
    if (registered_) return;
    eve::cap::addListener<eve::caps::IAssetReloader>(this, eve::caps::IAssetReloader::kCache);
    registered_ = true;
}

size_t ResourceManager::count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return resources.size();
}

Resource *ResourceManager::get(std::string key) {
    const std::string norm = makeKey(std::move(key));
    if (norm.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = resources.find(norm);
        if (it != resources.end()) return it->second.get();
    }

    // The provider load (file read + decode) is the expensive part; run it
    // outside the lock so concurrent insertions are not serialized behind it.
    Resource *loaded = nullptr;
    eve::cap::forEachUntil<eve::caps::IAssetReloader>([&](eve::caps::IAssetReloader *r) {
        if (r == this) return false;  // the cache itself never loads
        if (!r->handlesPath(norm)) return false;
        loaded = r->load(norm);
        return loaded != nullptr;
    });

    if (loaded == nullptr) return nullptr;
    loaded->setUri(norm);

    std::lock_guard<std::mutex> lock(mu_);
    auto [it, inserted] = resources.emplace(norm, loaded);
    if (!inserted) {
        // A concurrent get() may have won the race; emplace does not consume
        // the arguments when the key already exists, so discard our instance.
        delete loaded;
    }
    return it->second.get();
}

void ResourceManager::unload(std::string key) {
    std::lock_guard<std::mutex> lock(mu_);
    resources.erase(makeKey(std::move(key)));
}

void ResourceManager::unloadPath(const std::string &path) {
    const std::string norm = normalizePath(path);
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = resources.begin(); it != resources.end();) {
        if (pathOfKey(it->first) == norm)
            it = resources.erase(it);
        else
            ++it;
    }
}

void ResourceManager::clear() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        resources.clear();
    }
    if (registered_) {
        eve::cap::removeListener<eve::caps::IAssetReloader>(this);
        registered_ = false;
    }
}

bool ResourceManager::handlesPath(const std::string &normPath) const {
    const std::string norm = normalizePath(normPath);
    if (norm.empty()) return false;
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &kv : resources) {
        if (pathOfKey(kv.first) == norm) return true;
    }
    return false;
}

bool ResourceManager::reload(const std::string &normPath) {
    const std::string norm = normalizePath(normPath);
    if (norm.empty()) return false;

    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &kv : resources) {
            if (pathOfKey(kv.first) == norm) keys.push_back(kv.first);
        }
    }

    std::set<std::string> visited;
    bool any = false;
    for (const auto &key : keys) {
        if (refreshEntry(key, visited)) any = true;
    }
    return any;
}

bool ResourceManager::refreshEntry(const std::string &key, std::set<std::string> &visited) {
    if (!visited.insert(key).second) return false;  // cycle guard

    Resource *cached = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = resources.find(key);
        if (it == resources.end()) return false;
        cached = it->second.get();  // identity token; the cache entry keeps it alive
    }

    Resource *replacement = nullptr;
    eve::cap::forEachUntil<eve::caps::IAssetReloader>([&](eve::caps::IAssetReloader *r) {
        if (r == this) return false;
        if (!r->handlesPath(key)) return false;
        try {
            replacement = r->load(key);
        } catch (...) {
            replacement = nullptr;  // keep the previous contents on a failed reload
        }
        return replacement != nullptr;
    });
    if (replacement == nullptr) return false;

    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = resources.find(key);
        if (it == resources.end()) {
            delete replacement;  // entry unloaded while we were loading
            return false;
        }
        it->second->adopt(*replacement);
    }
    delete replacement;

    refreshDependents(cached, visited);
    return true;
}

void ResourceManager::refreshDependents(Resource *updated, std::set<std::string> &visited) {
    std::vector<std::string> dependents;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto &kv : resources) {
            if (visited.count(kv.first)) continue;
            for (auto dep : kv.second->getDependencies()) {  // copy: operator-> is non-const
                if (dep.get() == updated) {
                    dependents.push_back(kv.first);
                    break;
                }
            }
        }
    }
    for (const auto &key : dependents) refreshEntry(key, visited);
}

}  // namespace eve
