#pragma once

#include "common/config.h"
#include "filesystem/File.h"

// PhysFS
#include "physfs/physfs.h"

// STD
#include <string>

namespace eve {
namespace filesystem {
namespace physfs {

class File : public eve::filesystem::File {
public:
    /**
     * Constructs an File with the given ilename.
     * @param filename The relative filepath of the file to load.
     **/
    File(std::string filename);
    virtual ~File();

    // Implements eve::filesystem::File.
	using eve::filesystem::File::read;
	using eve::filesystem::File::write;
    bool        open(std::string mode) override;
    bool        close() override;
    bool        isOpen() const override;
    int64_t     getSize() override;
    int64_t     read(void *dst, int64_t size) override;
    bool        write(const void *data, int64_t size) override;
    bool        flush() override;
    bool        isEOF() override;
    int64_t     tell() override;
    bool        seek(uint64_t pos) override;
    bool        setBuffer(std::string bufmode, int64_t size) override;
    std::string getBuffer(int64_t &size) const override;
    std::string getMode() const override;
    std::string getFilename() const override;

private:
    // filename
    std::string filename;

    // PHYSFS File handle.
    PHYSFS_File *file;

    // The current mode of the file.
    std::string mode;

    std::string bufferMode;
    int64_t     bufferSize;

};  // File

}  // namespace physfs
}  // namespace filesystem
}  // namespace eve
