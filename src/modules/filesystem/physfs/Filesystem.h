#pragma once

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
class Filesystem final : public eve::filesystem::Filesystem {
public:
    Filesystem();
    ~Filesystem() override;

    void init(const char* arg0) override;

    void setFused(bool value) override;
    bool isFused() const override;

    bool setupWriteDirectory() override;

    bool        setIdentity(std::string ident, bool appendToPath = false) override;
    std::string getIdentity() const override;

    bool setSource(std::string source) override;
    bool setSourceFromMemory(const void* data, size_t size) override;

    std::string getSource() const override;

    bool mount(std::string archive, std::string mountpoint, bool appendToPath = false) override;
    bool mount(Data *data, std::string archivename, std::string mountpoint, bool appendToPath = false) override;
    bool mountRealDirectory(std::string realDir, std::string mountpoint, bool appendToPath = false) override;
    bool unmountRealDirectory(std::string realDir) override;

    bool unmount(std::string archive) override;
    bool unmount(Data *data) override;

    filesystem::File *newFile(std::string filename) const override;

    std::string getWorkingDirectory() override;
    std::string getUserDirectory() override;
    std::string getAppdataDirectory() override;
    std::string getSaveDirectory() override;
    std::string getSourceBaseDirectory() const override;

    std::string getRealDirectory(std::string filename) const override;

    bool getInfo(std::string filepath, Info &info) const override;

    bool createDirectory(std::string dir) override;

    bool remove(std::string file) override;

    FileData *read(std::string filename, int64_t size = File::ALL) const override;
    void      write(std::string filename, const void *data, int64_t size) const override;
    void      append(std::string filename, const void *data, int64_t size) const override;

    std::vector<std::string> getDirectoryItems(std::string dir) override;

    void setSymlinksEnabled(bool enable) override;
    bool areSymlinksEnabled() const override;

    std::vector<std::string> &getRequirePath() override;
    std::vector<std::string> &getCRequirePath() override;

    void allowMountingForPath(const std::string &path) override;

    bool watch(std::string path) override;
    bool unwatch(std::string path) override;
    void unwatchAll() override;
    int getWatchCount() const override;
    std::string pollWatch() override;
    std::string getLastWatchPath() const override;
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
