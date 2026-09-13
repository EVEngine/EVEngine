#include "RenderImageAudit.h"

#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <filesystem>
#include <fstream>

bool saveImagePng(const eve::image::ImageData &img, const std::string &path) {
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::filesystem::FileData *png =
        img.encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false);
    if (!png) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    const bool ok = out.good();
    if (ok) {
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
    }
    delete png;
    return ok && out.good();
}
