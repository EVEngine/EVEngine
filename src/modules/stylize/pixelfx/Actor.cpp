// Native actor inputs adapted from pixelfx core/actor.js, MIT. Copyright (c) 2026 Patch.
// Cold preparation only: resolved geometry is owned by the immutable PixelFx program.
#include <limits>
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Math.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr int                              maxDimension  = 256;
constexpr double                           maxCoordinate = 4096;
constexpr size_t                           maxEyes       = 64;
constexpr std::array<std::string_view, 19> heroRows{
    "....kkkkk...", "..kkhhhhhk..", ".khhhhhhhhk.", ".khhhhhhhhhk", ".khhssssshhk", ".khsessesshk", ".khsessesshk",
    "..ksssssssk.", "...kkSSSkk..", "..kttttttk..", ".kttTttTttk.", ".ksttTTttsk.", ".kstttttttk.", "..kbbbbbbk..",
    "..kppkkppk..", "..kppk.kppk.", "..kppk.kppk.", "..koook.kook", "..kkkkk.kkkk",
};
uint32_t heroColor(char c) {
    switch (c) {
        case 'k': return hex(0x17121f);
        case 'h': return hex(0x7a4128);
        case 's': return hex(0xf3c49d);
        case 'S': return hex(0xc98b6c);
        case 'e': return hex(0x17121f);
        case 't': return hex(0x5f6f94);
        case 'T': return hex(0x46536f);
        case 'b': return hex(0x8a5a32);
        case 'p': return hex(0x38334a);
        case 'o': return hex(0x6a4030);
        case 'H': return hex(0xa35d34);
        case 'w': return hex(0xffffff);
        case 'm': return hex(0xa3564a);
        case 'g': return hex(0xe8c060);
        case 'L': return hex(0x7f90b6);
        case 'O': return hex(0x8f5a3f);
        case 'P': return hex(0x4b4560);
        case 'c': return hex(0xf53cd1);
        case 'C': return hex(0xff8fe6);
        case 'D': return hex(0xa31f8a);
        default: return 0;
    }
}
constexpr std::array<std::string_view, 38> dummyRows{
    "...........kkk..........", "..........kAABk.........", ".........kAAABJk........", ".........kABBJJk........",
    ".........kBBBJCDk.......", ".........kBCJDDDk.......", ".........kkJJJDk........", ".......kkkkABBJCkk......",
    "......kAABCJBCCCkDk.....", "......kABBCJCCCmDkDk....", "......kBBCDDJCDDDkDk....", "......kABCDJDDDDDkk.....",
    ".....kAABDkDDDDJkk......", ".....kABCDkDDJJGkk......", ".....kABCkJJJGGGkk......", "....kJJCDkFFFGGGk.......",
    "....kAJJDkJJJGGGk.......", "....kJCJJkABBJGGk.......", ".....kJCCkBBCDJJJk......", ".....kCCCJBBCDJJDk......",
    ".....kCCCDJCCDkJDDk.....", "......kCDDJBCDkDDDk.....", ".......kDJJJCDkDDDkk....", ".......kJBCJCDkDDDkk....",
    ".......kBDJBCDkCDDkk....", "........kJJBCDkCDDk.....", "........kBBBCDkCCDDk....", "........kBBBCDkCCDDk....",
    ".........kBBCDkJDDDk....", "........kBJJJJkCJJJk....", "........kBBCCDkCCDDk....", "........kBBCDDkCDDk.....",
    "........kBBCDkCCDDk.....", "........kBCCDkCCDDk.....", "........kBCDDkCDDDk.....", ".......kJBCJkkkDJkkkk...",
    ".......kAJJAAAAkCCCCCk..", "........kkkkkkkkkkkkk...",
};
uint32_t dummyColor(char c) {
    switch (c) {
        case 'k': return hex(0x0e0c14);
        case 'A': return hex(0xe4e6ec);
        case 'B': return hex(0xb6b9c3);
        case 'C': return hex(0x868995);
        case 'D': return hex(0x5c5e6a);
        case 'E': return hex(0x6e707c);
        case 'F': return hex(0x4b4d58);
        case 'G': return hex(0x34353f);
        case 'J': return hex(0x2a2b33);
        case 'm': return hex(0xf53cd1);
        default: return 0;
    }
}
PixelFxActor stock(PixelFxActorSource source) {
    PixelFxActor actor;
    actor.source = source;
    if (source == PixelFxActorSource::None) return actor;
    const bool large = source == PixelFxActorSource::Dummy;
    const auto rows =
        large ? std::span<const std::string_view>(dummyRows) : std::span<const std::string_view>(heroRows);
    actor.width  = int(rows.front().size());
    actor.height = int(rows.size());
    actor.scale  = large ? 2 : 1;
    actor.x      = -std::floor(double(actor.width) / 2);
    actor.y      = -actor.height;
    actor.head   = {0, actor.y};
    actor.chest  = {0, large ? -26 : actor.y + 11};
    if (large) actor.eyes = {{{13, 3}}, {{15, 3}}};
    actor.mask.reserve(size_t(actor.width) * size_t(actor.height));
    actor.pixels.reserve(actor.mask.capacity());
    for (auto row : rows)
        for (char c : row) {
            actor.mask.push_back(c == '.' ? 0 : 1);
            actor.pixels.push_back(large ? dummyColor(c) : heroColor(c));
        }
    return actor;
}
Diagnostic invalid(std::string message, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), "/options/actor" + path);
}
std::string escape(std::string_view key) {
    std::string out;
    for (char c : key) out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
Value point(std::array<double, 2> p) { return Value::array({Value(p[0]), Value(p[1])}); }
}  // namespace
Value actorConstraints(bool required, int minimumFilledMaskCells, int minimumHeight) {
    return Value::object(
        {{"schema", Value("eve.pixelfx.actor")},
         {"version", Value(int64_t(1))},
         {"sources", required ? Value::array({Value("hero"), Value("dummy"), Value("custom")})
                              : Value::array({Value("none"), Value("hero"), Value("dummy"), Value("custom")})},
         {"minimumFilledMaskCells", Value(int64_t(minimumFilledMaskCells))},
         {"minimumDimension", Value(int64_t(1))},
         {"minimumHeight", Value(int64_t(minimumHeight))},
         {"maximumDimension", Value(int64_t(maxDimension))},
         {"maximumEyeCells", Value(int64_t(maxEyes))},
         {"minimumCoordinate", Value(-maxCoordinate)},
         {"maximumCoordinate", Value(maxCoordinate)},
         {"minimumScale", Value(int64_t(1))},
         {"maximumScale", Value(int64_t(2))},
         {"maskEncoding", Value("binary-row-major")},
         {"pixelEncoding", Value("rgba8-abgr")}});
}
Value encodeActor(const PixelFxActor& actor) {
    const char* source = actor.source == PixelFxActorSource::None     ? "none"
                         : actor.source == PixelFxActorSource::Hero   ? "hero"
                         : actor.source == PixelFxActorSource::Dummy  ? "dummy"
                         : actor.source == PixelFxActorSource::Custom ? "custom"
                                                                      : "invalid";
    Value       out    = Value::object(
        {{"schema", Value("eve.pixelfx.actor")}, {"version", Value(int64_t(1))}, {"source", Value(source)}});
    if (actor.source != PixelFxActorSource::Custom) return out;
    out.set("width", Value(int64_t(actor.width)));
    out.set("height", Value(int64_t(actor.height)));
    out.set("scale", Value(int64_t(actor.scale)));
    out.set("x", Value(actor.x));
    out.set("y", Value(actor.y));
    out.set("head", point(actor.head));
    out.set("chest", point(actor.chest));
    Value::Array mask, pixels, eyes;
    mask.reserve(actor.mask.size());
    pixels.reserve(actor.pixels.size());
    for (auto v : actor.mask) mask.emplace_back(int64_t(v));
    for (auto v : actor.pixels) pixels.emplace_back(int64_t(v));
    for (const auto& v : actor.eyes) eyes.push_back(Value::array({Value(int64_t(v[0])), Value(int64_t(v[1]))}));
    out.set("mask", Value::array(std::move(mask)));
    out.set("pixels", Value::array(std::move(pixels)));
    out.set("eyes", Value::array(std::move(eyes)));
    return out;
}
Result<PixelFxActor> decodeActor(const Value& input) {
    using R            = Result<PixelFxActor>;
    const auto* object = input.getIf<Value::Object>();
    if (!object) return R::failure(invalid("Expected a versioned actor object", ""));
    for (const auto& [key, value] : *object)
        if (key != "schema" && key != "version" && key != "source" && key != "width" && key != "height" &&
            key != "scale" && key != "x" && key != "y" && key != "head" && key != "chest" && key != "eyes" &&
            key != "mask" && key != "pixels")
            return R::failure(invalid("Unknown actor field", "/" + escape(key)));
    const auto* schema = input.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.pixelfx.actor")
        return R::failure(invalid("Expected eve.pixelfx.actor schema", "/schema"));
    const auto* version = input.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return R::failure(Diagnostic(DiagnosticCode::UnknownVersion, Severity::Error, "Unsupported actor version",
                                     "/options/actor/version"));
    const auto* source = input.find("source");
    if (!source || !source->isString()) return R::failure(invalid("Expected actor source", "/source"));
    const auto& tag = source->asString();
    if (tag == "none" || tag == "hero" || tag == "dummy") {
        for (const auto& [key, value] : *object)
            if (key != "schema" && key != "version" && key != "source")
                return R::failure(invalid("Stock/disabled actor fields are derived from source", "/" + escape(key)));
        return R::success(stock(tag == "none"   ? PixelFxActorSource::None
                                : tag == "hero" ? PixelFxActorSource::Hero
                                                : PixelFxActorSource::Dummy));
    }
    if (tag != "custom") return R::failure(invalid("Expected none, hero, dummy or custom actor source", "/source"));
    PixelFxActor actor;
    actor.source = PixelFxActorSource::Custom;
    auto integer = [&](std::string_view name, int low, int high, int& dest) -> Result<void> {
        const auto* v = input.find(std::string(name));
        if (!v || !v->isInt64() || v->asInt() < low || v->asInt() > high)
            return Result<void>::failure(invalid("Expected bounded actor integer", "/" + std::string(name)));
        dest = int(v->asInt());
        return Result<void>::success();
    };
    auto number = [&](const Value* v, double& dest, std::string path) -> Result<void> {
        if (!v || (!v->isInt64() && !v->isDouble()))
            return Result<void>::failure(invalid("Expected finite actor coordinate", path));
        const double n = v->isInt64() ? double(v->asInt()) : v->asDouble();
        if (!std::isfinite(n) || std::abs(n) > maxCoordinate)
            return Result<void>::failure(invalid("Actor coordinate outside -4096..4096", path));
        dest = n;
        return Result<void>::success();
    };
    auto w = integer("width", 1, maxDimension, actor.width);
    if (!w) return R::failure(w.status());
    auto h = integer("height", 1, maxDimension, actor.height);
    if (!h) return R::failure(h.status());
    auto scale = integer("scale", 1, 2, actor.scale);
    if (!scale) return R::failure(scale.status());
    auto x = number(input.find("x"), actor.x, "/x");
    if (!x) return R::failure(x.status());
    auto y = number(input.find("y"), actor.y, "/y");
    if (!y) return R::failure(y.status());
    for (auto name : {"head", "chest"}) {
        const auto* v = input.find(std::string(name));
        const auto* a = v ? v->getIf<Value::Array>() : nullptr;
        if (!a || a->size() != 2)
            return R::failure(invalid("Expected two actor anchor coordinates", "/" + std::string(name)));
        auto& dest = std::string_view(name) == "head" ? actor.head : actor.chest;
        for (size_t i = 0; i < 2; ++i) {
            auto n = number(&(*a)[i], dest[i], "/" + std::string(name) + "/" + std::to_string(i));
            if (!n) return R::failure(n.status());
        }
    }
    const size_t count = size_t(actor.width) * size_t(actor.height);
    for (auto name : {"mask", "pixels"}) {
        const auto* v = input.find(std::string(name));
        const auto* a = v ? v->getIf<Value::Array>() : nullptr;
        if (!a || a->size() != count)
            return R::failure(invalid("Actor array must contain width*height entries", "/" + std::string(name)));
        const bool mask = std::string_view(name) == "mask";
        if (mask)
            actor.mask.reserve(count);
        else
            actor.pixels.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto* n = (*a)[i].getIf<int64_t>();
            if (!n || *n < 0 || uint64_t(*n) > (mask ? 1u : std::numeric_limits<uint32_t>::max()))
                return R::failure(
                    invalid("Actor cell must be a bounded integer", "/" + std::string(name) + "/" + std::to_string(i)));
            if (mask)
                actor.mask.push_back(uint8_t(*n));
            else
                actor.pixels.push_back(uint32_t(*n));
        }
    }
    const auto* eyes    = input.find("eyes");
    const auto* entries = eyes ? eyes->getIf<Value::Array>() : nullptr;
    if (!entries || entries->size() > maxEyes) return R::failure(invalid("Expected up to 64 actor eye cells", "/eyes"));
    for (size_t i = 0; i < entries->size(); ++i) {
        const auto*       pair = (*entries)[i].getIf<Value::Array>();
        const std::string path = "/eyes/" + std::to_string(i);
        if (!pair || pair->size() != 2) return R::failure(invalid("Expected an actor eye cell pair", path));
        std::array<int, 2> cell{};
        for (size_t c = 0; c < 2; ++c) {
            const auto* n = (*pair)[c].getIf<int64_t>();
            if (!n || *n < 0 || *n >= (c == 0 ? actor.width : actor.height))
                return R::failure(invalid("Actor eye cell outside its mask", path + "/" + std::to_string(c)));
            cell[c] = int(*n);
        }
        actor.eyes.push_back(cell);
    }
    return R::success(std::move(actor));
}
}  // namespace eve::stylize::pixelfx
