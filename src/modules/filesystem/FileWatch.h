#pragma once

#include "common/config.h"

#ifndef EVENGINE_WEBGPU
#include <Poco/DirectoryWatcher.h>
#endif

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::filesystem {

/**
 * @brief OS directory watch with a main-thread event queue.
 * Desktop/mobile: backed by Poco::DirectoryWatcher (see FileWatch.cpp).
 * WebGPU (Emscripten): the browser VFS has no native file watching, so this is
 * a no-op stub — add()/remove() return false, poll() returns false.
 */
class FileWatch {
public:
    struct Event {
        std::string kind;  // "added" | "removed" | "modified" | "movedFrom" | "movedTo"
        std::string path;  // report path (usually the path passed to watch())
        std::string realPath;
    };

    FileWatch();
    ~FileWatch();

    FileWatch(const FileWatch &) = delete;
    FileWatch &operator=(const FileWatch &) = delete;

    /**
     * @param realDir Absolute OS directory to watch.
     * @param filterName Empty = all entries; otherwise only this basename.
     * @param reportPath Path reported in Event::path (virtual / user path).
     * @param scanInterval Seconds for platforms that poll (Darwin MODIFIED).
     */
    bool add(const std::string &realDir, const std::string &filterName, const std::string &reportPath,
             int scanInterval = 1);

    /** @brief Remove by reportPath previously passed to add(). */
    bool remove(const std::string &reportPath);
    void clear();

    int count() const;

    /** @brief Pop one event; false if empty. */
    bool poll(Event &out);

private:
    struct DirWatch;

#ifndef EVENGINE_WEBGPU
    void onAdded(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onRemoved(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onModified(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onMovedFrom(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onMovedTo(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
#endif

    void handlePocoEvent(const std::string &kind, const std::string &itemPath);

    mutable std::mutex mu_;
    std::vector<Event> queue_;
    std::unordered_map<std::string, std::unique_ptr<DirWatch>> byDir_;
    std::unordered_map<std::string, std::string> reportToDir_;
};

}  // namespace eve::filesystem
