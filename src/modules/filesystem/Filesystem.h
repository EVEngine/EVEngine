#pragma once

#include <string>
#include <vector>

#include "common/Module.h"
#include "common/config.h"
#include "filesystem/File.h"
#include "filesystem/FileData.h"

#define EVENGINE_APPDATA_PREFIX ""
#ifdef EVENGINE_WINDOWS
#define EVENGINE_APPDATA_FOLDER "EVE"
#define EVENGINE_PATH_SEPARATOR "/"
#define EVENGINE_MAX_PATH _MAX_PATH
#else
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
#define EVENGINE_APPDATA_FOLDER "EVE"
#elif defined(EVENGINE_LINUX)
#define EVENGINE_APPDATA_FOLDER "eve"
#else
#define EVENGINE_APPDATA_PREFIX "."
#define EVENGINE_APPDATA_FOLDER "eve"
#endif
#define EVENGINE_PATH_SEPARATOR "/"
#define EVENGINE_MAX_PATH MAXPATHLEN
#endif

namespace eve::filesystem {

class Filesystem : public Module {
public:
    Module_REG;

    struct Info {
        // Numbers will be -1 if they cannot be determined.
        int64_t     size;
        int64_t     modtime;
        std::string type;  // file, directory, symlink, other
    };

    Filesystem() {}
    virtual ~Filesystem() {}

    static Filesystem *create();

    virtual void init(std::string arg0) = 0;

    virtual void setFused(bool fused) = 0;
    virtual bool isFused() const      = 0;

    /**
     * This sets up the save directory. If the
     * it is already set up, nothing happens.
     * @return True on success, false otherwise.
     **/
    virtual bool setupWriteDirectory() = 0;

    /**
     * This sets the save location on Android.
     * False for internal, true for external
     * @param external Bool for whether
     * Android should use external file storage.
     **/
    virtual void setAndroidSaveExternal(bool useExternal = false) { this->useExternal = useExternal; }

    /**
     * Gets whether the Android save is external.
     * Returns a bool.
     **/
    virtual bool isAndroidSaveExternal() const { return useExternal; }

    /**
     * Sets the name of the save folder.
     * @param ident The name of the game. Will be used to
     * to create the folder in the LOVE data folder.
     **/
    virtual bool        setIdentity(std::string ident, bool appendToPath = false) = 0;
    virtual std::string getIdentity() const                                       = 0;

    /**
     * Sets the path to the game source.
     * This can only be set once.
     * @param source Path to a directory or a .love-file.
     **/
    virtual bool setSource(std::string source) = 0;

    /**
     * Gets the path to the game source.
     * Returns a 0-length string if the source has not been set.
     **/
    virtual std::string getSource() const = 0;

    virtual bool mount(std::string archive, std::string mountpoint, bool appendToPath = false)                 = 0;
    virtual bool mount(Data *data, std::string archivename, std::string mountpoint, bool appendToPath = false) = 0;

    virtual bool unmount(std::string archive) = 0;
    virtual bool unmount(Data *data)          = 0;

    /**
     * Creates a new file.
     **/
    virtual File *newFile(std::string filename) const = 0;

    /**
     * Creates a new FileData object. Data will be copied.
     * @param data Pointer to the data.
     * @param size The size of the data.
     * @param filename The full filename used to file type identification.
     **/
    virtual FileData *newFileData(const void *data, std::string filename, size_t size) const;

    /**
     * Gets the current working directory.
     **/
    virtual std::string getWorkingDirectory() = 0;

    /**
     * Gets the user home directory.
     **/
    virtual std::string getUserDirectory() = 0;

    /**
     * Gets the APPDATA directory. On Windows, this is the folder
     * in the %APPDATA% enviroment variable. On Linux, this is the
     * user home folder.
     **/
    virtual std::string getAppdataDirectory() = 0;

    /**
     * Gets the full path of the save folder.
     **/
    virtual std::string getSaveDirectory() = 0;

    /**
     * Gets the full path to the directory containing the game source.
     * For example if the game source is C:\Games\mygame.love, this will return
     * C:\Games.
     **/
    virtual std::string getSourceBaseDirectory() const = 0;

    /**
     * Gets the real directory path containing the file.
     **/
    virtual std::string getRealDirectory(std::string filename) const = 0;

    /**
     * Gets information about the item at the specified filepath. Returns false
     * if nothing exists at the path.
     **/
    virtual bool getInfo(std::string filepath, Info &info) const = 0;

    /**
     * Creates a directory. Write dir must be set.
     * @param dir The directory to create.
     **/
    virtual bool createDirectory(std::string dir) = 0;

    /**
     * Removes a file (or directory).
     * @param file The file or directory to remove.
     **/
    virtual bool remove(std::string file) = 0;

    /**
     * Reads data from a file.
     * @param filename The name of the file to read from.
     * @param size The size in bytes of the data to read.
     **/
    virtual FileData *read(std::string filename, int64_t size = File::ALL) const = 0;

    /**
     * Write data to a file.
     * @param filename The name of the file to write to.
     * @param data The data to write.
     * @param size The size in bytes of the data to write.
     **/
    virtual void write(std::string filename, const void *data, int64_t size) const = 0;

    /**
     * Append data to a file, creating it if it doesn't exist.
     * @param filename The name of the file to write to.
     * @param data The data to append.
     * @param size The size in bytes of the data to append.
     **/
    virtual void append(std::string filename, const void *data, int64_t size) const = 0;

    /**
     * This "native" method returns a table of all
     * files in a given directory.
     **/
    virtual std::vector<std::string> getDirectoryItems(std::string dir) = 0;

    /**
     * Enable or disable symbolic link support in love.filesystem.
     **/
    virtual void setSymlinksEnabled(bool enable) = 0;

    /**
     * Gets whether symbolic link support is enabled.
     **/
    virtual bool areSymlinksEnabled() const = 0;

    // Require path accessors
    // Not const because it's R/W
    virtual std::vector<std::string> &getRequirePath()  = 0;
    virtual std::vector<std::string> &getCRequirePath() = 0;

    /**
     * Allows a full (OS-dependent) path to be used with Filesystem::mount.
     **/
    virtual void allowMountingForPath(const std::string &path) = 0;

    /**
     * Gets whether the given full (OS-dependent) path is a directory.
     **/
    virtual bool isRealDirectory(const std::string &path) const;

    /**
     * Gets the full platform-dependent path to the executable.
     **/
    virtual std::string getExecutablePath() const;

private:
    // Should we save external or internal for Android
    bool useExternal;
};  // Filesystem

}  // namespace eve::filesystem
