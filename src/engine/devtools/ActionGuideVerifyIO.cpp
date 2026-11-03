#include "devtools/ActionGuideVerifyIO.hpp"

#include "common/Diagnostic.h"
#include "devtools/ActionGuideIngestIO.hpp"

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

std::string padFolder(size_t index1Based) {
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << index1Based;
    return oss.str();
}

std::vector<uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed reading " + path.generic_string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void writeBytes(const std::filesystem::path& path, const char* data, size_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data, static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("failed writing " + path.generic_string());
}

}  // namespace

Result<ActionGuideVerifyRequest> loadActionGuideVerifyRequest(const ActionGuidePack&       pack,
                                                              const std::filesystem::path& guideArtifactDir,
                                                              const std::filesystem::path& candidateDir) {
    if (pack.shots.empty()) return fail<ActionGuideVerifyRequest>(DiagnosticCode::InvalidArgument, "pack has no shots");
    if (!std::filesystem::is_directory(guideArtifactDir))
        return fail<ActionGuideVerifyRequest>(DiagnosticCode::NotFound, "guideArtifactDir is missing",
                                              guideArtifactDir.generic_string());
    if (!std::filesystem::is_directory(candidateDir))
        return fail<ActionGuideVerifyRequest>(DiagnosticCode::NotFound, "candidateDir is missing",
                                              candidateDir.generic_string());

    ActionGuideVerifyRequest request;
    request.pack = pack;
    request.shots.reserve(pack.shots.size());
    try {
        for (size_t i = 0; i < pack.shots.size(); ++i) {
            const auto folder     = padFolder(i + 1);
            const auto guidePng   = guideArtifactDir / "shots" / folder / "silhouette.png";
            const auto candidateP = candidateDir / folder / "frame.png";
            if (!std::filesystem::is_regular_file(guidePng))
                return fail<ActionGuideVerifyRequest>(DiagnosticCode::NotFound, "missing guide silhouette.png",
                                                      guidePng.generic_string());
            if (!std::filesystem::is_regular_file(candidateP))
                return fail<ActionGuideVerifyRequest>(DiagnosticCode::NotFound, "missing candidate frame.png",
                                                      candidateP.generic_string());

            const auto guideBytes = readAllBytes(guidePng);
            auto       guideFrame = decodeActionGuideCandidatePng(guideBytes);
            if (!guideFrame) return Result<ActionGuideVerifyRequest>::failure(guideFrame.status());
            const auto candBytes = readAllBytes(candidateP);
            auto       candFrame = decodeActionGuideCandidatePng(candBytes);
            if (!candFrame) return Result<ActionGuideVerifyRequest>::failure(candFrame.status());

            ActionGuideVerifyShotInput shot;
            shot.shotId               = pack.shots[i].shotId;
            shot.width                = guideFrame.value().width;
            shot.height               = guideFrame.value().height;
            shot.guideSilhouetteRgba8 = guideFrame.value().rgba8;
            shot.candidateRgba8       = candFrame.value().rgba8;
            if (shot.width != candFrame.value().width || shot.height != candFrame.value().height)
                return fail<ActionGuideVerifyRequest>(DiagnosticCode::InvalidArgument,
                                                      "guide and candidate sizes differ", folder);
            request.shots.push_back(std::move(shot));
        }
    } catch (const std::exception& e) {
        return fail<ActionGuideVerifyRequest>(DiagnosticCode::Failed, std::string("verify load failed: ") + e.what());
    }
    return Result<ActionGuideVerifyRequest>::success(std::move(request));
}

Result<Value> writeActionGuideVerifyEvidence(const ActionGuideVerifyReport& report,
                                             const std::filesystem::path&   outputRoot) {
    try {
        std::filesystem::create_directories(outputRoot);
        Result<std::string> json = [&]() -> Result<std::string> {
            if (!report.evidence.isNull()) {
                auto encodedEvidence = report.evidence.toJson();
                if (encodedEvidence) return encodedEvidence;
                encodedEvidence.ignore();
            }
            auto encoded = encodeActionGuideVerifyReport(report);
            if (!encoded) return Result<std::string>::failure(encoded.status());
            return encoded.value().toJson();
        }();
        if (!json) return Result<Value>::failure(json.status());
        writeBytes(outputRoot / "verify.evidence.json", json.value().data(), json.value().size());
        return Result<Value>::success(
            Value::object({{"evidencePath", Value((outputRoot / "verify.evidence.json").generic_string())},
                           {"passed", Value(report.passed)},
                           {"sourceGuideRevision", Value(static_cast<std::int64_t>(report.sourceGuideRevision))}}));
    } catch (const std::exception& e) {
        return fail<Value>(DiagnosticCode::Failed, std::string("verify evidence write failed: ") + e.what());
    }
}

}  // namespace eve::dev
