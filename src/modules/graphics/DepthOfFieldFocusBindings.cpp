#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "graphics/DepthOfFieldFocus.h"
namespace eve::graphics {
void exposeDepthOfFieldFocusBindings(ssq::Table& table) {
    auto s = table.addClass("DepthOfFieldFocusSettings", ssq::Class::Ctor<DepthOfFieldFocusSettings()>());
    s.addVar("tracking", &DepthOfFieldFocusSettings::tracking);
    s.addVar("enabled", &DepthOfFieldFocusSettings::enabled);
    s.addVar("interactWithPlayer", &DepthOfFieldFocusSettings::interactWithPlayer);
    s.addVar("focusOffset", &DepthOfFieldFocusSettings::focusOffset);
    s.addVar("maximumDistance", &DepthOfFieldFocusSettings::maximumDistance);
    s.addVar("aperture", &DepthOfFieldFocusSettings::aperture);
    s.addVar("focalLength", &DepthOfFieldFocusSettings::focalLength);
    s.addVar("response", &DepthOfFieldFocusSettings::response);
    auto i = table.addClass("DepthOfFieldFocusInput", ssq::Class::Ctor<DepthOfFieldFocusInput()>());
    i.addVar("hasRayHit", &DepthOfFieldFocusInput::hasRayHit);
    i.addVar("hitPlayer", &DepthOfFieldFocusInput::hitPlayer);
    i.addVar("rayHitDistance", &DepthOfFieldFocusInput::rayHitDistance);
    i.addVar("hasTarget", &DepthOfFieldFocusInput::hasTarget);
    i.addVar("targetDistance", &DepthOfFieldFocusInput::targetDistance);
    i.addVar("deltaSeconds", &DepthOfFieldFocusInput::deltaSeconds);
    auto st = table.addClass("DepthOfFieldFocusState", ssq::Class::Ctor<DepthOfFieldFocusState()>());
    st.addVar("initialized", &DepthOfFieldFocusState::initialized);
    st.addVar("maximumExceeded", &DepthOfFieldFocusState::maximumExceeded);
    st.addVar("focusDistance", &DepthOfFieldFocusState::focusDistance);
    auto o = table.addClass("DepthOfFieldFocusOutput", ssq::Class::Ctor<DepthOfFieldFocusOutput()>());
    o.addVar("active", &DepthOfFieldFocusOutput::active);
    o.addVar("focusDistance", &DepthOfFieldFocusOutput::focusDistance);
    o.addVar("aperture", &DepthOfFieldFocusOutput::aperture);
    o.addVar("focalLength", &DepthOfFieldFocusOutput::focalLength);
    table.addFunc("evaluateDepthOfFieldFocus",
                  [vm = table.getHandle()](DepthOfFieldFocusState* state, DepthOfFieldFocusOutput* out,
                                           DepthOfFieldFocusSettings* settings, DepthOfFieldFocusInput* input) {
                      return eve::script::projectResult(vm, evaluateDepthOfFieldFocus(state, out, settings, input));
                  });
}
}  // namespace eve::graphics
