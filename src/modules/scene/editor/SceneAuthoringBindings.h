#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::scene_editor {
/** @brief Bind read-only authoring queries; table and module are borrowed synchronously. */
void exposeSceneAuthoringQueries(ssq::Table& table, ssq::Class& module);
}  // namespace eve::scene_editor
