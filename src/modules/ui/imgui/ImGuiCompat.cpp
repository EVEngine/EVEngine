#include <imgui.h>

// Some installed ImGui builds include ShowMetricsWindow from imgui.cpp but omit
// imgui_demo.cpp, where this helper normally lives. The metrics window is not part
// of EVEngine's runtime UI, so provide the missing no-op to keep such static builds
// linkable. The source-built EVEngine configuration also omits imgui_demo.cpp.
namespace ImGui {
void ShowFontAtlas(ImFontAtlas *) {}
}  // namespace ImGui
