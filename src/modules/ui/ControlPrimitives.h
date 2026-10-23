#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::ui::controls {

/** @brief Result of drawing one immediate control in the current frame. */
enum class ControlEdit : uint8_t { Unchanged = 0, Changed };

/** @brief Shared immediate renderer for retained and EditorHost buttons. */
inline ControlEdit button(const char *label, float width = 0.f, float height = 0.f) {
    const bool changed = (width > 0.f || height > 0.f) ? ImGui::Button(label, ImVec2(width, height))
                                                       /** @brief Button. */
                                                       : ImGui::Button(label);
    return changed ? ControlEdit::Changed : ControlEdit::Unchanged;
}

/** @brief Shared immediate renderer for retained and EditorHost checkboxes. */
inline ControlEdit checkbox(const char *label, bool &value) {
    /** @brief Checkbox. */
    return ImGui::Checkbox(label, &value) ? ControlEdit::Changed : ControlEdit::Unchanged;
}

/** @brief Shared immediate renderer for scalar sliders. */
inline ControlEdit slider(const char *label, float &value, float minimum, float maximum,
                          const char *format = "%.3f") {
    /** @brief Slider float. */
    return ImGui::SliderFloat(label, &value, minimum, maximum, format) ? ControlEdit::Changed
                                                                       : ControlEdit::Unchanged;
}

/** @brief Shared immediate renderer for indexed combo boxes. */
inline ControlEdit combo(const char *label, int &selected,
                         const std::vector<const char *> &items) {
    const bool changed = !items.empty() &&
                         /** @brief Combo. */
                         ImGui::Combo(label, &selected, items.data(),
                                      static_cast<int>(items.size()));
    return changed ? ControlEdit::Changed : ControlEdit::Unchanged;
}

/** @brief Render a keyboard-accessible text link without launching its target. */
inline ControlEdit textLink(const char *label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
    const bool   clicked = ImGui::Button(label);
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, hi.y - 1.f), ImVec2(hi.x, hi.y - 1.f),
                                        ImGui::GetColorU32(ImGuiCol_Text));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked ? ControlEdit::Changed : ControlEdit::Unchanged;
}

namespace detail {
/**
 * @brief Grows an owning string for ImGui's synchronous resize callback.
 * @param data Borrowed non-null callback data owned by ImGui for this call only.
 * @ownership The callback stores no pointer after returning.
 * @lifetime data and UserData are valid only for the callback duration.
 */
inline int resizeInput(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
    auto *value = static_cast<std::string *>(data->UserData);
    value->resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = value->data();
    return 0;
}
}  // namespace detail

/** @brief UTF-8 owning text editor with automatic buffer growth. */
inline ControlEdit inputText(const char *label, std::string &value, bool multiline = false,
                             ImVec2 multilineSize = ImVec2(0.f, 96.f), bool readOnly = false, const char *hint = "") {
    if (value.capacity() == 0) value.reserve(64);
    const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_CallbackResize | (readOnly ? ImGuiInputTextFlags_ReadOnly : 0);
    const bool changed = multiline ? ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, multilineSize,
                                                               flags, detail::resizeInput, &value)
                                   /** @brief Input text. */
                                   : ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, flags,
                                                              detail::resizeInput, &value);
    value.resize(std::char_traits<char>::length(value.c_str()));
    return changed ? ControlEdit::Changed : ControlEdit::Unchanged;
}

/** @brief UTF-8 owning single-line editor with placeholder text and automatic growth. */
inline ControlEdit inputTextWithHint(const char *label, const char *hint, std::string &value) {
    if (value.capacity() == 0) value.reserve(64);
    const bool changed = ImGui::InputTextWithHint(
        label, hint, value.data(), value.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
        detail::resizeInput, &value);
    value.resize(std::char_traits<char>::length(value.c_str()));
    return changed ? ControlEdit::Changed : ControlEdit::Unchanged;
}

}  // namespace eve::ui::controls
