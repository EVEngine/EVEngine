// Shared test helper translation unit.
//
// `saveImagePng` is declared in RenderImageAudit.h but consumed by more than one
// domain: the graphics image-audit tests (RenderImageAudit.cpp) and the voxel
// render tests (VoxelRenderFixtures.h). A `unit_test_<domain>` executable is its
// own link unit, so the definition has to be compiled into every domain that
// needs it -- an engine export macro cannot help, the symbol is test-side.
//
// This file therefore holds the definition without any TEST_CASE, and
// scripts/test_domains.py lists it under SHARED_SOURCES so the domain targets
// compile it; RenderImageAudit.cpp keeps the graphics-only test cases.

#include "RenderImageAudit.h"

#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <filesystem>
#include <fstream>
#include <string>

using eve::image::ImageData;

bool saveImagePng(const ImageData &img, const std::string &path) {
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
