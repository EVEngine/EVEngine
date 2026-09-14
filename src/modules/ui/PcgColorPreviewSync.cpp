#include "ui/PcgColorPreviewSync.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <cmath>
namespace eve::ui {
Result<void> PcgColorPreviewSync::sync(float r, float g, float b, float a) {
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "source color must be finite", "ui.pcgColorPreview.sync"));
    if (r == 0.f && g == 0.f && b == 0.f) {
        red_ = green_ = blue_ = 0.5f; alpha_ = 1.f;
    } else {
        red_ = r * 2.5f; green_ = g * 2.5f; blue_ = b * 2.5f; alpha_ = a * 2.5f;
    }
    return Result<void>::success();
}
void exposePcgColorPreviewSyncBindings(ssq::Table& table) {
    auto cls = table.addClass("PcgColorPreviewSync", ssq::Class::Ctor<PcgColorPreviewSync()>());
    auto vm = table.getHandle();
    cls.addFunc("sync", [vm](PcgColorPreviewSync* self, float r, float g, float b, float a) {
        return eve::script::projectResult(vm, self->sync(r, g, b, a));
    });
    cls.addFunc("getRed", [](const PcgColorPreviewSync* self) { return self->getRed(); });
    cls.addFunc("getGreen", [](const PcgColorPreviewSync* self) { return self->getGreen(); });
    cls.addFunc("getBlue", [](const PcgColorPreviewSync* self) { return self->getBlue(); });
    cls.addFunc("getAlpha", [](const PcgColorPreviewSync* self) { return self->getAlpha(); });
}
}
