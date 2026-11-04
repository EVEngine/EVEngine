#include "graphics/DamageNumbersBindings.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/DamageNumbers.h"
#include "graphics/Font.h"
#include "graphics/Graphics.h"

#include <functional>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::graphics {
namespace {
Result<DamageNumberKind> parseKind(const std::string& kind) {
    if (kind == "damage") return Result<DamageNumberKind>::success(DamageNumberKind::Damage);
    if (kind == "critical") return Result<DamageNumberKind>::success(DamageNumberKind::Critical);
    if (kind == "heal") return Result<DamageNumberKind>::success(DamageNumberKind::Heal);
    if (kind == "miss") return Result<DamageNumberKind>::success(DamageNumberKind::Miss);
    return Result<DamageNumberKind>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown damage-number kind", "kind"));
}
Value admission(DamageNumberAdmission result) {
    switch (result) {
        case DamageNumberAdmission::Spawned: return Value("spawned");
        case DamageNumberAdmission::Merged: return Value("merged");
        case DamageNumberAdmission::ReplacedOldest: return Value("replacedOldest");
    }
    return Value("spawned");
}
}  // namespace

void exposeFontAndDamageNumberBindings(ssq::Table& table, ssq::Class& graphicsClass) {
#ifndef EVENGINE_WEBGPU
    auto fontCls =
        table.addClass<Font>("GraphicsFont", std::function<Font*()>([]() -> Font* { return nullptr; }), true);
    fontCls.addFunc("getHeight", &Font::getHeight);
    fontCls.addFunc("getAscent", &Font::getAscent);
    fontCls.addFunc("getBaseline", &Font::getBaseline);
    fontCls.addFunc("getWidth", &Font::getWidth);
    fontCls.addFunc("hasGlyph", &Font::hasGlyph);
#endif
    const auto vm    = table.getHandle();
    auto       style = table.addClass("DamageNumberStyle", ssq::Class::Ctor<DamageNumberStyle()>());
    style.addVar("lifetime", &DamageNumberStyle::lifetime);
    style.addVar("velocityY", &DamageNumberStyle::velocityY);
    style.addVar("gravity", &DamageNumberStyle::gravity);
    style.addVar("scale", &DamageNumberStyle::scale);
    style.addVar("pop", &DamageNumberStyle::pop);
    style.addVar("shake", &DamageNumberStyle::shake);
    style.addVar("r", &DamageNumberStyle::r);
    style.addVar("g", &DamageNumberStyle::g);
    style.addVar("b", &DamageNumberStyle::b);
    style.addVar("outline", &DamageNumberStyle::outline);
    style.addVar("fadeIn", &DamageNumberStyle::fadeIn);
    style.addVar("fadeOut", &DamageNumberStyle::fadeOut);
    style.addVar("mergeWindow", &DamageNumberStyle::mergeWindow);
    style.addVar("mergeRadius", &DamageNumberStyle::mergeRadius);
    style.addVar("decimals", &DamageNumberStyle::decimals);
    style.addVar("compact", &DamageNumberStyle::compact);
    auto cls =
        table.addClass<DamageNumbers>("DamageNumbers", std::function<DamageNumbers*()>([] { return nullptr; }), false);
    graphicsClass.addFunc("newDamageNumbers", [vm](Graphics*) {
        auto owned = script::makeOwnedSquirrelInstance(vm, std::make_unique<DamageNumbers>());
        if (!owned) return script::projectStatusResult(vm, owned.status());
        auto result = script::projectStatusResult(vm, Status::success(), std::move(owned).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
    cls.addFunc("spawn", [vm](DamageNumbers* self, float amount, float x, float y, const std::string& kind,
                              const std::string& group, float velocityX) {
        auto parsed = parseKind(kind);
        if (!parsed) return script::projectStatusResult(vm, parsed.status());
        return script::projectResult(vm, self->spawn({amount, x, y, parsed.value(), group, velocityX}), admission);
    });
    cls.addFunc("setStyle", [vm](DamageNumbers* self, const std::string& kind, const DamageNumberStyle* value) {
        auto parsed = parseKind(kind);
        if (!parsed) return script::projectStatusResult(vm, parsed.status());
        if (!value)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Style is required", "style")));
        return script::projectResult(vm, self->setStyle(parsed.value(), *value));
    });
    cls.addFunc("update",
                [vm](DamageNumbers* self, float seconds) { return script::projectResult(vm, self->update(seconds)); });
    cls.addFunc("clear", &DamageNumbers::clear);
    cls.addFunc("count", [](const DamageNumbers* self) { return static_cast<int>(self->count()); });
    cls.addFunc("snapshot", [vm](const DamageNumbers* self) {
        Value::Array result;
        for (const auto& sample : self->snapshot())
            result.emplace_back(Value::object({{"text", sample.text},
                                               {"x", sample.x},
                                               {"y", sample.y},
                                               {"scale", sample.scale},
                                               {"alpha", sample.alpha}}));
        return script::projectResult(vm, Result<Value>::success(Value(std::move(result))),
                                     [](Value value) { return value; });
    });
    cls.addFunc("render", [vm](const DamageNumbers* self, Graphics* graphics, Font* font) {
        if (!graphics || !font)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Graphics and font are required",
                                                      "render")));
        self->render(*graphics, *font);
        return script::projectResult(vm, Result<void>::success());
    });
}
}  // namespace eve::graphics
