#include "common/Capability.h"
#include "common/PixelWorldAutomation.h"
#include "pixelworld/PixelWorldControl.h"
#include "pixelworld/PixelMaterialCatalogCodec.h"

#if defined(__EMSCRIPTEN__)

namespace eve::pixelworld {
namespace {

class PixelWorldAutomation final : public eve::IPixelWorldAutomation {
public:
    std::string invoke(const std::string&, const std::string&) override {
        return R"({"ok":false,"status":"Unsupported","error":"PixelWorld MCP automation is unavailable on the Web build"})";
    }
};

PixelWorldAutomation automation;

}  // namespace

void registerPixelWorldAutomation() { eve::cap::provide<eve::IPixelWorldAutomation>(&automation); }
void unregisterPixelWorldAutomation() { eve::cap::revoke<eve::IPixelWorldAutomation>(&automation); }

}  // namespace eve::pixelworld

#else

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace eve::pixelworld {
namespace {

using Object = Poco::JSON::Object;

std::string stringify(Object::Ptr object) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(object), out);
    return out.str();
}

std::string stringify(const Poco::Dynamic::Var& value) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(value, out);
    return out.str();
}

Object::Ptr response(bool ok) {
    Object::Ptr out(new Object);
    out->set("ok", ok);
    return out;
}

template <class T>
std::string failure(const eve::Result<T>& result) {
    Object::Ptr out = response(false);
    out->set("status", std::string(eve::statusCodeName(result.status().code())));
    if (result.status().hasDiagnostics())
        out->set("error", result.status().diagnostics().front().message());
    return stringify(out);
}

Object::Ptr parse(const std::string& json) {
    try {
        return Poco::JSON::Parser().parse(json).extract<Object::Ptr>();
    } catch (...) {
        return Object::Ptr(new Object);
    }
}

std::string hex(std::span<const std::byte> bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const std::byte value : bytes) out << std::setw(2) << std::to_integer<unsigned>(value);
    return out.str();
}

eve::Result<std::vector<std::byte>> unhex(const std::string& value) {
    if ((value.size() & 1U) != 0)
        return eve::Result<std::vector<std::byte>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot hex length must be even", "snapshot", {},
            "pixelworld.automation"));
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        unsigned parsed = 0;
        std::istringstream in(value.substr(index, 2));
        in >> std::hex >> parsed;
        if (!in || !in.eof())
            return eve::Result<std::vector<std::byte>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "snapshot contains non-hex data", "snapshot", {},
                "pixelworld.automation"));
        bytes.push_back(std::byte(parsed));
    }
    return eve::Result<std::vector<std::byte>>::success(std::move(bytes));
}

class PixelWorldAutomation final : public eve::IPixelWorldAutomation {
public:
    std::string invoke(const std::string& operation, const std::string& requestJson) override {
        auto args = parse(requestJson);
        auto& control = pixelWorldControlService();
        if (operation == "worlds") {
            Poco::JSON::Array::Ptr items(new Poco::JSON::Array);
            for (const auto& world : control.worlds()) {
                Object::Ptr item(new Object);
                item->set("world", world.link.world);
                item->set("epoch", world.link.epoch);
                item->set("seed", world.seed);
                item->set("revision", world.revision);
                item->set("tick", world.tick);
                item->set("lastEditSequence", world.lastEditSequence);
                item->set("chunks", world.chunks);
                item->set("activeChunks", world.activeChunks);
                item->set("paused", world.paused);
                items->add(item);
            }
            auto out = response(true);
            out->set("worlds", items);
            return stringify(out);
        }
        if (operation == "catalog_builtin") {
            const MaterialCatalog catalog = MaterialCatalog::builtIn();
            auto encoded = encodeMaterialCatalogJson(catalog);
            if (!encoded.ok()) return failure(encoded);
            auto out = response(true);
            out->set("fingerprint", std::to_string(catalog.fingerprint()));
            out->set("catalog", encoded.value());
            return stringify(out);
        }
        if (operation == "catalog_validate") {
            std::string document;
            if (args->has("catalog")) {
                const auto value = args->get("catalog");
                document = value.isString() ? value.convert<std::string>() : stringify(value);
            }
            auto decoded = decodeMaterialCatalogJson(document);
            if (!decoded.ok()) return failure(decoded);
            auto canonical = encodeMaterialCatalogJson(decoded.value());
            if (!canonical.ok()) return failure(canonical);
            auto out = response(true);
            out->set("fingerprint", std::to_string(decoded.value().fingerprint()));
            out->set("materialCount", decoded.value().definitions().size());
            out->set("reactionCount", decoded.value().reactions().size());
            out->set("phaseRuleCount", decoded.value().phaseRules().size());
            out->set("canonicalCatalog", canonical.value());
            return stringify(out);
        }
        if (operation == "catalog_apply") {
            std::string document;
            if (args->has("catalog")) {
                const auto value = args->get("catalog");
                document = value.isString() ? value.convert<std::string>() : stringify(value);
            }
            std::uint64_t expected = 0;
            try {
                expected = std::stoull(args->optValue<std::string>("expectedFingerprint", "0"));
            } catch (...) {
                auto out = response(false);
                out->set("error", "expectedFingerprint must be an unsigned decimal string");
                return stringify(out);
            }
            auto result = control.reloadMaterialCatalog(
                args->optValue<std::uint64_t>("world", 0), document, expected);
            if (!result.ok()) return failure(result);
            auto out = response(true);
            out->set("fingerprintBefore", std::to_string(result.value().fingerprintBefore));
            out->set("fingerprintAfter", std::to_string(result.value().fingerprintAfter));
            out->set("revisionAfter", result.value().revisionAfter);
            out->set("worldEpoch", result.value().worldEpoch);
            out->set("chunksRebuilt", result.value().chunksRebuilt);
            out->set("replayHistoryInvalidated", result.value().replayHistoryInvalidated);
            return stringify(out);
        }
        const auto worldId = args->optValue<std::uint64_t>("world", 0);
        if (operation == "pause") {
            auto result = control.setPaused(worldId, args->optValue<bool>("paused", true));
            if (!result.ok()) return failure(result);
            auto out = response(true);
            out->set("paused", control.world(worldId).value().paused);
            return stringify(out);
        }
        if (operation == "step") {
            auto result = control.step(worldId, args->optValue<std::uint32_t>("count", 1));
            if (!result.ok()) return failure(result);
            const auto& receipt = result.value();
            auto out = response(true);
            out->set("steps", receipt.steps);
            out->set("firstTick", receipt.firstTick);
            out->set("lastTick", receipt.lastTick);
            out->set("elapsedMicroseconds", receipt.elapsedMicroseconds);
            out->set("cellsChanged", receipt.finalStep.cellsChanged);
            out->set("cellsMoved", receipt.finalStep.cellsMoved);
            return stringify(out);
        }
        if (operation == "edit") {
            PixelEditCommand command;
            command.sequence = args->optValue<std::uint64_t>("sequence", 0);
            const std::string kind = args->optValue<std::string>("kind", "paint");
            command.kind = kind == "heat" ? PixelEditKind::HeatCircle
                           : kind == "explosion" ? PixelEditKind::Explosion
                                                  : PixelEditKind::PaintCircle;
            command.centerX = args->optValue<int>("x", 0);
            command.centerY = args->optValue<int>("y", 0);
            command.radius = args->optValue<int>("radius", 0);
            command.material = MaterialId(args->optValue<unsigned>("material", 0));
            command.strength = args->optValue<int>("strength", 0);
            command.temperatureDelta = std::int16_t(std::clamp(
                args->optValue<int>("temperatureDelta", 0), -32768, 32767));
            auto result = control.applyEdit(worldId, command);
            if (!result.ok()) return failure(result);
            auto out = response(true);
            out->set("revisionBefore", result.value().revisionBefore);
            out->set("revisionAfter", result.value().revisionAfter);
            out->set("cellsChanged", result.value().cellsChanged);
            return stringify(out);
        }
        if (operation == "diagnostics") {
            PixelChunkRegion region{args->optValue<int>("minX", 0), args->optValue<int>("minY", 0),
                                    args->optValue<int>("maxX", 0), args->optValue<int>("maxY", 0)};
            auto result = control.chunkDiagnostics(worldId, region);
            if (!result.ok()) return failure(result);
            Poco::JSON::Array::Ptr chunks(new Poco::JSON::Array);
            for (const auto& diagnostic : result.value()) {
                Object::Ptr item(new Object);
                item->set("x", diagnostic.x);
                item->set("y", diagnostic.y);
                item->set("revision", diagnostic.revision);
                item->set("nonAirCells", diagnostic.nonAirCells);
                item->set("mobileCells", diagnostic.mobileCells);
                item->set("minimumTemperature", diagnostic.minimumTemperature);
                item->set("maximumTemperature", diagnostic.maximumTemperature);
                item->set("idleTicks", diagnostic.idleTicks);
                item->set("active", diagnostic.active);
                chunks->add(item);
            }
            auto out = response(true);
            out->set("chunks", chunks);
            return stringify(out);
        }
        if (operation == "samples") {
            auto result = control.performanceSamples(
                worldId, args->optValue<std::uint32_t>("limit", 120));
            if (!result.ok()) return failure(result);
            Poco::JSON::Array::Ptr samples(new Poco::JSON::Array);
            for (const auto& sample : result.value()) {
                Object::Ptr item(new Object);
                item->set("tick", sample.tick);
                item->set("elapsedMicroseconds", sample.elapsedMicroseconds);
                item->set("chunksVisited", sample.stats.chunksVisited);
                item->set("cellsVisited", sample.stats.cellsVisited);
                item->set("cellsChanged", sample.stats.cellsChanged);
                item->set("cellsMoved", sample.stats.cellsMoved);
                item->set("parallelTasks", sample.stats.parallelTasks);
                samples->add(item);
            }
            auto out = response(true);
            out->set("samples", samples);
            return stringify(out);
        }
        if (operation == "snapshot_capture") {
            auto result = control.captureSnapshot(worldId);
            if (!result.ok()) return failure(result);
            auto out = response(true);
            out->set("encoding", "hex");
            out->set("snapshot", hex(result.value()));
            return stringify(out);
        }
        if (operation == "snapshot_restore") {
            auto decoded = unhex(args->optValue<std::string>("snapshot", ""));
            if (!decoded.ok()) return failure(decoded);
            auto result = control.restoreSnapshot(worldId, decoded.value());
            if (!result.ok()) return failure(result);
            return stringify(response(true));
        }
        auto out = response(false);
        out->set("error", "unknown PixelWorld automation operation");
        return stringify(out);
    }
};

PixelWorldAutomation automation;

}  // namespace

void registerPixelWorldAutomation() { eve::cap::provide<eve::IPixelWorldAutomation>(&automation); }
void unregisterPixelWorldAutomation() { eve::cap::revoke<eve::IPixelWorldAutomation>(&automation); }

}  // namespace eve::pixelworld

#endif  // defined(__EMSCRIPTEN__)
