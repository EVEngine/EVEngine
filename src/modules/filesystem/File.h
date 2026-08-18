#pragma once

#include <string>

#include "filesystem/FileData.h"

namespace eve {
namespace filesystem {

/**
 * @brief A File interface, providing generic means of reading from and
 * writing to files.
 **/
class File {
public:
    /**
     * @brief Used to indicate ALL data in a file.
     **/
    static const int64_t ALL = -1;

    /**
     * @brief Destructor.
     **/
    virtual ~File();

    /**
     * @brief Opens the file in a certain mode.
     *
     * @param mode read(r), write(w), append(a).
     * @return True if successful, false otherwise.
     **/
    virtual bool open(std::string mode) = 0;

    /**
     * @brief Closes the file.
     *
     * @return True if successful, false otherwise.
     **/
    virtual bool close() = 0;

    /**
     * @brief Gets whether the file is open.
     **/
    virtual bool isOpen() const = 0;

    /**
     * @brief Gets the size of the file.
     *
     * @return The size of the file.
     **/
    virtual int64_t getSize() = 0;

    /**
     * @brief Reads data from the file and allocates a Data object.
     *
     * @param size The number of bytes to attempt reading, or -1 for EOF.
     * @return A newly allocated Data object.
     **/
    virtual FileData *read(int64_t size = ALL);

    /**
     * @brief Reads data into the destination buffer.
     *
     * @param dst The destination buffer.
     * @param size The number of bytes to attempt reading.
     * @return The number of bytes actually read.
     **/
    virtual int64_t read(void *dst, int64_t size) = 0;

    /**
     * @brief Writes data into the File.
     *
     * @param data The source buffer.
     * @param size The size of the buffer.
     * @return True of success, false otherwise.
     **/
    virtual bool write(const void *data, int64_t size) = 0;

    /**
     * @brief Writes a Data object into the File.
     *
     * @param data The data object to write into the file.
     * @param size The number of bytes to attempt writing, or -1 for everything.
     * @return True of success, false otherwise.
     **/
    virtual bool write(const Data *data, int64_t size = ALL);

    /**
     * @brief Flushes the currently buffered file data to disk. Only applicable in
     * write mode.
     **/
    virtual bool flush() = 0;

    /**
     * @brief Checks whether we are currently at end-of-file.
     *
     * @return True if EOF, false otherwise.
     **/
    virtual bool isEOF() = 0;

    /**
     * @brief Gets the current position in the File.
     *
     * @return The current byte position in the File.
     **/
    virtual int64_t tell() = 0;

    /**
     * @brief Seeks to a certain position in the File.
     *
     * @param pos The byte position in the file.
     * @return True on success, false otherwise.
     **/
    virtual bool seek(uint64_t pos) = 0;

    /**
     * @brief Sets the buffering mode for the file. When buffering is enabled,
     *
     * 'none'    - no buffer enabled
     * 'full'    - the file will not write to disk (or will pre-load data if in read mode) until the
     *             buffer's capacity is reached.
     * 'newline' - the file will not only write when capacity is reached, but also write to disk
     *             if a newline is written.
     *
     * @param bufmode The buffer mode.
     * @param size The size in bytes of the buffer.
     **/
    virtual bool setBuffer(std::string bufmode, int64_t size) = 0;

    /**
     * @param[out] size The size in bytes of the buffer.
     * @return The current buffer mode.
     **/
    virtual std::string getBuffer(int64_t &size) const = 0;

    /**
     * @brief Gets the current mode of the File.
     * @return The current mode of the File; CLOSED, READ, WRITE or APPEND.
     **/
    virtual std::string getMode() const = 0;

    /**
     * @brief Gets the filename for this File, or empty string if none.
     * @return The filename for this File.
     **/
    virtual std::string getFilename() const = 0;

    /**
     * @brief Gets the file extension for this File, or empty string if none.
     * @return The file extension for this File (without the dot).
     **/
    virtual std::string getExtension() const;

};  // File

}  // namespace filesystem
}  // namespace eve
