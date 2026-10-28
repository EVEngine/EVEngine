#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::scene_editor {
/** @brief Register owned, UI-neutral scene editing sessions in the supplied VM; owner-thread only. */
void exposeSceneEditorSessions(ssq::Table& table, ssq::Class& module);
}  // namespace eve::scene_editor
