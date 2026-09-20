#pragma once

namespace ssq {
class Class;
}

namespace eve::procgen {

/**
 * @brief Add the synchronous script-generator host API to the Procgen facade.
 * @param cls Procgen Squirrel class owned by the active VM.
 * @thread The active Squirrel VM's owning thread only.
 * @reentrancy A generator may run another system, but the same system cannot
 *             recursively rebuild itself.
 */
void exposeScriptGeneratorHost(ssq::Class& cls);

}  // namespace eve::procgen
