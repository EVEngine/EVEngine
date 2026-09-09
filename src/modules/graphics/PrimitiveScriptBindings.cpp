#include "graphics/PrimitiveScriptBindings.h"
#include "graphics/Graphics.h"
#include "graphics/PrimitiveScene.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <functional>
#include <memory>

namespace eve::graphics {
namespace {
struct ScriptPrimitive3D {
    ScriptPrimitive3D(std::weak_ptr<PrimitiveScene> sceneValue, PrimitiveHandle handleValue)
        : scene(std::move(sceneValue)), handle(handleValue) {}
    ~ScriptPrimitive3D() noexcept {
        if (auto owner = scene.lock(); owner && !owner->isStale(handle))
            owner->remove(handle).ignore("script primitive proxy destruction");
    }

    std::weak_ptr<PrimitiveScene> scene;
    PrimitiveHandle               handle;
};

template <class T>
eve::Result<T> primitiveBindingFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "graphics.primitive.binding"));
}

eve::Result<PrimitiveDescriptor3D> primitiveDescriptor(ScriptPrimitive3D* value) {
    if (!value)
        return primitiveBindingFailure<PrimitiveDescriptor3D>(eve::DiagnosticCode::InvalidArgument,
                                                              "primitive proxy must not be null", "primitive");
    auto scene = value->scene.lock();
    if (!scene)
        return primitiveBindingFailure<PrimitiveDescriptor3D>(eve::DiagnosticCode::StaleHandle,
                                                              "primitive scene owner no longer exists", "primitive");
    const PrimitiveDescriptor3D* descriptor = scene->tryGet(value->handle);
    if (!descriptor)
        return primitiveBindingFailure<PrimitiveDescriptor3D>(eve::DiagnosticCode::StaleHandle,
                                                              "primitive handle is stale", "primitive");
    return eve::Result<PrimitiveDescriptor3D>::success(*descriptor);
}

template <class Mutator>
eve::Result<PrimitiveUpdateStatus> mutatePrimitive(ScriptPrimitive3D* value, Mutator&& mutator) {
    auto descriptor = primitiveDescriptor(value);
    if (!descriptor) return eve::Result<PrimitiveUpdateStatus>::failure(descriptor.status());
    PrimitiveDescriptor3D copy = std::move(descriptor).takeValue();
    std::invoke(std::forward<Mutator>(mutator), copy);
    auto scene = value->scene.lock();
    if (!scene)
        return primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::StaleHandle,
                                                              "primitive scene owner no longer exists", "primitive");
    return scene->update(value->handle, std::move(copy));
}

ssq::Table makePrimitiveProxy(HSQUIRRELVM vm, const std::shared_ptr<PrimitiveScene>& scene,
                              eve::Result<PrimitiveHandle>&& result) {
    if (!result) return eve::script::projectStatusResult(vm, result.status(), false, false);
    const PrimitiveHandle handle = std::move(result).takeValue();
    auto                  object = eve::script::makeOwnedSquirrelInstance<ScriptPrimitive3D>(
        vm, std::make_unique<ScriptPrimitive3D>(scene, handle));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned primitive proxy");
        scene->remove(handle).ignore("rollback failed primitive proxy allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    auto projected = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    projected.set("value", std::move(object).takeValue());
    projected.set("ownership", std::string("owned"));
    projected.set("owner", static_cast<std::int64_t>(handle.owner()));
    projected.set("index", static_cast<std::int64_t>(handle.index()));
    projected.set("generation", static_cast<std::int64_t>(handle.generation()));
    return projected;
}

ScenePrimitivePaint scriptPrimitivePaint(float r, float g, float b, float a, float width) {
    ScenePrimitivePaint paint;
    paint.color             = Color(r, g, b, a);
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = width;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    return paint;
}

eve::Result<std::vector<glm::vec3>> scriptPoints(ssq::Array values, std::size_t minimum, std::size_t maximum) {
    if (values.size() % 3 != 0 || values.size() / 3 < minimum || values.size() / 3 > maximum)
        return primitiveBindingFailure<std::vector<glm::vec3>>(eve::DiagnosticCode::InvalidArgument,
            "expected a flat xyz array with the required point count", "points");
    std::vector<glm::vec3> points;
    points.reserve(values.size() / 3);
    try {
        for (std::size_t i = 0; i < values.size(); i += 3)
            points.push_back({values.get<float>(i), values.get<float>(i+1), values.get<float>(i+2)});
    } catch (const std::exception& error) {
        return primitiveBindingFailure<std::vector<glm::vec3>>(eve::DiagnosticCode::InvalidArgument,
            error.what(), "points");
    }
    return eve::Result<std::vector<glm::vec3>>::success(std::move(points));
}
} // namespace

void exposePrimitiveScriptBindings(ssq::Table& table, ssq::Class& cls) {
    const auto vm = table.getHandle();
    cls.addFunc("newPrimitiveGrid3D", [vm](Graphics* self, ssq::Array xyz, int cellsU, int cellsV,
            float r, float g, float b, float a, float width) {
        auto parsed = scriptPoints(xyz, 3, 3);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status(), false, false);
        auto points = std::move(parsed).takeValue();
        if (cellsU <= 0 || cellsV <= 0 || cellsU > 4096 || cellsV > 4096)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveHandle>(eve::DiagnosticCode::InvalidArgument,
                    "grid cells must be in [1,4096]", "cells").status(), false, false);
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveGrid3D{points[0],points[1],points[2],
            static_cast<std::uint32_t>(cellsU),static_cast<std::uint32_t>(cellsV)};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveArc3D", [vm](Graphics* self, ssq::Array xyz, float radius, float start, float sweep,
            float r, float g, float b, float a, float width) {
        auto parsed = scriptPoints(xyz, 3, 3);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status(), false, false);
        auto points = std::move(parsed).takeValue();
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveArc3D{points[0],points[1],points[2],radius,start,sweep,32};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitivePolyline3D", [vm](Graphics* self, ssq::Array xyz, bool closed,
            float r, float g, float b, float a, float width) {
        auto parsed = scriptPoints(xyz, closed ? 3 : 2, 65536);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status(), false, false);
        auto points = std::move(parsed).takeValue();
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitivePolyline3D{points,closed};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveObb3D", [vm](Graphics* self, ssq::Array xyz,
            float r, float g, float b, float a, float width) {
        auto parsed = scriptPoints(xyz, 4, 4);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status(), false, false);
        auto points = std::move(parsed).takeValue();
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveObb3D{points[0],{points[1],points[2],points[3]}};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveFrustum3D", [vm](Graphics* self, ssq::Array xyz,
            float r, float g, float b, float a, float width) {
        auto parsed = scriptPoints(xyz, 8, 8);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status(), false, false);
        auto points = std::move(parsed).takeValue();
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveFrustum3D{{points[0],points[1],points[2],points[3],points[4],points[5],points[6],points[7]}};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveDisk3D", [vm](Graphics* self, float x, float y, float z, float nx, float ny, float nz, float radius,
            float r, float g, float b, float a, float width) {
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveDisk3D{{x,y,z},{nx,ny,nz},radius,32};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveCylinder3D", [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz, float radius,
            float r, float g, float b, float a, float width) {
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveCylinder3D{{ax,ay,az},{bx,by,bz},radius,32};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveCapsule3D", [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz, float radius,
            float r, float g, float b, float a, float width) {
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveCapsule3D{{ax,ay,az},{bx,by,bz},radius,32};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveCone3D", [vm](Graphics* self, float x, float y, float z, float dx, float dy, float dz, float height, float radius,
            float r, float g, float b, float a, float width) {
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveCone3D{{x,y,z},{dx,dy,dz},height,radius,32};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveArrow3D", [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz, float headLength, float headRadius,
            float r, float g, float b, float a, float width) {
        auto scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveArrow3D{{ax,ay,az},{bx,by,bz},headLength,headRadius};
        descriptor.paint = scriptPrimitivePaint(r,g,b,a,width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    auto primitive = table.addClass<ScriptPrimitive3D>(
        "Primitive3D", std::function<ScriptPrimitive3D*()>([]() { return nullptr; }), false);
    primitive.addFunc("ownership", [](ScriptPrimitive3D*) { return std::string("owned"); });
    primitive.addFunc("setPolyline", [vm](ScriptPrimitive3D* value, ssq::Array xyz, bool closed) {
        auto parsed = scriptPoints(xyz, closed ? 3 : 2, 65536);
        if (!parsed) return eve::script::projectStatusResult(vm, parsed.status(), false, false);
        auto points = std::move(parsed).takeValue();
        auto result = mutatePrimitive(value, [&](PrimitiveDescriptor3D& descriptor) {
            descriptor.geometry = PrimitivePolyline3D{std::move(points), closed};
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setLayer", [vm](ScriptPrimitive3D* value, std::int64_t layer) {
        if (layer < 0 || layer > 0xffffffffLL)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "layer must fit uint32", "layer").status(), false, false);
        auto result = mutatePrimitive(value, [layer](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.layer = static_cast<std::uint32_t>(layer);
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setTransform", [vm](ScriptPrimitive3D* value, ssq::Array elements) {
        if (elements.size() != 16)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "transform requires 16 column-major floats", "transform").status(), false, false);
        glm::mat4 transform(1.f);
        try {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    transform[column][row] = elements.get<float>(column * 4 + row);
        } catch (const std::exception& error) {
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    error.what(), "transform").status(), false, false);
        }
        auto result = mutatePrimitive(value, [&](PrimitiveDescriptor3D& descriptor) {
            descriptor.transform = transform;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setWidthSpace", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<WidthSpace> parsed;
        if (mode == "screen") parsed = WidthSpace::ScreenPixels;
        if (mode == "world") parsed = WidthSpace::WorldUnits;
        if (!parsed)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "unsupported setWidthSpace value", "setWidthSpace").status(), false, false);
        auto result = mutatePrimitive(value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.stroke.widthSpace = mode;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setPaintMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<PaintMode> parsed;
        if (mode == "fill") parsed = PaintMode::Fill;
        if (mode == "stroke") parsed = PaintMode::Stroke;
        if (mode == "fill-stroke") parsed = PaintMode::FillAndStroke;
        if (!parsed)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "unsupported setPaintMode value", "setPaintMode").status(), false, false);
        auto result = mutatePrimitive(value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.mode = mode;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setLineCap", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<LineCap> parsed;
        if (mode == "butt") parsed = LineCap::Butt;
        if (mode == "square") parsed = LineCap::Square;
        if (mode == "round") parsed = LineCap::Round;
        if (!parsed)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "unsupported setLineCap value", "setLineCap").status(), false, false);
        auto result = mutatePrimitive(value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.stroke.cap = mode;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setLineJoin", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<LineJoin> parsed;
        if (mode == "miter") parsed = LineJoin::Miter;
        if (mode == "bevel") parsed = LineJoin::Bevel;
        if (mode == "round") parsed = LineJoin::Round;
        if (!parsed)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "unsupported setLineJoin value", "setLineJoin").status(), false, false);
        auto result = mutatePrimitive(value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.stroke.join = mode;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setCullMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<PrimitiveCullMode> parsed;
        if (mode == "none") parsed = PrimitiveCullMode::None;
        if (mode == "back") parsed = PrimitiveCullMode::Back;
        if (mode == "front") parsed = PrimitiveCullMode::Front;
        if (!parsed)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "unsupported setCullMode value", "setCullMode").status(), false, false);
        auto result = mutatePrimitive(value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.cull = mode;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setBlendMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<BlendMode> parsed;
        if (mode == "alpha") parsed = BlendMode::Alpha;
        if (mode == "opaque") parsed = BlendMode::Opaque;
        if (mode == "additive") parsed = BlendMode::Additive;
        if (mode == "premultiplied") parsed = BlendMode::Premultiplied;
        if (mode == "multiply") parsed = BlendMode::Multiply;
        if (!parsed)
            return eve::script::projectStatusResult(vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                    "unsupported setBlendMode value", "setBlendMode").status(), false, false);
        auto result = mutatePrimitive(value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.blend = mode;
        });
        const auto status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("isStale", [](ScriptPrimitive3D* value) {
        if (!value) return true;
        auto scene = value->scene.lock();
        return !scene || scene->isStale(value->handle);
    });
    primitive.addFunc("remove", [vm](ScriptPrimitive3D* value) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                primitiveBindingFailure<PrimitiveRemoveStatus>(eve::DiagnosticCode::InvalidArgument,
                                                               "primitive proxy must not be null", "primitive")
                    .status(),
                false, false);
        auto scene = value->scene.lock();
        if (!scene)
            return eve::script::projectStatusResult(
                vm,
                primitiveBindingFailure<PrimitiveRemoveStatus>(eve::DiagnosticCode::StaleHandle,
                                                               "primitive scene owner no longer exists", "primitive")
                    .status(),
                false, false);
        auto              result = scene->remove(value->handle);
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        value->handle = {};
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setVisible", [vm](ScriptPrimitive3D* value, bool visible) {
        auto result =
            mutatePrimitive(value, [visible](PrimitiveDescriptor3D& descriptor) { descriptor.visible = visible; });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setColor", [vm](ScriptPrimitive3D* value, float r, float g, float b, float a) {
        auto result = mutatePrimitive(
            value, [=](PrimitiveDescriptor3D& descriptor) { descriptor.paint.color = Color(r, g, b, a); });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setLineWidth", [vm](ScriptPrimitive3D* value, float width) {
        auto result = mutatePrimitive(
            value, [width](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.width = width; });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setDash", [vm](ScriptPrimitive3D* value, float drawLength, float gapLength, float phase,
                                      const std::string& space) {
        if (space != "screen" && space != "world")
            return eve::script::projectStatusResult(
                vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                                                               "dash space must be screen or world", "dashSpace")
                    .status(),
                false, false);
        auto              result = mutatePrimitive(value, [=](PrimitiveDescriptor3D& descriptor) {
            DashPattern dash;
            dash.intervals = {drawLength, gapLength};
            dash.phase     = phase;
            dash.space     = space == "screen" ? DashSpace::ScreenPixels : DashSpace::WorldUnits;
            descriptor.paint.stroke.dash = std::move(dash);
        });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("clearDash", [vm](ScriptPrimitive3D* value) {
        auto result =
            mutatePrimitive(value, [](PrimitiveDescriptor3D& descriptor) { descriptor.paint.stroke.dash.reset(); });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setDepthMode", [vm](ScriptPrimitive3D* value, const std::string& mode) {
        std::optional<PrimitiveDepthMode> parsed;
        if (mode == "test-write")
            parsed = PrimitiveDepthMode::TestAndWrite;
        else if (mode == "test")
            parsed = PrimitiveDepthMode::TestOnly;
        else if (mode == "ignore")
            parsed = PrimitiveDepthMode::Ignore;
        if (!parsed)
            return eve::script::projectStatusResult(
                vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(
                    eve::DiagnosticCode::InvalidArgument, "depth mode must be test-write, test, or ignore", "depthMode")
                    .status(),
                false, false);
        auto result = mutatePrimitive(
            value, [mode = *parsed](PrimitiveDescriptor3D& descriptor) { descriptor.paint.depth = mode; });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });
    primitive.addFunc("setObjectId", [vm](ScriptPrimitive3D* value, std::int64_t objectId) {
        if (objectId < 0)
            return eve::script::projectStatusResult(
                vm,
                primitiveBindingFailure<PrimitiveUpdateStatus>(eve::DiagnosticCode::InvalidArgument,
                                                               "object id must be non-negative", "objectId")
                    .status(),
                false, false);
        auto              result = mutatePrimitive(value, [objectId](PrimitiveDescriptor3D& descriptor) {
            descriptor.paint.objectId = static_cast<std::uint64_t>(objectId);
        });
        const eve::Status status = result.status();
        if (!result) return eve::script::projectStatusResult(vm, status, false, false);
        std::move(result).takeValue();
        return eve::script::projectStatusResult(vm, status, true, false);
    });

    cls.addFunc("newPrimitiveLine3D", [vm](Graphics* self, float ax, float ay, float az, float bx, float by, float bz,
                                           float r, float g, float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitivePolyline3D{{{ax, ay, az}, {bx, by, bz}}, false};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveSphere3D", [vm](Graphics* self, float x, float y, float z, float radius, float r, float g,
                                             float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveSphere3D{{x, y, z}, radius, 32};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
    cls.addFunc("newPrimitiveAabb3D", [vm](Graphics* self, float minX, float minY, float minZ, float maxX, float maxY,
                                           float maxZ, float r, float g, float b, float a, float width) {
        auto                  scene = self->getPrimitiveScene();
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = PrimitiveAabb3D{{minX, minY, minZ}, {maxX, maxY, maxZ}};
        descriptor.paint    = scriptPrimitivePaint(r, g, b, a, width);
        return makePrimitiveProxy(vm, scene, scene->add(std::move(descriptor)));
    });
}
} // namespace eve::graphics
