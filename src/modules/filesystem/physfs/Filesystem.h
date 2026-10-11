#pragma once
#include "common/Export.h"


#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "filesystem/Filesystem.h"
#include "filesystem/FileWatch.h"

namespace eve {
namespace filesystem {
namespace physfs {

/** @brief PhysFS 文件系统后端实现（挂载/解挂、读写、热重载）。 */
class EVENGINE_API_FOUNDATION Filesystem final : public eve::filesystem::Filesystem {
public:
    /** @brief Filesystem. */
    Filesystem();
    /** @brief Filesystem. */
    ~Filesystem() override;

    /** @brief Initializes init. */
    void init(const char* arg0) override;

    /** @brief Sets the fused. */
    void setFused(bool value) override;
    /** @brief True when fused. */
    bool isFused() const override;

    /** @brief Setup write directory. */
    bool setupWriteDirectory() override;

    /** @brief Sets the identity. */
    bool        setIdentity(std::string ident, bool appendToPath = false) override;
    /** @brief Returns the identity. */
    std::string getIdentity() const override;

    /** @brief Sets the source. */
    bool setSource(std::string source) override;
    /** @brief Sets the source from memory. */
    bool setSourceFromMemory(const void* data, size_t size) override;

    /** @brief Returns the source. */
    std::string getSource() const override;

    /** @brief Mounts mount. */
    bool mount(std::string archive, std::string mountpoint, bool appendToPath = false) override;
    /** @brief Mounts mount. */
    bool mount(Data *data, std::string archivename, std::string mountpoint, bool appendToPath = false) override;
    /** @brief Mounts real directory. */
    bool mountRealDirectory(std::string realDir, std::string mountpoint, bool appendToPath = false) override;
    /** @brief Unmounts real directory. */
    bool unmountRealDirectory(std::string realDir) override;

    /** @brief Unmounts unmount. */
    bool unmount(std::string archive) override;
    /** @brief Unmounts unmount. */
    bool unmount(Data *data) override;

    /** @brief Creates a file. @ownership Caller deletes unless documented otherwise. */
    filesystem::File *newFile(std::string filename) const override;

    /** @brief Returns the working directory. */
    std::string getWorkingDirectory() override;
    /** @brief Returns the user directory. */
    std::string getUserDirectory() override;
    /** @brief Returns the appdata directory. */
    std::string getAppdataDirectory() override;
    /** @brief Returns the save directory. */
    std::string getSaveDirectory() override;
    /** @brief Returns the source base directory. */
    std::string getSourceBaseDirectory() const override;

    /** @brief Returns the real directory. */
    std::string getRealDirectory(std::string filename) const override;

    /** @brief Returns the info. */
    bool getInfo(std::string filepath, Info &info) const override;

    /** @brief Creates directory. */
    bool createDirectory(std::string dir) override;

    /** @brief Removes remove. */
    bool remove(std::string file) override;

    /** @brief Reads read. */
    FileData *read(std::string filename, int64_t size = File::ALL) const override;
    /** @brief Writes write. */
    void      write(std::string filename, const void *data, int64_t size) const override;
    /** @brief Append. */
    void      append(std::string filename, const void *data, int64_t size) const override;

    /** @brief Returns the directory items. */
    std::vector<std::string> getDirectoryItems(std::string dir) override;

    /** @brief Sets the symlinks enabled. */
    void setSymlinksEnabled(bool enable) override;
    /** @brief Are symlinks enabled. */
    bool areSymlinksEnabled() const override;

    /** @brief Returns the require path. */
    std::vector<std::string> &getRequirePath() override;
    /** @brief Returns the c require path. */
    std::vector<std::string> &getCRequirePath() override;

    /** @brief Allow mounting for path. */
    void allowMountingForPath(const std::string &path) override;

    /** @brief Watches watch. */
    bool watch(std::string path) override;
    /** @brief Watches real directory. */
    [[nodiscard]] eve::Result<void> watchRealDirectory(std::string realDir,
                                                       std::string reportPath) override;
    /** @brief Stops watching unwatch. */
    bool unwatch(std::string path) override;
    /** @brief Stops watching all. */
    void unwatchAll() override;
    /** @brief Returns the watch count. */
    int getWatchCount() const override;
    /** @brief Polls watch. */
    std::string pollWatch() override;
    /** @brief Returns the last watch path. */
    std::string getLastWatchPath() const override;
    /** @brief Returns the last watch real path. */
    std::string getLastWatchRealPath() const override;

private:
    bool resolveWatchTarget(const std::string &path, std::string &realDir, std::string &filterName,
                            std::string &reportPath);
    FileWatch &watchers();

    // Contains the current working directory (UTF8).
    std::string cwd;

    // %APPDATA% on Windows.
    std::string appdata;

    // This name will be used to create the folder
    // in the appdata/userdata folder.
    std::string save_identity;

    // Full and relative paths of the game save folder.
    // (Relative to the %APPDATA% folder, meaning that the
    // relative string will look something like: ./LOVE/game)
    std::string save_path_relative, save_path_full;

    // The full path to the source of the game.
    std::string game_source;

    // Allow saving outside of the LOVE_APPDATA_FOLDER
    // for release 'builds'
    bool fused;
    bool fusedSet;

    // Search path for require
    std::vector<std::string> requirePath;
    std::vector<std::string> cRequirePath;

    std::vector<std::string> allowedMountPaths;

    std::map<std::string, Data*> mountedData;

    std::unique_ptr<FileWatch> fileWatch_;
    std::string lastWatchPath_;
    std::string lastWatchRealPath_;

    // Real directories mounted as virtual overlays (see mountRealDirectory).
    std::vector<std::string> mountedRealDirs_;
    std::mutex mountMu_;

    // Owns the bytes backing a memory-mounted game archive (see setSourceFromMemory).
    void* memoryArchive_ = nullptr;

};  // Filesystem

}  // namespace physfs
}  // namespace filesystem
}  // namespace eve
