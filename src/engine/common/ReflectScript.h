#pragma once

#include "common/Export.h"

namespace ssq {
class Table;
}

namespace eve {

class Runtime;

/**
 * @brief Exposes the Runtime reflection API to scripts as `eve.reflect`.
 *
 * Adds a `reflect` sub-table to the `eve` table with class/instance
 * introspection, typed property read/write and array/table member editing:
 *
 * @code
 * local info = eve.reflect.classInfo("Hero")     // class metadata table
 * local hero = eve.reflect.createInstance("Hero")
 * eve.reflect.write(hero, "hp", 42.5)
 * local hp = eve.reflect.read(hero, "hp")        // 42.5
 * local members = eve.reflect.inspect(hero)      // member metadata + live values
 * @endcode
 *
 * Scalar reads/writes keep the slot's own type (bool stays bool, integer
 * stays integer, ...), mirroring `Runtime::writeProperty`; array, table and
 * instance slots are read as their live script objects. `classInfo()` returns
 * null for an unknown class, and every mutator returns false instead of
 * throwing when the member is missing, is a method, or is not editable.
 *
 * @param runtime Runtime whose reflection data the bindings operate on.
 * @param eveTable Root `eve` table to add the `reflect` sub-table to.
 */
EVENGINE_API void exposeReflection(Runtime& runtime, ssq::Table eveTable);

}  // namespace eve
