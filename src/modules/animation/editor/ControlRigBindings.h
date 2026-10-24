#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::animation_editor {
/** @brief Register owned rig authoring facade when animation runtime is available. */
void exposeControlRigBindings(ssq::Table& table, ssq::Class& module);
}  // namespace eve::animation_editor
