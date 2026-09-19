/**
 * @file SquirrelOwnership.cpp
 * @brief Out-of-line core of the owned-Squirrel-instance factory.
 *
 * makeOwnedSquirrelInstance<T> is instantiated once per wrapper type (66 call
 * sites across 28 files), and its body was ~45 lines of stack discipline,
 * instance creation, typing, rooting and exception handling that has nothing to
 * do with T. Emitting it here means one copy instead of one per wrapper.
 */

#include "common/SquirrelOwnership.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <exception>
#include <string>

namespace eve::script::detail {

Diagnostic ownedInstanceArgumentDiagnostic() {
    return Diagnostic::error(DiagnosticCode::InvalidArgument,
                             "owned Squirrel instance requires a VM and non-null object", {}, {},
                             "squirrel.ownership");
}

Result<ssq::Object> makeOwnedSquirrelInstanceRaw(HSQUIRRELVM vm, void* object, SquirrelReleaseHook releaseHook,
                                                 OwnedInstanceDestroy destroy, std::size_t typeHash) {
    const SQInteger top         = sq_gettop(vm);
    bool            transferred = false;
    try {
        const HSQOBJECT& classObject = ssq::detail::getClassObj(vm, typeHash);
        sq_pushobject(vm, classObject);
        if (SQ_FAILED(sq_createinstance(vm, -1)))
            throw ssq::RuntimeException("failed to create owned Squirrel instance");
        sq_remove(vm, -2);
        if (SQ_FAILED(sq_setinstanceup(vm, -1, static_cast<SQUserPointer>(object))))
            throw ssq::RuntimeException("failed to attach owned Squirrel instance");
        sq_settypetag(vm, -1, reinterpret_cast<SQUserPointer>(typeHash));
        sq_setreleasehook(vm, -1, releaseHook);
        // From here the live instance owns the object: its release hook is
        // installed, so a later failure must not destroy it here.
        transferred = true;

        ssq::Object result(vm);
        if (SQ_FAILED(sq_getstackobj(vm, -1, &result.getRaw())))
            throw ssq::RuntimeException("failed to root owned Squirrel instance");
        sq_addref(vm, &result.getRaw());
        sq_settop(vm, top);
        return Result<ssq::Object>::success(std::move(result));
    } catch (const std::exception& error) {
        sq_settop(vm, top);
        if (!transferred) {
            destroy(object);
            return Result<ssq::Object>::failure(Diagnostic::error(
                DiagnosticCode::Failed, std::string("owned Squirrel instance creation failed: ") + error.what(), {}, {},
                "squirrel.ownership"));
        }
        return Result<ssq::Object>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("owned Squirrel instance rooting failed: ") + error.what(), {}, {},
            "squirrel.ownership"));
    } catch (...) {
        sq_settop(vm, top);
        if (!transferred) destroy(object);
        return Result<ssq::Object>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "owned Squirrel instance creation failed", {}, {}, "squirrel.ownership"));
    }
}

}  // namespace eve::script::detail
