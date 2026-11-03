#include "devtools/ActionGuideIngestIO.hpp"

#include "common/Diagnostic.h"

#include <Poco/Checksum.h>
#include <Poco/DeflatingStream.h>
#include <Poco/InflatingStream.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace eve::dev {
namespace {

template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

void writeBytes(const std::filesystem::path& path, const char* data, size_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data, static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("failed writing " + path.generic_string());
}

std::vector<uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed reading " + path.generic_string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

uint32_t readBe32(const uint8_t* data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | uint32_t(data[3]);
}

void be32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

void pngChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& bytes) {
    be32(out, static_cast<uint32_t>(bytes.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
    Poco::Checksum checksum(Poco::Checksum::TYPE_CRC32);
    checksum.update(reinterpret_cast<const char*>(out.data() + start), static_cast<unsigned>(out.size() - start));
    be32(out, checksum.checksum());
}

Result<std::vector<uint8_t>> encodeRgbaPng(uint32_t width, uint32_t height,
                                           const std::shared_ptr<const std::vector<uint8_t>>& rgba) {
    if (!rgba || width == 0 || height == 0 || rgba->size() != size_t(width) * height * 4)
        return fail<std::vector<uint8_t>>(DiagnosticCode::InvalidArgument, "invalid RGBA for PNG encode");
    // Dedicated encoder (not frameCapturePreview): preview path forces alpha=255.
    std::vector<uint8_t> scanlines((size_t(width) * 4 + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        const size_t row = size_t(y) * (size_t(width) * 4 + 1);
        scanlines[row]   = 0;  // filter None
        std::memcpy(scanlines.data() + row + 1, rgba->data() + size_t(y) * width * 4, size_t(width) * 4);
    }
    std::ostringstream          compressed;
    Poco::DeflatingOutputStream zlib(compressed, Poco::DeflatingStreamBuf::STREAM_ZLIB);
    zlib.write(reinterpret_cast<const char*>(scanlines.data()), static_cast<std::streamsize>(scanlines.size()));
    zlib.close();
    const auto           data = compressed.str();
    std::vector<uint8_t> png  = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> header;
    be32(header, width);
    be32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    pngChunk(png, "IHDR", header);
    pngChunk(png, "IDAT", std::vector<uint8_t>(data.begin(), data.end()));
    pngChunk(png, "IEND", {});
    return Result<std::vector<uint8_t>>::success(std::move(png));
}

std::string padFolder(size_t index1Based) {
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << index1Based;
    return oss.str();
}

}  // namespace

Result<ActionGuideCandidateFrame> decodeActionGuideCandidatePng(std::span<const std::uint8_t> pngBytes) {
    static const uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (pngBytes.size() < 8 || std::memcmp(pngBytes.data(), kSig, 8) != 0)
        return fail<ActionGuideCandidateFrame>(DiagnosticCode::ParseError, "not a PNG");
    size_t               offset = 8;
    uint32_t             width = 0, height = 0;
    bool                 sawIhdr = false, sawIend = false;
    std::vector<uint8_t> idat;
    while (offset + 12 <= pngBytes.size()) {
        const uint32_t length = readBe32(pngBytes.data() + offset);
        if (offset + 12 + length > pngBytes.size())
            return fail<ActionGuideCandidateFrame>(DiagnosticCode::ParseError, "truncated PNG chunk");
        const char*    type = reinterpret_cast<const char*>(pngBytes.data() + offset + 4);
        const auto*    data = pngBytes.data() + offset + 8;
        Poco::Checksum crc(Poco::Checksum::TYPE_CRC32);
        crc.update(reinterpret_cast<const char*>(pngBytes.data() + offset + 4), static_cast<unsigned>(length + 4));
        const uint32_t expected = readBe32(pngBytes.data() + offset + 8 + length);
        if (crc.checksum() != expected)
            return fail<ActionGuideCandidateFrame>(DiagnosticCode::ParseError, "PNG chunk CRC mismatch");
        if (std::strncmp(type, "IHDR", 4) == 0) {
            if (length != 13 || sawIhdr)
                return fail<ActionGuideCandidateFrame>(DiagnosticCode::ParseError, "invalid IHDR");
            width                = readBe32(data);
            height               = readBe32(data + 4);
            const auto bitDepth  = data[8];
            const auto colorType = data[9];
            const auto interlace = data[12];
            if (bitDepth != 8 || colorType != 6 || interlace != 0 || width == 0 || height == 0 || width > 4096 ||
                height > 4096)
                return fail<ActionGuideCandidateFrame>(DiagnosticCode::Unsupported,
                                                       "only non-interlaced 8-bit RGBA PNG is supported");
            sawIhdr = true;
        } else if (std::strncmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + length);
        } else if (std::strncmp(type, "IEND", 4) == 0) {
            sawIend = true;
            offset += 12 + length;
            break;
        }
        offset += 12 + length;
    }
    if (!sawIhdr || !sawIend || idat.empty())
        return fail<ActionGuideCandidateFrame>(DiagnosticCode::ParseError, "PNG missing IHDR/IDAT/IEND");

    std::string                compressed(reinterpret_cast<const char*>(idat.data()), idat.size());
    std::istringstream         input(compressed);
    Poco::InflatingInputStream zlib(input, Poco::InflatingStreamBuf::STREAM_ZLIB);
    std::ostringstream         inflated;
    inflated << zlib.rdbuf();
    const auto   raw    = inflated.str();
    const size_t stride = size_t(width) * 4 + 1;
    if (raw.size() != stride * height)
        return fail<ActionGuideCandidateFrame>(DiagnosticCode::ParseError, "PNG inflated size mismatch");

    auto rgba = std::make_shared<std::vector<uint8_t>>(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const auto* row    = reinterpret_cast<const uint8_t*>(raw.data() + size_t(y) * stride);
        const auto  filter = row[0];
        if (filter != 0)
            return fail<ActionGuideCandidateFrame>(DiagnosticCode::Unsupported,
                                                   "PNG filter methods other than None are unsupported");
        std::memcpy(rgba->data() + size_t(y) * width * 4, row + 1, size_t(width) * 4);
    }
    ActionGuideCandidateFrame frame;
    frame.width  = width;
    frame.height = height;
    frame.rgba8  = std::move(rgba);
    return Result<ActionGuideCandidateFrame>::success(std::move(frame));
}

Result<std::vector<ActionGuideCandidateFrame>> loadActionGuideCandidates(const std::filesystem::path& candidateDir,
                                                                         const ActionGuidePack&       pack) {
    if (pack.shots.empty())
        return fail<std::vector<ActionGuideCandidateFrame>>(DiagnosticCode::InvalidArgument, "pack has no shots");
    if (!std::filesystem::is_directory(candidateDir))
        return fail<std::vector<ActionGuideCandidateFrame>>(DiagnosticCode::NotFound, "candidateDir is missing",
                                                            candidateDir.generic_string());
    std::vector<ActionGuideCandidateFrame> frames;
    frames.reserve(pack.shots.size());
    try {
        for (size_t i = 0; i < pack.shots.size(); ++i) {
            const auto folder   = candidateDir / padFolder(i + 1);
            const auto metaPath = folder / "meta.json";
            const auto pngPath  = folder / "frame.png";
            if (!std::filesystem::is_regular_file(metaPath) || !std::filesystem::is_regular_file(pngPath))
                return fail<std::vector<ActionGuideCandidateFrame>>(
                    DiagnosticCode::NotFound, "missing meta.json or frame.png", folder.generic_string());
            const auto metaBytes = readAllBytes(metaPath);
            auto       metaJson =
                Value::fromJson(std::string(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size()));
            if (!metaJson) return Result<std::vector<ActionGuideCandidateFrame>>::failure(metaJson.status());
            auto meta = decodeActionGuideCandidateMeta(metaJson.value());
            if (!meta) return Result<std::vector<ActionGuideCandidateFrame>>::failure(meta.status());
            const auto pngBytes = readAllBytes(pngPath);
            auto       frame    = decodeActionGuideCandidatePng(pngBytes);
            if (!frame) return Result<std::vector<ActionGuideCandidateFrame>>::failure(frame.status());
            frame.value().meta = std::move(meta).takeValue();
            frames.push_back(std::move(frame).takeValue());
        }
    } catch (const std::exception& e) {
        return fail<std::vector<ActionGuideCandidateFrame>>(DiagnosticCode::Failed,
                                                            std::string("candidate load failed: ") + e.what());
    }
    return Result<std::vector<ActionGuideCandidateFrame>>::success(std::move(frames));
}

Result<Value> writeActionGuideIngestPack(const ActionGuideIngestDraft&              draft,
                                         std::span<const ActionGuideCandidateFrame> frames,
                                         const std::filesystem::path&               outputRoot) {
    if (frames.empty() || static_cast<std::int64_t>(frames.size()) != draft.frameCount)
        return fail<Value>(DiagnosticCode::InvalidArgument, "draft frameCount does not match frames");
    const auto staging = outputRoot / ".staging";
    try {
        std::filesystem::remove_all(staging);
        std::filesystem::create_directories(staging / "frames");
        for (size_t i = 0; i < frames.size(); ++i) {
            auto png = encodeRgbaPng(frames[i].width, frames[i].height, frames[i].rgba8);
            if (!png) {
                std::filesystem::remove_all(staging);
                return Result<Value>::failure(png.status());
            }
            writeBytes(staging / ("frames/frame_" + std::to_string(i) + ".png"),
                       reinterpret_cast<const char*>(png.value().data()), png.value().size());
        }
        auto writeJson = [&](const char* name, const Value& value) -> Result<void> {
            auto json = value.toJson();
            if (!json) return Result<void>::failure(json.status());
            writeBytes(staging / name, json.value().data(), json.value().size());
            return Result<void>::success();
        };
        if (auto r = writeJson("clip.json", draft.clipDescriptor); !r) {
            std::filesystem::remove_all(staging);
            return Result<Value>::failure(r.status());
        }
        if (auto r = writeJson("ability.draft.json", draft.abilityDraft); !r) {
            std::filesystem::remove_all(staging);
            return Result<Value>::failure(r.status());
        }
        if (auto r = writeJson("attribution.gen.json", draft.attribution); !r) {
            std::filesystem::remove_all(staging);
            return Result<Value>::failure(r.status());
        }
        Value manifest =
            Value::object({{"schemaId", Value("eve.action.guide-ingest.artifact")},
                           {"schemaVersion", Value(static_cast<std::int64_t>(1))},
                           {"sourceGuideRevision", Value(static_cast<std::int64_t>(draft.sourceGuideRevision))},
                           {"clipName", Value(draft.clipName)},
                           {"frameCount", Value(draft.frameCount)},
                           {"sequencePattern", Value(draft.sequencePattern)},
                           {"sequenceFirst", Value(draft.sequenceFirst)},
                           {"sequenceLast", Value(draft.sequenceLast)},
                           {"clipPath", Value("clip.json")},
                           {"abilityDraftPath", Value("ability.draft.json")},
                           {"attributionPath", Value("attribution.gen.json")}});
        if (auto r = writeJson("ingest.manifest.json", manifest); !r) {
            std::filesystem::remove_all(staging);
            return Result<Value>::failure(r.status());
        }

        std::filesystem::create_directories(outputRoot);
        // Replace published content atomically by swapping staging into place via temp rename.
        const auto publishedTmp = outputRoot / (".published-" + std::to_string(draft.sourceGuideRevision));
        std::filesystem::remove_all(publishedTmp);
        std::filesystem::rename(staging, publishedTmp);
        for (const auto& entry : std::filesystem::directory_iterator(publishedTmp)) {
            const auto name = entry.path().filename();
            if (name == "." || name == "..") continue;
            const auto dest = outputRoot / name;
            std::filesystem::remove_all(dest);
            std::filesystem::rename(entry.path(), dest);
        }
        std::filesystem::remove_all(publishedTmp);

        return Result<Value>::success(
            Value::object({{"exportRoot", Value(outputRoot.generic_string())},
                           {"manifest", Value("ingest.manifest.json")},
                           {"clipPath", Value("clip.json")},
                           {"abilityDraftPath", Value("ability.draft.json")},
                           {"frameCount", Value(draft.frameCount)},
                           {"clipName", Value(draft.clipName)},
                           {"sourceGuideRevision", Value(static_cast<std::int64_t>(draft.sourceGuideRevision))}}));
    } catch (const std::exception& e) {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        return fail<Value>(DiagnosticCode::Failed, std::string("ingest write failed: ") + e.what());
    }
}

}  // namespace eve::dev
