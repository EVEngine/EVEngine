#include "graphics/multiscale/MultiscaleView.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/Graphics.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/SphereView.h"
#include "spatial/multiscale/CellPosition.h"

namespace eve::graphics::multiscale {
namespace {
Diagnostic invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.multiscale.script");
}
const Value& required(const Value& object, const char* name) {
    const auto* value = object.find(name);
    if (!value) throw std::invalid_argument(std::string("Missing field: ") + name);
    return *value;
}
void keys(const Value& value, std::initializer_list<std::string_view> allowed) {
    if (!value.isObject()) throw std::invalid_argument("Expected object");
    for (const auto& key : value.keys()) {
        bool found = false;
        for (auto expected : allowed) found |= key == expected;
        if (!found) throw std::invalid_argument("Unknown field: " + key);
    }
}
double number(const Value& value) {
    if (!value.isNumeric()) throw std::invalid_argument("Expected finite number");
    const double result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(result)) throw std::invalid_argument("Expected finite number");
    return result;
}
int integer(const Value& value) {
    if (!value.isInt64() || value.asInt() < std::numeric_limits<int>::min() ||
        value.asInt() > std::numeric_limits<int>::max())
        throw std::invalid_argument("Expected int32");
    return static_cast<int>(value.asInt());
}
std::array<double, 3> triple(const Value& value) {
    if (!value.isArray() || value.arraySize() != 3) throw std::invalid_argument("Expected three numbers");
    return {number(value.at(0)), number(value.at(1)), number(value.at(2))};
}
spatial::CellPosition position(const Value& value) {
    keys(value, {"cell", "fraction"});
    const auto& cells = required(value, "cell");
    if (!cells.isArray() || cells.arraySize() != 3) throw std::invalid_argument("Expected three decimal cell strings");
    spatial::CellPosition result;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto& item = cells.at(axis);
        if (!item.isString()) throw std::invalid_argument("Cell address must be a decimal string");
        const auto& text   = item.asString();
        const auto  parsed = std::from_chars(text.data(), text.data() + text.size(), result.cell[axis]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::invalid_argument("Cell address is outside int64 or malformed");
    }
    result.fraction = triple(required(value, "fraction"));
    for (double fraction : result.fraction)
        if (fraction < -0.5 || fraction >= 0.5) throw std::invalid_argument("Noncanonical cell fraction");
    return result;
}
Result<SphereFrame> prepare(const Value& input, int limit) {
    try {
        if (limit < 0) return Result<SphereFrame>::failure(invalid("Negative sphere budget"));
        keys(input, {"camera", "view", "spheres"});
        const auto  camera    = position(required(input, "camera"));
        const auto& viewValue = required(input, "view");
        keys(viewValue, {"right", "up", "forward", "tanHalfVerticalFov", "aspect", "cellExponent"});
        AngularView view;
        view.right              = triple(required(viewValue, "right"));
        view.up                 = triple(required(viewValue, "up"));
        view.forward            = triple(required(viewValue, "forward"));
        view.tanHalfVerticalFov = number(required(viewValue, "tanHalfVerticalFov"));
        view.aspect             = number(required(viewValue, "aspect"));
        view.cellExponent       = integer(required(viewValue, "cellExponent"));
        const auto& spheres     = required(input, "spheres");
        if (!spheres.isArray()) return Result<SphereFrame>::failure(invalid("Expected sphere array"));
        if (spheres.arraySize() > static_cast<std::size_t>(limit))
            return Result<SphereFrame>::failure(invalid("Sphere input exceeds explicit budget"));
        std::vector<spatial::CellPosition> centers;
        std::vector<SphereAppearance>      appearances;
        centers.reserve(spheres.arraySize());
        appearances.reserve(spheres.arraySize());
        for (std::size_t i = 0; i < spheres.arraySize(); ++i) {
            const auto& sphere = spheres.at(i);
            keys(sphere, {"center", "radiusMantissa", "radiusExponent", "color"});
            centers.push_back(position(required(sphere, "center")));
            SphereAppearance appearance;
            appearance.radiusMantissa = number(required(sphere, "radiusMantissa"));
            appearance.radiusExponent = integer(required(sphere, "radiusExponent"));
            const auto color          = triple(required(sphere, "color"));
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (color[axis] < 0 || color[axis] > 1) throw std::invalid_argument("Color outside [0,1]");
                appearance.color[axis] = static_cast<float>(color[axis]);
            }
            appearances.push_back(appearance);
        }
        return SphereFrame::build(centers, appearances, camera, view, static_cast<std::size_t>(limit));
    } catch (const std::invalid_argument& error) {
        return Result<SphereFrame>::failure(invalid(error.what()));
    } catch (const std::bad_alloc&) {
        return Result<SphereFrame>::failure(Diagnostic::error(DiagnosticCode::Failed, "Frame allocation failed"));
    }
}
template <class T>
ssq::Table owned(HSQUIRRELVM vm, std::unique_ptr<T> value) {
    auto instance = script::makeOwnedSquirrelInstance<T>(vm, std::move(value));
    if (!instance.ok()) return script::projectStatusResult(vm, instance.status());
    return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
}
}  // namespace
Module_IMPL(MultiscaleView, new MultiscaleView());
void MultiscaleView::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("newSphereView", [vm](MultiscaleView*, ssq::Object provider) {
        if (provider.getType() != ssq::Type::INSTANCE ||
            provider.getTypeTag() != script::detail::squirrelTypeHash<Graphics*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected graphics provider instance")));
        Graphics* graphics = nullptr;
        try {
            graphics = provider.to<Graphics*>();
        } catch (const ssq::TypeException&) {
            return script::projectStatusResult(vm, Status::failure(invalid("Expected graphics provider")));
        }
        if (!graphics) return script::projectStatusResult(vm, Status::failure(invalid("Missing graphics provider")));
        auto result = SphereView::build(*graphics);
        if (!result.ok()) return script::projectStatusResult(vm, result.status());
        return owned(vm, std::move(result).takeValue());
    });
    cls.addFunc("prepareSphereFrame", [vm](MultiscaleView*, ssq::Object input, ssq::Object budget) {
        // Bound conversion before allocating the native snapshot; this is a rebuild operation, not a draw lookup.
        if (budget.getType() != ssq::Type::INTEGER)
            return script::projectStatusResult(vm, Status::failure(invalid("Expected integer sphere budget")));
        const auto requested = budget.toInt();
        if (requested < 1 || requested > 1000000)
            return script::projectStatusResult(vm, Status::failure(invalid("Sphere budget outside [1,1000000]")));
        const int limit     = static_cast<int>(requested);
        auto      converted = script::valueFromSquirrel(
            input, {8, 64 + static_cast<std::size_t>(limit) * 32, "graphics.multiscale.script"});
        if (!converted.ok()) return script::projectStatusResult(vm, converted.status());
        auto result = prepare(converted.value(), limit);
        if (!result.ok()) return script::projectStatusResult(vm, result.status());
        return owned(vm, std::make_unique<SphereFrame>(std::move(result).takeValue()));
    });
    cls.addFunc("prepareSphereBatch", [vm](MultiscaleView*, ssq::Object provider, ssq::Object input) {
        if (provider.getType() != ssq::Type::INSTANCE ||
            provider.getTypeTag() != script::detail::squirrelTypeHash<Graphics*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected graphics provider instance")));
        if (input.getType() != ssq::Type::INSTANCE ||
            input.getTypeTag() != script::detail::squirrelTypeHash<SphereFrame*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere frame instance")));
        Graphics*    graphics = nullptr;
        SphereFrame* frame    = nullptr;
        try {
            graphics = provider.to<Graphics*>();
            frame    = input.to<SphereFrame*>();
        } catch (const ssq::TypeException&) {
            return script::projectStatusResult(vm,
                                               Status::failure(invalid("Expected graphics provider and sphere frame")));
        }
        if (!graphics || !frame)
            return script::projectStatusResult(vm,
                                               Status::failure(invalid("Missing graphics provider or sphere frame")));
        auto result = SphereBatch::prepare(*graphics, *frame);
        if (!result.ok()) return script::projectStatusResult(vm, result.status());
        return owned(vm, std::move(result).takeValue());
    });
}
void MultiscaleView::expose(ssq::Table& table) {
    auto cls = table.addClass(name, MultiscaleView::create, false);
    expose(cls);
    table.addAbstractClass<SphereFrame>("MultiscaleSphereFrame");
    auto       view = table.addAbstractClass<SphereView>("MultiscaleSphereView");
    const auto vm   = table.getHandle();
    view.addFunc("draw", [vm](SphereView* self, ssq::Object input) {
        SQUserPointer receiverType = nullptr;
        if (SQ_FAILED(sq_gettypetag(vm, 1, &receiverType)) ||
            reinterpret_cast<std::size_t>(receiverType) != script::detail::squirrelTypeHash<SphereView*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere view receiver")));
        if (input.getType() != ssq::Type::INSTANCE ||
            input.getTypeTag() != script::detail::squirrelTypeHash<SphereFrame*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere frame instance")));
        SphereFrame* frame = nullptr;
        try {
            frame = input.to<SphereFrame*>();
        } catch (const ssq::TypeException&) {
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere frame")));
        }
        if (!self || !frame) return script::projectStatusResult(vm, Status::failure(invalid("Missing view or frame")));
        return script::projectResult(vm, self->draw(*frame));
    });
    auto batch = table.addAbstractClass<SphereBatch>("MultiscaleSphereBatch");
    batch.addFunc("update", [vm](SphereBatch* self, ssq::Object input) {
        SQUserPointer receiverType = nullptr;
        if (SQ_FAILED(sq_gettypetag(vm, 1, &receiverType)) ||
            reinterpret_cast<std::size_t>(receiverType) != script::detail::squirrelTypeHash<SphereBatch*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere batch receiver")));
        if (input.getType() != ssq::Type::INSTANCE ||
            input.getTypeTag() != script::detail::squirrelTypeHash<SphereFrame*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere frame instance")));
        SphereFrame* frame = nullptr;
        try {
            frame = input.to<SphereFrame*>();
        } catch (const ssq::TypeException&) {
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere frame")));
        }
        if (!self || !frame) return script::projectStatusResult(vm, Status::failure(invalid("Missing batch or frame")));
        return script::projectResult(vm, self->update(*frame));
    });
    batch.addFunc("draw", [vm](SphereBatch* self) {
        SQUserPointer receiverType = nullptr;
        if (SQ_FAILED(sq_gettypetag(vm, 1, &receiverType)) ||
            reinterpret_cast<std::size_t>(receiverType) != script::detail::squirrelTypeHash<SphereBatch*>())
            return script::projectStatusResult(vm, Status::failure(invalid("Expected sphere batch receiver")));
        if (!self) return script::projectStatusResult(vm, Status::failure(invalid("Missing sphere batch")));
        return script::projectResult(vm, self->draw());
    });
}
}  // namespace eve::graphics::multiscale
