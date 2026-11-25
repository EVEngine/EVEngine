#include "stylize/PixelFx.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace eve::stylize;
void catalogue(std::ostream& out) {
    out << "{\"schema\":\"eve.pixelfx-native/v1\",\"effects\":[";
    bool first = true;
    for (const auto& i : PixelFx::catalogue()) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":\"" << i.id << "\",\"size\":[" << i.width << ',' << i.height << "],\"origin\":[" << i.originX
            << ',' << i.originY << "],\"duration\":" << std::setprecision(17) << i.duration
            << ",\"loop\":" << (i.loop ? "true" : "false") << '}';
    }
    out << "]}\n";
}
void writePlane(const std::filesystem::path& name, std::span<const uint32_t> plane) {
    std::ofstream file(name, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot create output plane");
    for (auto value : plane)
        for (int shift = 0; shift < 32; shift += 8) file.put(char((value >> shift) & 255u));
    if (!file) throw std::runtime_error("Cannot write output plane");
}
}  // namespace
int main(int argc, char** argv) {
    try {
        std::string             catalogPath, id, output, actorPath, optionsJson, timesJson, layerName = "both";
        int                     frames          = 9;
        bool                    framesSpecified = false;
        std::optional<uint32_t> seed, stone;
        std::optional<double>   height, radius, effectSpeed, spread, reach;
        std::optional<std::vector<uint32_t>> pilot, wood;
        std::optional<std::array<int, 4>>    canvas;
        PixelFxPlacement                     placement;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (i + 1 >= argc) throw std::runtime_error("Expected --catalog FILE or --id ID --out DIR [--frames N]");
            const std::string value = argv[++i];
            if (arg == "--catalog")
                catalogPath = value;
            else if (arg == "--id")
                id = value;
            else if (arg == "--out")
                output = value;
            else if (arg == "--actor")
                actorPath = value;
            else if (arg == "--options")
                optionsJson = value;
            else if (arg == "--layer")
                layerName = value;
            else if (arg == "--x")
                placement.x = std::stod(value);
            else if (arg == "--y")
                placement.y = std::stod(value);
            else if (arg == "--angle")
                placement.angle = std::stod(value);
            else if (arg == "--flip") {
                if (value != "true" && value != "false") throw std::runtime_error("Flip requires true or false");
                placement.flip = value == "true";
            } else if (arg == "--canvas") {
                auto parsed = eve::Value::fromJson(value);
                if (!parsed) throw std::runtime_error(parsed.status().describe());
                const auto* a = parsed.value().getIf<eve::Value::Array>();
                if (!a || a->size() != 4) throw std::runtime_error("Canvas requires [width,height,originX,originY]");
                std::array<int, 4> values{};
                for (size_t j = 0; j < 4; ++j) {
                    if (!(*a)[j].isInt64() || (*a)[j].asInt() < -4096 || (*a)[j].asInt() > 4096)
                        throw std::runtime_error("Canvas entries outside bounded integer range");
                    values[j] = int((*a)[j].asInt());
                }
                if (values[0] < 1 || values[1] < 1) throw std::runtime_error("Canvas dimensions must be positive");
                canvas = values;
            } else if (arg == "--times")
                timesJson = value;
            else if (arg == "--frames") {
                frames          = std::stoi(value);
                framesSpecified = true;
            } else if (arg == "--seed") {
                const auto number = std::stoull(value);
                if (number > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("Seed outside uint32 range");
                seed = uint32_t(number);
            } else if (arg == "--stone") {
                const auto rgb = uint32_t(std::stoul(value, nullptr, 16));
                stone          = 0xff000000u | ((rgb & 255u) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 255u);
            } else if (arg == "--height")
                height = std::stod(value);
            else if (arg == "--radius")
                radius = std::stod(value);
            else if (arg == "--effect-speed")
                effectSpeed = std::stod(value);
            else if (arg == "--spread")
                spread = std::stod(value);
            else if (arg == "--reach")
                reach = std::stod(value);
            else if (arg == "--pilot" || arg == "--wood") {
                auto& ramp = arg == "--pilot" ? pilot : wood;
                ramp.emplace();
                size_t start = 0;
                do {
                    const auto end = value.find(',', start);
                    const auto rgb = uint32_t(std::stoul(value.substr(start, end - start), nullptr, 16));
                    ramp->push_back(0xff000000u | ((rgb & 255u) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 255u));
                    if (end == std::string::npos) break;
                    start = end + 1;
                } while (start < value.size());
            } else
                throw std::runtime_error("Unknown argument");
        }
        if (framesSpecified && !timesJson.empty()) throw std::runtime_error("Use either --times or --frames");
        if (!catalogPath.empty()) {
            if (catalogPath == "-")
                catalogue(std::cout);
            else {
                std::ofstream file(catalogPath);
                if (!file) throw std::runtime_error("Cannot create catalogue");
                catalogue(file);
            }
        }
        if (!id.empty()) {
            if (output.empty() || frames < 2 || frames > 4097)
                throw std::runtime_error("Render requires output and frames in [2,4097]");
            bool found = false;
            for (const auto& info : PixelFx::catalogue())
                if (info.id == id) {
                    found = true;
                    PixelFx fx(info.kind);
                    if (seed || height || radius || effectSpeed || spread || reach || pilot || wood || stone ||
                        !actorPath.empty() || !optionsJson.empty()) {
                        auto  config  = fx.configurationValue();
                        auto* options = config.find("options");
                        if (!optionsJson.empty()) {
                            auto parsed = eve::Value::fromJson(optionsJson);
                            if (!parsed) throw std::runtime_error(parsed.status().describe());
                            const auto* overrides = parsed.value().getIf<eve::Value::Object>();
                            if (!overrides) throw std::runtime_error("Options require an object");
                            for (const auto& [key, value] : *overrides) options->set(key, value);
                        }
                        if (!actorPath.empty()) {
                            std::ifstream file(actorPath);
                            if (!file) throw std::runtime_error("Cannot read actor JSON");
                            const std::string text((std::istreambuf_iterator<char>(file)),
                                                   std::istreambuf_iterator<char>());
                            auto              actor = eve::Value::fromJson(text);
                            if (!actor) throw std::runtime_error(actor.status().describe());
                            options->set("actor", std::move(actor).takeValue());
                        }
                        if (seed) options->set("seed", eve::Value(int64_t(*seed)));
                        if (stone) options->set("stone", eve::Value(int64_t(*stone)));
                        if (height) options->set("height", eve::Value(*height));
                        if (radius) options->set("radius", eve::Value(*radius));
                        if (effectSpeed) options->set("speed", eve::Value(*effectSpeed));
                        if (spread) options->set("spread", eve::Value(*spread));
                        if (reach) options->set("reach", eve::Value(*reach));
                        if (pilot) {
                            eve::Value::Array colors;
                            for (auto color : *pilot) colors.emplace_back(int64_t(color));
                            options->set("pilot", eve::Value::array(std::move(colors)));
                        }
                        if (wood) {
                            eve::Value::Array colors;
                            for (auto color : *wood) colors.emplace_back(int64_t(color));
                            options->set("wood", eve::Value::array(std::move(colors)));
                        }
                        auto prepared = PixelFx::prepareConfigurationValue(info.kind, config);
                        if (!prepared) throw std::runtime_error(prepared.status().describe());
                        auto committed = fx.commitConfiguration(std::move(prepared).takeValue());
                        if (!committed) throw std::runtime_error(committed.status().describe());
                    }
                    std::filesystem::create_directories(output);
                    std::string prefix = id;
                    std::replace(prefix.begin(), prefix.end(), '/', '_');
                    std::vector<double> times;
                    if (!timesJson.empty()) {
                        auto parsed = eve::Value::fromJson(timesJson);
                        if (!parsed) throw std::runtime_error(parsed.status().describe());
                        const auto* rows = parsed.value().getIf<eve::Value::Array>();
                        if (!rows || rows->empty() || rows->size() > 4097)
                            throw std::runtime_error("Expected 1..4097 times");
                        for (const auto& row : *rows) {
                            if (!row.isInt64() && !row.isDouble()) throw std::runtime_error("Times must be numbers");
                            const double time = row.isInt64() ? double(row.asInt()) : row.asDouble();
                            if (!std::isfinite(time) || time < 0 || time > 1e12 ||
                                (!times.empty() && time < times.back()) ||
                                std::abs(std::round(time * 120) / 120 - time) > 1e-8)
                                throw std::runtime_error("Times must be ordered finite nonnegative fixed-tick values");
                            times.push_back(time);
                        }
                    } else
                        for (int i = 0; i < frames; ++i)
                            times.push_back(std::round(double(i) * info.duration * 120 / (frames - 1)) / 120);
                    // Optional evidence alongside raw planes; schema v1 readers ignore unknown fields.
                    std::ofstream visibility(std::filesystem::path(output) / "actor-visibility.json");
                    if (!visibility) throw std::runtime_error("Cannot open actor visibility output");
                    visibility << "{\"schema\":\"eve.pixelfx-actor-visibility/v1\",\"id\":\"" << id
                               << "\",\"frames\":[";
                    for (size_t i = 0; i < times.size(); ++i) {
                        const double t         = times[i];
                        auto         candidate = fx.prepareSeek(t);
                        if (!candidate) throw std::runtime_error(candidate.status().describe());
                        auto published = fx.commitSeek(std::move(candidate).takeValue());
                        if (!published) throw std::runtime_error(published.status().describe());
                        const auto target =
                            canvas.value_or(std::array<int, 4>{info.width, info.height, info.originX, info.originY});
                        std::vector<uint32_t> colors(size_t(target[0]) * size_t(target[1])), lights(colors.size());
                        const auto            layer = layerName == "both"    ? PixelFxLayer::Both
                                                      : layerName == "back"  ? PixelFxLayer::Back
                                                      : layerName == "front" ? PixelFxLayer::Front
                                                                             : static_cast<PixelFxLayer>(-1);
                        auto drawn = fx.drawToCanvas({target[0], target[1], target[2], target[3], colors, lights},
                                                     layer, placement);
                        if (!drawn) throw std::runtime_error(drawn.status().describe());
                        if (i) visibility << ',';
                        visibility << "{\"time\":" << std::setprecision(17) << t << ",\"actorVisible\":"
                                   << (fx.actorVisibility() == PixelFxActorVisibility::Visible ? "true" : "false")
                                   << '}';
                        const auto stem = std::filesystem::path(output) / (prefix + "_" + std::to_string(i));
                        writePlane(stem.string() + "_color.bin", colors);
                        writePlane(stem.string() + "_light.bin", lights);
                    }
                    visibility << "]}\n";
                    visibility.flush();
                    if (!visibility) throw std::runtime_error("Cannot write actor visibility output");
                    std::cout << info.id << ": rendered " << times.size() << " sampled dual-plane frames\n";
                }
            if (!found) throw std::runtime_error("Effect id is not implemented");
        }
        if (catalogPath.empty() && id.empty()) catalogue(std::cout);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
