#include "filesystem/FileWatch.h"
#include "common/config.h"

#ifndef EVENGINE_WEBGPU

#include <Poco/Delegate.h>
#include <Poco/File.h>
#include <Poco/Path.h>

#include <cstdlib>

#endif

namespace eve::filesystem {

#ifdef EVENGINE_WEBGPU
// WebGPU (browser) build has no native directory watching; DirWatch stays an
// empty struct so the unique_ptr members in the header compile.
struct FileWatch::DirWatch {};
#endif

#ifndef EVENGINE_WEBGPU
namespace {

std::string basenameOf(const std::string &path) {
    Poco::Path p(path);
    return p.getFileName();
}

std::string normalizeDir(const std::string &dir) {
    std::string s;
#if !defined(_WIN32)
    // Resolve symlinks (/var → /private/var on macOS) so watch keys match event paths.
    char *resolved = realpath(dir.c_str(), nullptr);
    if (resolved) {
        s = resolved;
        free(resolved);
    } else
#endif
    {
        Poco::Path p(dir);
        p.makeAbsolute();
        s = p.toString();
    }
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

}  // namespace

struct FileWatch::DirWatch {
    std::string realDir;
    std::unique_ptr<Poco::DirectoryWatcher> watcher;
    std::unordered_map<std::string, std::string> filters;  // filter → reportPath
    int refs = 0;
};
#endif  // !EVENGINE_WEBGPU

FileWatch::FileWatch() = default;

FileWatch::~FileWatch() { clear(); }

bool FileWatch::add(const std::string &realDir, const std::string &filterName,
                    const std::string &reportPath, int scanInterval) {
#ifdef EVENGINE_WEBGPU
    // Browser VFS has no native directory watching; no-op stub.
    (void)realDir;
    (void)filterName;
    (void)reportPath;
    (void)scanInterval;
    return false;
#else
    if (realDir.empty() || reportPath.empty()) return false;
    const std::string dir = normalizeDir(realDir);
    try {
        Poco::File f(dir);
        if (!f.exists() || !f.isDirectory()) return false;
    } catch (...) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mu_);

    auto prev = reportToDir_.find(reportPath);
    if (prev != reportToDir_.end()) {
        auto it = byDir_.find(prev->second);
        if (it != byDir_.end()) {
            DirWatch *dw = it->second.get();
            for (auto fit = dw->filters.begin(); fit != dw->filters.end();) {
                if (fit->second == reportPath) {
                    fit = dw->filters.erase(fit);
                    --dw->refs;
                } else {
                    ++fit;
                }
            }
            if (dw->refs <= 0) {
                if (dw->watcher) {
                    dw->watcher->itemAdded -= Poco::delegate(this, &FileWatch::onAdded);
                    dw->watcher->itemRemoved -= Poco::delegate(this, &FileWatch::onRemoved);
                    dw->watcher->itemModified -= Poco::delegate(this, &FileWatch::onModified);
                    dw->watcher->itemMovedFrom -= Poco::delegate(this, &FileWatch::onMovedFrom);
                    dw->watcher->itemMovedTo -= Poco::delegate(this, &FileWatch::onMovedTo);
                }
                byDir_.erase(it);
            }
        }
        reportToDir_.erase(prev);
    }

    DirWatch *dw = nullptr;
    auto it = byDir_.find(dir);
    if (it == byDir_.end()) {
        auto owned = std::make_unique<DirWatch>();
        owned->realDir = dir;
        try {
            owned->watcher = std::make_unique<Poco::DirectoryWatcher>(
                dir, Poco::DirectoryWatcher::DW_FILTER_ENABLE_ALL, scanInterval);
        } catch (...) {
            return false;
        }
        owned->watcher->itemAdded += Poco::delegate(this, &FileWatch::onAdded);
        owned->watcher->itemRemoved += Poco::delegate(this, &FileWatch::onRemoved);
        owned->watcher->itemModified += Poco::delegate(this, &FileWatch::onModified);
        owned->watcher->itemMovedFrom += Poco::delegate(this, &FileWatch::onMovedFrom);
        owned->watcher->itemMovedTo += Poco::delegate(this, &FileWatch::onMovedTo);
        dw = owned.get();
        byDir_[dir] = std::move(owned);
    } else {
        dw = it->second.get();
    }

    dw->filters[filterName] = reportPath;
    ++dw->refs;
    reportToDir_[reportPath] = dir;
    return true;
#endif
}

bool FileWatch::remove(const std::string &reportPath) {
#ifdef EVENGINE_WEBGPU
    (void)reportPath;
    return false;
#else
    std::lock_guard<std::mutex> lock(mu_);
    auto prev = reportToDir_.find(reportPath);
    if (prev == reportToDir_.end()) return false;
    const std::string dir = prev->second;
    reportToDir_.erase(prev);

    auto it = byDir_.find(dir);
    if (it == byDir_.end()) return false;
    DirWatch *dw = it->second.get();
    for (auto fit = dw->filters.begin(); fit != dw->filters.end();) {
        if (fit->second == reportPath) {
            fit = dw->filters.erase(fit);
            --dw->refs;
        } else {
            ++fit;
        }
    }
    if (dw->refs <= 0) {
        if (dw->watcher) {
            dw->watcher->itemAdded -= Poco::delegate(this, &FileWatch::onAdded);
            dw->watcher->itemRemoved -= Poco::delegate(this, &FileWatch::onRemoved);
            dw->watcher->itemModified -= Poco::delegate(this, &FileWatch::onModified);
            dw->watcher->itemMovedFrom -= Poco::delegate(this, &FileWatch::onMovedFrom);
            dw->watcher->itemMovedTo -= Poco::delegate(this, &FileWatch::onMovedTo);
        }
        byDir_.erase(it);
    }
    return true;
#endif
}

void FileWatch::clear() {
    std::lock_guard<std::mutex> lock(mu_);
#ifdef EVENGINE_WEBGPU
    queue_.clear();
    return;
#else
    for (auto &kv : byDir_) {
        DirWatch *dw = kv.second.get();
        if (dw && dw->watcher) {
            dw->watcher->itemAdded -= Poco::delegate(this, &FileWatch::onAdded);
            dw->watcher->itemRemoved -= Poco::delegate(this, &FileWatch::onRemoved);
            dw->watcher->itemModified -= Poco::delegate(this, &FileWatch::onModified);
            dw->watcher->itemMovedFrom -= Poco::delegate(this, &FileWatch::onMovedFrom);
            dw->watcher->itemMovedTo -= Poco::delegate(this, &FileWatch::onMovedTo);
        }
    }
    byDir_.clear();
    reportToDir_.clear();
    queue_.clear();
#endif
}

int FileWatch::count() const {
    std::lock_guard<std::mutex> lock(mu_);
#ifdef EVENGINE_WEBGPU
    return 0;
#else
    return int(reportToDir_.size());
#endif
}

bool FileWatch::poll(Event &out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.erase(queue_.begin());
    return true;
}

#ifndef EVENGINE_WEBGPU
void FileWatch::handlePocoEvent(const std::string &kind, const std::string &itemPath) {
    const std::string name = basenameOf(itemPath);
    Poco::Path parent(itemPath);
    parent.makeParent();
    const std::string dir = normalizeDir(parent.toString());

    std::lock_guard<std::mutex> lock(mu_);
    auto it = byDir_.find(dir);
    if (it == byDir_.end()) {
        for (auto &kv : byDir_) {
            if (itemPath.size() >= kv.first.size() && itemPath.compare(0, kv.first.size(), kv.first) == 0) {
                it = byDir_.find(kv.first);
                break;
            }
        }
    }
    if (it == byDir_.end()) return;
    DirWatch *dw = it->second.get();

    auto push = [&](const std::string &report) {
        Event ev;
        ev.kind = kind;
        ev.path = report;
        ev.realPath = itemPath;
        queue_.push_back(std::move(ev));
    };

    auto all = dw->filters.find("");
    if (all != dw->filters.end()) {
        std::string report = all->second;
        if (!report.empty() && report.back() != '/') report += "/";
        report += name;
        push(report);
    }

    auto fit = dw->filters.find(name);
    if (fit != dw->filters.end()) push(fit->second);
}

void FileWatch::onAdded(const void *, const Poco::DirectoryWatcher::DirectoryEvent &event) {
    handlePocoEvent("added", event.item.path());
}

void FileWatch::onRemoved(const void *, const Poco::DirectoryWatcher::DirectoryEvent &event) {
    handlePocoEvent("removed", event.item.path());
}

void FileWatch::onModified(const void *, const Poco::DirectoryWatcher::DirectoryEvent &event) {
    handlePocoEvent("modified", event.item.path());
}

void FileWatch::onMovedFrom(const void *, const Poco::DirectoryWatcher::DirectoryEvent &event) {
    handlePocoEvent("movedFrom", event.item.path());
}

void FileWatch::onMovedTo(const void *, const Poco::DirectoryWatcher::DirectoryEvent &event) {
    handlePocoEvent("movedTo", event.item.path());
}
#endif  // !EVENGINE_WEBGPU

}  // namespace eve::filesystem
