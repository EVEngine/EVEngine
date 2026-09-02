#include "common/Resource.h"
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"

#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace eve {

ResourceManager& ResourceManager::getInstance() {
    // Intentionally leaked: cached CPU resources may own third-party handles
    // (FreeType faces, Assimp scenes, image decode handlers) whose libraries
    // are torn down at process exit in an unspecified TU order. Destroying
    // cached entries from the singleton destructor can therefore crash at
    // exit. Keeping the singleton alive until the OS reclaims it avoids
    // exit-time destructors entirely; explicit unload()/clear() still release
    // entries during the run.
    static ResourceManager* instance = new ResourceManager();
    instance->ensureRegistered();
    return *instance;
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

Resource *ResourceManager::loadUncached(const std::string &norm) {
    Resource *loaded = nullptr;
    eve::cap::forEachUntil<eve::caps::IAssetReloader>([&](eve::caps::IAssetReloader *r) {
        if (r == this) return false;
        if (!r->handlesPath(norm)) return false;
        loaded = r->load(norm);
        return loaded != nullptr;
    });
    if (loaded) loaded->setUri(norm);
    return loaded;
}

void ResourceManager::runLoadJob(std::string norm, uint64_t epoch) {
    Resource *loaded = nullptr;
    std::string error;
    try {
        loaded = loadUncached(norm);
    } catch (const std::exception &ex) {
        error = ex.what();
    } catch (...) {
        error = "unknown resource load failure";
    }

    std::lock_guard<std::mutex> lock(mu_);
    auto pendingIt = pending_.find(norm);
    if (epoch_ != epoch) {
        delete loaded;
        if (pendingIt != pending_.end()) {
            pendingIt->second->done = true;
            pending_.erase(pendingIt);
        }
        cv_.notify_all();
        return;
    }
    if (!error.empty() || loaded == nullptr) {
        if (pendingIt != pending_.end()) {
            pendingIt->second->done = true;
            pendingIt->second->failed = true;
            pendingIt->second->error =
                error.empty() ? std::string("no provider claimed key") : std::move(error);
            pending_.erase(pendingIt);
        }
        delete loaded;
        cv_.notify_all();
        return;
    }
    auto [it, inserted] = resources.emplace(norm, loaded);
    if (!inserted) delete loaded;
    if (pendingIt != pending_.end()) {
        pendingIt->second->done = true;
        pending_.erase(pendingIt);
    }
    cv_.notify_all();
}

Resource *ResourceManager::get(std::string key) {
    auto waited = waitFor(std::move(key));
    if (waited.ok()) return &waited.value().get();
    if (waited.code() == eve::StatusCode::NotFound) return nullptr;
    const std::string detail = waited.status().describe();
    throw eve::Exception("%s", detail.c_str());
}

eve::Result<void> ResourceManager::request(std::string key) {
    const std::string norm = makeKey(std::move(key));
    if (norm.empty()) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "empty resource key"));
    }
    ensureRegistered();

    std::shared_ptr<AsyncLoad> job;
    uint64_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto cached = resources.find(norm);
        if (cached != resources.end())
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
        auto pendingIt = pending_.find(norm);
        if (pendingIt != pending_.end() && !pendingIt->second->done)
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Pending));
        if (pendingIt != pending_.end()) pending_.erase(pendingIt);

        bool claimed = false;
        eve::cap::forEachUntil<eve::caps::IAssetReloader>([&](eve::caps::IAssetReloader *r) {
            if (r == this) return false;
            if (!r->handlesPath(norm)) return false;
            claimed = true;
            return true;
        });
        if (!claimed) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no provider claimed key", norm));
        }

        job = std::make_shared<AsyncLoad>();
        job->epoch = epoch_;
        epoch = epoch_;
        pending_.emplace(norm, job);
    }

    auto *executor = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    if (!executor) {
        runLoadJob(norm, epoch);
        std::lock_guard<std::mutex> lock(mu_);
        if (resources.find(norm) != resources.end())
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "inline resource load failed", norm));
    }

    auto submitted = executor->submit([this, norm, epoch]() { runLoadJob(norm, epoch); });
    if (!submitted.ok()) {
        std::lock_guard<std::mutex> lock(mu_);
        pending_.erase(norm);
        cv_.notify_all();
        return submitted;
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ResourceManager::requestAll(std::span<const std::string> keys) {
    for (const auto &key : keys) {
        auto one = request(key);
        if (!one.ok()) return one;
        one.ignore("queued or already cached/pending");
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

OptionalRef<Resource> ResourceManager::peek(const std::string &key) {
    const std::string norm = makeKey(key);
    std::lock_guard<std::mutex> lock(mu_);
    auto it = resources.find(norm);
    if (it == resources.end()) return std::nullopt;
    return std::ref(*it->second.get());
}

ResultRef<Resource> ResourceManager::waitFor(std::string key) {
    const std::string norm = makeKey(std::move(key));
    if (norm.empty()) {
        return ResultRef<Resource>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "empty resource key"));
    }
    ensureRegistered();

    std::shared_ptr<AsyncLoad> job;
    {
        std::unique_lock<std::mutex> lock(mu_);
        auto cached = resources.find(norm);
        if (cached != resources.end())
            return ResultRef<Resource>::success(std::ref(*cached->second.get()));
        auto pendingIt = pending_.find(norm);
        if (pendingIt != pending_.end()) job = pendingIt->second;
        if (job) {
            cv_.wait(lock, [&] { return job->done || resources.find(norm) != resources.end(); });
            cached = resources.find(norm);
            if (cached != resources.end())
                return ResultRef<Resource>::success(std::ref(*cached->second.get()));
            if (job->failed) {
                return ResultRef<Resource>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed,
                    job->error.empty() ? "async resource load failed" : job->error, norm));
            }
        }
    }

    if (job && job->done) {
        std::lock_guard<std::mutex> lock(mu_);
        auto cached = resources.find(norm);
        if (cached != resources.end())
            return ResultRef<Resource>::success(std::ref(*cached->second.get()));
        if (job->failed) {
            return ResultRef<Resource>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed,
                job->error.empty() ? "async resource load failed" : job->error, norm));
        }
    }

    Resource *loaded = nullptr;
    try {
        loaded = loadUncached(norm);
    } catch (const std::exception &ex) {
        return ResultRef<Resource>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, ex.what(), norm));
    }
    if (!loaded) {
        return ResultRef<Resource>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no provider claimed key", norm));
    }

    std::lock_guard<std::mutex> lock(mu_);
    auto [it, inserted] = resources.emplace(norm, loaded);
    if (!inserted) delete loaded;
    return ResultRef<Resource>::success(std::ref(*it->second.get()));
}

size_t ResourceManager::pendingCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pending_.size();
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
        std::unique_lock<std::mutex> lock(mu_);
        ++epoch_;
        cv_.wait(lock, [&] {
            for (const auto &kv : pending_) {
                if (!kv.second->done) return false;
            }
            return true;
        });
        pending_.clear();
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

eve::Result<bool> ResourceManager::reload(const std::string &normPath) {
    const std::string norm = normalizePath(normPath);
    if (norm.empty()) return eve::Result<bool>::success(false);

    struct Prepared {
        std::string               key;
        Resource                 *cached = nullptr;
        std::unique_ptr<Resource> replacement;
    };

    std::vector<std::string> keys;
    std::set<std::string>     selectedKeys;
    std::set<Resource *>      selectedResources;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto &kv : resources) {
            if (pathOfKey(kv.first) != norm) continue;
            keys.push_back(kv.first);
            selectedKeys.insert(kv.first);
            selectedResources.insert(kv.second.get());
        }

        // Close over reverse dependencies before loading anything. This gives
        // the transaction a stable, root-first order and makes cycles safe.
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (auto &kv : resources) {
                if (selectedKeys.count(kv.first)) continue;
                bool dependsOnSelection = false;
                for (auto dependency : kv.second->getDependencies()) {
                    if (selectedResources.count(dependency.get())) {
                        dependsOnSelection = true;
                        break;
                    }
                }
                if (!dependsOnSelection) continue;
                keys.push_back(kv.first);
                selectedKeys.insert(kv.first);
                selectedResources.insert(kv.second.get());
                expanded = true;
            }
        }
    }
    if (keys.empty()) return eve::Result<bool>::success(false);

    // Decode every root and dependent into detached candidates. A single
    // invalid file aborts the whole graph without touching live objects.
    std::vector<Prepared> prepared;
    std::vector<ref<Resource>> keepAlive;
    prepared.reserve(keys.size());
    keepAlive.reserve(keys.size());
    for (const auto &key : keys) {
        Resource *cached = nullptr;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = resources.find(key);
            if (it == resources.end()) return eve::Result<bool>::success(false);
            cached = it->second.get();
            keepAlive.emplace_back(cached);
        }
        std::unique_ptr<Resource> replacement(loadReplacement(key));
        if (!replacement) return eve::Result<bool>::success(false);
        prepared.push_back({key, cached, std::move(replacement)});
    }

    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &item : prepared) {
        auto it = resources.find(item.key);
        if (it == resources.end() || it->second.get() != item.cached) return eve::Result<bool>::success(false);
    }

    size_t committed = 0;
    try {
        for (; committed < prepared.size(); ++committed)
            prepared[committed].cached->adopt(*prepared[committed].replacement);
    } catch (...) {
        // adopt() is a payload swap. Candidates therefore hold the old state
        // after a successful commit and can restore it in reverse order.
        while (committed > 0) {
            --committed;
            try {
                prepared[committed].cached->adopt(*prepared[committed].replacement);
            } catch (...) {
                // The adopt contract requires a non-mutating failure. Keep
                // unwinding other entries even if a broken implementation
                // violates it.
            }
        }
        return eve::Result<bool>::success(false);
    }
    return eve::Result<bool>::success(true, eve::Status::success(eve::StatusCode::Applied));
}

Resource *ResourceManager::loadReplacement(const std::string &key) {
    try {
        return loadUncached(key);
    } catch (...) {
        return nullptr;
    }
}

}  // namespace eve
