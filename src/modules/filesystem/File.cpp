#include "File.h"
#include <cstring>

namespace eve {
namespace filesystem {

File::~File() {}

FileData *File::read(int64_t size) {
    bool isopen = isOpen();

    if (!isopen && !open("r")) throw eve::Exception("Could not read file %s.", getFilename().c_str());

    int64_t max = getSize();
    int64_t cur = tell();
    size        = (size == ALL) ? max : size;

    if (size < 0) throw eve::Exception("Invalid read size.");
    if (cur + size > max) size = max - cur;

    FileData *fileData  = new FileData(getFilename(), size);
    int64_t   bytesRead = read(fileData->getData(), size);

    if (bytesRead < 0 || (bytesRead == 0 && bytesRead != size)) {
        delete fileData;
        throw eve::Exception("Could not read from file.");
    }

    if (bytesRead < size) {
        FileData *tmpFileData = new FileData(getFilename(), bytesRead);
        memcpy(tmpFileData->getData(), fileData->getData(), (size_t)bytesRead);
        fileData = tmpFileData;
    }

    if (!isopen) close();

    return fileData;
}

bool File::write(const Data *data, int64_t size) {
    return write(data->getData(), (size == ALL) ? data->getSize() : size);
}

std::string File::getExtension() const {
    const std::string &    filename = getFilename();
    std::string::size_type idx      = filename.rfind('.');

    if (idx != std::string::npos)
        return filename.substr(idx + 1);
    else
        return std::string();
}

}  // namespace filesystem
}  // namespace eve
