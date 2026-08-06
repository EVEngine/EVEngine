#pragma once

#include <Poco/DirectoryWatcher.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::filesystem {

/**
 * OS directory watch (Poco::DirectoryWatcher) with a main-thread event queue.
 * Watch a directory (all children) or a single file (parent dir + name filter).
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

    /** Remove by reportPath previously passed to add(). */
    bool remove(const std::string &reportPath);
    void clear();

    int count() const;

    /** Pop one event; false if empty. */
    bool poll(Event &out);

private:
    struct DirWatch;

    void onAdded(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onRemoved(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onModified(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onMovedFrom(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);
    void onMovedTo(const void *sender, const Poco::DirectoryWatcher::DirectoryEvent &event);

    void handlePocoEvent(const std::string &kind, const std::string &itemPath);

    mutable std::mutex mu_;
    std::vector<Event> queue_;
    std::unordered_map<std::string, std::unique_ptr<DirWatch>> byDir_;
    std::unordered_map<std::string, std::string> reportToDir_;
};

}  // namespace eve::filesystem
