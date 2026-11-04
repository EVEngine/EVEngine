#include "common/ScriptBinding.h"

namespace eve::script::detail {

void bindPointer(ssq::Class& cls, const char* name, const void* pointer, size_t size, SQFUNCTION invoke,
                 SQInteger arity, const char* params, bool isStatic) {
    const auto vm = cls.getHandle();
    if (!vm) throw ssq::RuntimeException("VM is not initialised");
    const auto top = sq_gettop(vm);
    struct RestoreStack {
        HSQUIRRELVM vm;
        SQInteger   top;
        ~RestoreStack() { sq_settop(vm, top); }
    } restore{vm, top};

    sq_pushobject(vm, cls.getRaw());
    sq_pushstring(vm, name, -1);
    auto* data = sq_newuserdata(vm, size);
    std::memcpy(data, pointer, size);
    // Pointers have no owned state/destructor, so no release hook is needed.
    sq_newclosure(vm, invoke, 1);
    if (SQ_FAILED(sq_setparamscheck(vm, arity, params)) || SQ_FAILED(sq_newslot(vm, -3, isStatic)))
        throw ssq::TypeException("Failed to bind member function");
}

}  // namespace eve::script::detail
