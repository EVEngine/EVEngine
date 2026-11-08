#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include "stylize/pixelfx/Rig.h"
using namespace eve;
using namespace eve::stylize::pixelfx::rig;
namespace {
template <class T>
Value array(const T& values) {
    Value::Array out;
    out.reserve(values.size());
    for (auto v : values) {
        if constexpr (std::is_floating_point_v<decltype(v)>)
            out.emplace_back(double(v));
        else
            out.emplace_back(int64_t(v));
    }
    return Value::array(std::move(out));
}
Value evaluate(const Pose& pose, Rasterizer& raster) {
    auto geometry = skeleton(pose);
    if (!geometry) throw std::runtime_error(geometry.status().describe());
    auto solved = solve(pose);
    if (!solved) throw std::runtime_error(solved.status().describe());
    auto rendered = raster.render(pose);
    if (!rendered) throw std::runtime_error(rendered.status().describe());
    const auto&   p = rendered.value();
    Value::Object joints;
    size_t        i = 0;
    for (auto name : jointNames()) joints.emplace(std::string(name), array(geometry.value().joints[i++]));
    auto separated = raster.renderParts(pose);
    if (!separated) throw std::runtime_error(separated.status().describe());
    Value::Array parts;
    for (const auto& part : separated.value())
        parts.push_back(Value::object({{"name", Value(std::string(part.name))},
                                       {"part", Value(int64_t(part.part))},
                                       {"xs", array(part.pixels.xs)},
                                       {"ys", array(part.pixels.ys)},
                                       {"color", array(part.pixels.color)},
                                       {"cx", Value(part.centerX)},
                                       {"cy", Value(part.centerY)},
                                       {"z", Value(part.pixels.z)}}));
    return Value::object({{"schema", Value("eve.pixelfx.rig-probe/v1")},
                          {"parts", Value::array(std::move(parts))},
                          {"joints", Value::object(std::move(joints))},
                          {"shortfall", Value(shortfall(solved.value()))},
                          {"xs", array(p.xs)},
                          {"ys", array(p.ys)},
                          {"color", array(p.color)},
                          {"src", array(p.src)},
                          {"ink", array(p.ink)},
                          {"bounds", Value::array({Value(int64_t(p.x0)), Value(int64_t(p.y0)), Value(int64_t(p.x1)),
                                                   Value(int64_t(p.y1))})},
                          {"z", Value(p.z)}});
}
Value trackFrames(const Value& document, const Value& times) {
    auto prepared = PoseTrack::prepare(document);
    if (!prepared) throw std::runtime_error(prepared.status().describe());
    const auto* values = times.getIf<Value::Array>();
    if (!values) throw std::runtime_error("Expected sample time array");
    Value::Array frames;
    for (const auto& time : *values) {
        if (!time.isInt64() && !time.isDouble()) throw std::runtime_error("Expected numeric time");
        const double t        = time.isDouble() ? time.asDouble() : double(time.asInt());
        auto         authored = prepared.value().sampleAuthored(t);
        if (!authored) throw std::runtime_error(authored.status().describe());
        const auto& pose     = authored.value().pose;
        auto        geometry = skeleton(pose);
        if (!geometry) throw std::runtime_error(geometry.status().describe());
        auto solved = solve(pose);
        if (!solved) throw std::runtime_error(solved.status().describe());
        Value::Object joints;
        size_t        i = 0;
        for (auto name : jointNames()) joints.emplace(std::string(name), array(geometry.value().joints[i++]));
        const auto   encodedPose = encodePose(pose);
        Value::Array presence;
        const auto   names = poseFieldNames();
        for (size_t f = 0; f < names.size(); ++f)
            if (authored.value().contains(PoseField(f))) presence.emplace_back(std::string(names[f]));
        frames.push_back(Value::object({{"time", Value(t)},
                                        {"pose", *encodedPose.find("fields")},
                                        {"presence", Value::array(std::move(presence))},
                                        {"joints", Value::object(std::move(joints))},
                                        {"shortfall", Value(shortfall(solved.value()))}}));
    }
    return Value::object(
        {{"schema", Value("eve.pixelfx.track-probe/v1")}, {"frames", Value::array(std::move(frames))}});
}
Value readDocument(const char* path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read input document");
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto              value = Value::fromJson(source);
    if (!value) throw std::runtime_error(value.status().describe());
    return std::move(value).takeValue();
}
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 7 && std::string_view(argv[1]) == "--track" && std::string_view(argv[3]) == "--times" &&
            std::string_view(argv[5]) == "--out") {
            auto value = trackFrames(readDocument(argv[2]), readDocument(argv[4]));
            auto json  = value.toJson();
            if (!json) throw std::runtime_error(json.status().describe());
            std::ofstream output(argv[6], std::ios::binary);
            output << json.value() << '\n';
            if (!output) throw std::runtime_error("Cannot write track snapshot");
            std::cout << "Native pose track frames: " << value.find("frames")->arraySize() << '\n';
            return 0;
        }
        if (argc != 5 || std::string_view(argv[1]) != "--pose" || std::string_view(argv[3]) != "--out")
            throw std::runtime_error("Expected --pose FILE --out FILE");
        std::ifstream input(argv[2]);
        if (!input) throw std::runtime_error("Cannot read pose document");
        const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        auto              document = Value::fromJson(source);
        if (!document) throw std::runtime_error(document.status().describe());
        auto pose = decodePose(document.value());
        if (!pose) throw std::runtime_error(pose.status().describe());
        Rasterizer raster;
        auto       value = evaluate(pose.value(), raster);
        auto       json  = value.toJson();
        if (!json) throw std::runtime_error(json.status().describe());
        std::ofstream output(argv[4], std::ios::binary);
        output << json.value() << '\n';
        if (!output) throw std::runtime_error("Cannot write rig snapshot");
        std::cout << "Native rig: " << value.find("color")->arraySize() << " sparse pixels\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
