#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include "stylize/pixelfx/ActRuntime.h"
#include "stylize/pixelfx/Descriptor.h"
using namespace eve;
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
namespace {
Value read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Missing input");
    std::string s((std::istreambuf_iterator<char>(f)), {});
    auto        v = Value::fromJson(s);
    if (!v) throw std::runtime_error(v.status().describe());
    return std::move(v).takeValue();
}
void write(const std::filesystem::path& path, const Value& value) {
    auto s = value.toJson();
    if (!s) throw std::runtime_error(s.status().describe());
    std::ofstream f(path, std::ios::binary);
    f << s.value() << '\n';
    if (!f) throw std::runtime_error("Cannot write report");
}
double numeric(const Value& value) {
    if (value.isDouble()) return value.asDouble();
    if (value.isInt64()) return double(value.asInt());
    throw std::runtime_error("Expected number");
}
Value timingValue(std::optional<ActTimingView> timing) {
    if (!timing) return Value();
    Value::Array                         targets, hits, poses;
    constexpr std::array<const char*, 9> skins{"", "flash", "char", "ice", "shadow", "holy", "red", "shock", "poison"};
    for (double x : timing->targets) targets.emplace_back(x);
    for (const auto& hit : timing->hits) {
        Value::Object fields{{"t", Value(hit.time)},
                             {"freeze", Value(hit.freeze)},
                             {"shake", Value(hit.shake)},
                             {"amp", Value(hit.amp)},
                             {"push", Value(hit.push)}};
        if (hit.skin)
            fields.emplace("skin", *hit.skin == ActTargetSkin::None ? Value() : Value(skins[size_t(*hit.skin)]));
        if (hit.at) fields.emplace("at", Value(*hit.at));
        hits.push_back(Value::object(std::move(fields)));
    }
    const auto names = rig::poseFieldNames();
    for (const auto& key : timing->poses) {
        auto          encoded = rig::encodePose(key.pose.pose);
        Value::Object fields;
        for (size_t i = 0; i < names.size(); ++i)
            if (key.pose.contains(rig::PoseField(i)))
                fields.emplace(std::string(names[i]), *encoded.find("fields")->find(std::string(names[i])));
        poses.push_back(Value::object(
            {{"t", Value(key.time)}, {"how", Value(key.how)}, {"pose", Value::object(std::move(fields))}}));
    }
    return Value::object({{"windup", Value(timing->windup)},
                          {"recovery", Value(timing->recovery)},
                          {"onBody", Value(timing->onBody)},
                          {"targets", Value::array(std::move(targets))},
                          {"hits", Value::array(std::move(hits))},
                          {"poses", Value::array(std::move(poses))}});
}
void plane(const std::filesystem::path& path, std::span<const uint32_t> pixels) {
    std::ofstream f(path, std::ios::binary);
    for (auto c : pixels)
        for (int s : {0, 8, 16, 24}) f.put(char(c >> s));
    if (!f) throw std::runtime_error("Cannot write plane");
}
}  // namespace
int main(int argc, char** argv) {
    try {
        std::string                       id, out, input, warmId, actorPath;
        std::optional<int>                warmSteps;
        std::optional<uint32_t>           seed;
        PixelFxPlacement                  placement;
        bool                              showTargets = false, showBody = false, contract = false, inputBody = false;
        std::array<std::optional<int>, 4> geometry;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 >= argc) throw std::runtime_error("Expected flag value");
            const std::string key = argv[i], value = argv[i + 1];
            if (key == "--id")
                id = value;
            else if (key == "--out")
                out = value;
            else if (key == "--input")
                input = value;
            else if (key == "--actor")
                actorPath = value;
            else if (key == "--warm-id")
                warmId = value;
            else if (key == "--warm-steps") {
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Expected bounded warm steps");
                const auto n = std::stoull(value);
                if (n > 4096) throw std::runtime_error("Warm steps outside bounds");
                warmSteps = int(n);
            } else if (key == "--seed") {
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Seed must be a uint32 integer");
                const auto parsed = std::stoull(value);
                if (parsed > UINT32_MAX) throw std::runtime_error("Seed must be a uint32 integer");
                seed = uint32_t(parsed);
            } else if (key == "--targets" || key == "--body" || key == "--contract" || key == "--input-body" ||
                       key == "--flip") {
                if (value != "0" && value != "1") throw std::runtime_error("Expected boolean 0 or 1");
                if (key == "--targets")
                    showTargets = value == "1";
                else if (key == "--body")
                    showBody = value == "1";
                else if (key == "--input-body")
                    inputBody = value == "1";
                else if (key == "--contract")
                    contract = value == "1";
                else
                    placement.flip = value == "1";
            } else if (key == "--x" || key == "--y") {
                if (value.empty() || value.find_first_not_of("0123456789.+-eE") != std::string::npos)
                    throw std::runtime_error("Expected decimal placement");
                size_t       used   = 0;
                const double parsed = std::stod(value, &used);
                if (used != value.size() || !std::isfinite(parsed) || std::abs(parsed) > 4096)
                    throw std::runtime_error("Placement outside bounds");
                if (key == "--x")
                    placement.x = parsed;
                else
                    placement.y = parsed;
            } else if (key == "--width" || key == "--height" || key == "--origin-x" || key == "--origin-y") {
                if (value.empty() || value.find_first_not_of("0123456789+-") != std::string::npos)
                    throw std::runtime_error("Expected integer canvas geometry");
                size_t     used      = 0;
                const auto parsed    = std::stoll(value, &used);
                const bool dimension = key == "--width" || key == "--height";
                if (used != value.size() || parsed < (dimension ? 1 : -4096) || parsed > 4096)
                    throw std::runtime_error("Canvas outside bounds");
                const size_t index = key == "--width" ? 0 : key == "--height" ? 1 : key == "--origin-x" ? 2 : 3;
                geometry[index]    = int(parsed);
            } else
                throw std::runtime_error("Unknown flag");
        }
        const PixelFxInfo* info = nullptr;
        for (const auto& fx : PixelFx::catalogue())
            if (fx.id == id) info = &fx;
        if (!info || out.empty() || input.empty()) throw std::runtime_error("Expected --id ID --out DIR --input FILE");
        auto        protocol = read(input);
        const auto* schema   = protocol.find("schema");
        const auto* version  = protocol.find("version");
        if (!schema || !schema->isString() || schema->asString() != "eve.pixelfx.act-probe-input" || !version ||
            !version->isInt64() || version->asInt() != 1)
            throw std::runtime_error("Bad input schema/version");
        for (const auto& [name, value] : *protocol.getIf<Value::Object>())
            if (name != "schema" && name != "version" && name != "steps" && name != "frames" && name != "stops" &&
                name != "initialColor" && name != "initialLight")
                throw std::runtime_error("Unknown input field");
        const auto* stepValue = protocol.find("steps");
        const auto* steps     = stepValue ? stepValue->getIf<Value::Array>() : nullptr;
        if (!steps || steps->size() > 4096) throw std::runtime_error("Invalid steps");
        for (const auto& value : *steps) {
            const double delta = numeric(value);
            if (!std::isfinite(delta) || delta < 0 || delta > 1) throw std::runtime_error("Invalid delta");
        }
        uint32_t initialColor = 0, initialLight = 0;
        for (auto pair : {std::pair{"initialColor", &initialColor}, std::pair{"initialLight", &initialLight}}) {
            if (const auto* value = protocol.find(pair.first)) {
                if (!value->isInt64() || value->asInt() < 0 || uint64_t(value->asInt()) > 4294967295ull)
                    throw std::runtime_error("Invalid initial plane color");
                *pair.second = uint32_t(value->asInt());
            }
        }
        std::set<size_t> requested, stops;
        for (auto pair : {std::pair{"frames", &requested}, std::pair{"stops", &stops}}) {
            const auto* value   = protocol.find(pair.first);
            const auto* indices = value ? value->getIf<Value::Array>() : nullptr;
            if (!indices || indices->size() > 4097) throw std::runtime_error("Invalid sample indices");
            for (const auto& index : *indices) {
                if (!index.isInt64() || index.asInt() < 0 || uint64_t(index.asInt()) > steps->size())
                    throw std::runtime_error("Sample index outside step sequence");
                pair.second->insert(size_t(index.asInt()));
            }
        }
        auto view = *info;
        if (geometry[0]) view.width = *geometry[0];
        if (geometry[1]) view.height = *geometry[1];
        if (geometry[2]) view.originX = *geometry[2];
        if (geometry[3]) view.originY = *geometry[3];
        auto settings = PixelFx::defaults(info->kind);
        if (seed) settings.seed = *seed;
        if (!actorPath.empty()) {
            auto actor = decodeActor(read(actorPath));
            if (!actor) throw std::runtime_error(actor.status().describe());
            settings.actor = std::move(actor).takeValue();
        }
        if (warmId.empty() != !warmSteps.has_value() || (!warmId.empty() && !showBody))
            throw std::runtime_error("Warm requires --warm-id/--warm-steps and --body 1");
        if (inputBody && !showBody) throw std::runtime_error("Input body requires --body 1");
        const auto bodyPresentation = inputBody ? ActBodyPresentation::InputActor : ActBodyPresentation::SourceRig;
        auto       session          = std::make_shared<ActRenderSession>();
        auto       prepared         = ActRuntime::prepareInSession(info->kind, settings, session);
        if (!prepared) throw std::runtime_error(prepared.status().describe());
        auto published = ActRuntime::publish(std::move(prepared).takeValue());
        if (!published) throw std::runtime_error(published.status().describe());
        auto act = std::move(published).takeValue();
        std::filesystem::create_directories(out);
        Value::Array          states, frames;
        const auto            names = rig::poseFieldNames();
        std::vector<uint32_t> colors(size_t(view.width) * view.height), lights(colors.size());
        if (!warmId.empty()) {
            const PixelFxInfo* warmInfo = nullptr;
            for (const auto& item : PixelFx::catalogue())
                if (item.id == warmId) warmInfo = &item;
            if (!warmInfo) throw std::runtime_error("Unknown warm effect");
            auto warmSettings = PixelFx::defaults(warmInfo->kind);
            if (seed) warmSettings.seed = *seed;
            if (!actorPath.empty()) warmSettings.actor = settings.actor;
            auto candidate = ActRuntime::prepareInSession(warmInfo->kind, warmSettings, session);
            if (!candidate) throw std::runtime_error(candidate.status().describe());
            auto publishedWarm = ActRuntime::publish(std::move(candidate).takeValue());
            if (!publishedWarm) throw std::runtime_error(publishedWarm.status().describe());
            auto warm = std::move(publishedWarm).takeValue();
            for (int i = 0; i < *warmSteps; ++i) {
                auto step = warm->step(1.0 / 60);
                if (!step) throw std::runtime_error(step.status().describe());
            }
            PixelFxCanvas warmCanvas{view.width, view.height, view.originX, view.originY, colors, lights};
            auto          warmFrame = warm->prepareFrame(warmCanvas, placement, showTargets, bodyPresentation);
            if (!warmFrame) throw std::runtime_error(warmFrame.status().describe());
        }

        if (contract) {
            const auto timing         = timingValue(act->timing());
            auto       preparedBounds = act->prepareBounds();
            if (!preparedBounds) throw std::runtime_error(preparedBounds.status().describe());
            const auto bounds = preparedBounds.value().bounds();
            write(std::filesystem::path(out) / "contract.json",
                  Value::object({{"schema", Value("eve.pixelfx.act-contract/v1")},
                                 {"id", Value(id)},
                                 {"timing", timing},
                                 {"bounds", Value::object({{"x0", Value(bounds.x0)},
                                                           {"y0", Value(bounds.y0)},
                                                           {"x1", Value(bounds.x1)},
                                                           {"y1", Value(bounds.y1)}})}}));
        }
        auto observe = [&](size_t index) {
            if (stops.contains(index)) act->stop();
            const auto                           s    = act->snapshot();
            auto                                 pose = rig::encodePose(s.pose.pose);
            Value::Array                         fields, shake, events, targets;
            constexpr std::array<const char*, 9> skinNames{"",     "flash", "char",  "ice",   "shadow",
                                                           "holy", "red",   "shock", "poison"};
            for (const auto& target : act->targetStates()) {
                Value::Array straw;
                for (const auto& p : target.straw)
                    straw.push_back(Value::object({{"x", Value(p.x)},
                                                   {"y", Value(p.y)},
                                                   {"vx", Value(p.vx)},
                                                   {"vy", Value(p.vy)},
                                                   {"age", Value(p.age)},
                                                   {"life", Value(p.life)},
                                                   {"big", Value(p.big)}}));
                targets.push_back(Value::object(
                    {{"x", Value(target.x)},
                     {"theta", Value(target.theta)},
                     {"omega", Value(target.omega)},
                     {"flashTime", Value(target.flashTime)},
                     {"skin", target.skin == ActTargetSkin::None ? Value() : Value(skinNames[size_t(target.skin)])},
                     {"shiver", Value(target.shiver)},
                     {"clock", Value(target.clock)},
                     {"randomState", Value(int64_t(target.randomState))},
                     {"straw", Value::array(std::move(straw))}}));
            }
            for (size_t i = 0; i < names.size(); ++i)
                if (s.pose.contains(rig::PoseField(i))) fields.emplace_back(std::string(names[i]));
            for (auto n : s.shake) shake.emplace_back(int64_t(n));
            for (const auto& h : act->hitEvents())
                events.push_back(Value::object({{"sourceTime", Value(h.sourceTime)},
                                                {"actTime", Value(h.actTime)},
                                                {"effectTime", Value(h.effectTime)}}));
            states.push_back(Value::object({{"index", Value(int64_t(index))},
                                            {"wallTime", Value(s.wallTime)},
                                            {"actTime", Value(s.actTime)},
                                            {"effectTime", Value(s.effectTime)},
                                            {"frozen", Value(s.frozen)},
                                            {"done", Value(s.done)},
                                            {"shake", Value::array(std::move(shake))},
                                            {"pose", *pose.find("fields")},
                                            {"presence", Value::array(std::move(fields))},
                                            {"targets", Value::array(std::move(targets))},
                                            {"events", Value::array(std::move(events))}}));
            if (requested.contains(index)) {
                std::fill(colors.begin(), colors.end(), initialColor);
                std::fill(lights.begin(), lights.end(), initialLight);
                PixelFxCanvas canvas{view.width, view.height, view.originX, view.originY, colors, lights};
                auto          result = [&]() -> Result<void> {
                    if (!showBody) return act->drawWithoutBody(canvas, placement, showTargets);
                    auto preparedFrame = act->prepareFrame(canvas, placement, showTargets, bodyPresentation);
                    if (!preparedFrame) return Result<void>::failure(preparedFrame.status());
                    return preparedFrame.value().copyTo(canvas);
                }();
                if (!result) throw std::runtime_error(result.status().describe());
                const auto stem = "frame_" + std::to_string(index);
                plane(std::filesystem::path(out) / (stem + "_color.bin"), colors);
                plane(std::filesystem::path(out) / (stem + "_light.bin"), lights);
                frames.push_back(Value::object({{"time", Value(s.wallTime)},
                                                {"color", Value(stem + "_color.bin")},
                                                {"light", Value(stem + "_light.bin")}}));
            }
        };
        observe(0);
        for (size_t i = 0; i < steps->size(); ++i) {
            auto result = act->step(numeric((*steps)[i]));
            if (!result) throw std::runtime_error(result.status().describe());
            observe(i + 1);
        }
        write(std::filesystem::path(out) / "states.json",
              Value::object({{"schema", Value("eve.pixelfx.act-clock-probe/v1")},
                             {"id", Value(id)},
                             {"states", Value::array(std::move(states))}}));
        write(std::filesystem::path(out) / "frames.json",
              Value::object({{"schema", Value("eve.pixelfx-reference-frames/v1")},
                             {"id", Value(id)},
                             {"w", Value(int64_t(view.width))},
                             {"h", Value(int64_t(view.height))},
                             {"frames", Value::array(std::move(frames))}}));
        const auto stats = session->cacheStats();
        write(std::filesystem::path(out) / "session-stats.json",
              Value::object({{"schema", Value("eve.pixelfx.act-render-session-stats/v1")},
                             {"entries", Value(int64_t(stats.entries))},
                             {"hits", Value(int64_t(stats.hits))},
                             {"rasterEvaluations", Value(int64_t(stats.rasterEvaluations))},
                             {"clears", Value(int64_t(stats.clears))}}));
        std::cout << id << ": native Act clock/effect-layer observation complete\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
