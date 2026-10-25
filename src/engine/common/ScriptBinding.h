#pragma once

#include "common/Export.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <cstring>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

namespace eve::script {
namespace detail {

EVENGINE_API_FOUNDATION void bindPointer(ssq::Class& cls, const char* name, const void* pointer,
                             size_t size, SQFUNCTION invoke, SQInteger arity,
                             const char* params, bool isStatic);

template <class Pointer> struct Call;

template <class R, class... Args>
struct Call<R (*)(Args...)> {
    using Result = R;
    static constexpr size_t arity = sizeof...(Args);
    static void params(char* out) { ssq::detail::paramPacker<Args...>(out); }
    template <size_t... I>
    static R invoke(HSQUIRRELVM vm, R (*fn)(Args...), std::index_sequence<I...>) {
        return fn(ssq::detail::pop<std::remove_reference_t<Args>>(vm, I + 1)...);
    }
    static R invoke(HSQUIRRELVM vm, R (*fn)(Args...)) {
        return invoke(vm, fn, std::index_sequence_for<Args...>{});
    }
};

template <class Pointer, class R, class C, class... Args>
struct MemberCall {
    using Result = R;
    static constexpr size_t arity = sizeof...(Args) + 1;
    static void params(char* out) { ssq::detail::paramPacker<C*, Args...>(out); }
    template <size_t... I>
    static R invoke(HSQUIRRELVM vm, Pointer fn, std::index_sequence<I...>) {
        auto* self = ssq::detail::pop<C*>(vm, 1);
        return (self->*fn)(ssq::detail::pop<std::remove_reference_t<Args>>(vm, I + 2)...);
    }
    static R invoke(HSQUIRRELVM vm, Pointer fn) {
        return invoke(vm, fn, std::index_sequence_for<Args...>{});
    }
};

template <class R, class C, class... Args>
struct Call<R (C::*)(Args...)> : MemberCall<R (C::*)(Args...), R, C, Args...> {};
template <class R, class C, class... Args>
struct Call<R (C::*)(Args...) const> : MemberCall<R (C::*)(Args...) const, R, C, Args...> {};

// Keep simplesquirrel's vector/tuple-to-array return convention as well.
template <class R> struct PushResult {
    static void push(HSQUIRRELVM vm, const R& value) { ssq::detail::push(vm, value); }
};
template <class T, class Alloc> struct PushResult<std::vector<T, Alloc>> {
    static void push(HSQUIRRELVM vm, std::vector<T, Alloc> value) {
        ssq::Array array(vm, std::move(value));
        ssq::detail::push(vm, array);
    }
};
template <class... T> struct PushResult<std::tuple<T...>> {
    static void push(HSQUIRRELVM vm, std::tuple<T...> value) {
        ssq::Array array(vm, std::move(value));
        ssq::detail::push(vm, array);
    }
};

template <class Pointer>
SQInteger invokePointer(HSQUIRRELVM vm) {
    try {
        void* data = nullptr;
        if (SQ_FAILED(sq_getuserdata(vm, -1, &data, nullptr)) || !data)
            return sq_throwerror(vm, "Missing direct binding userdata");
        Pointer pointer;
        // Member pointers may be larger than void*. Copy their complete object
        // representation, without imposing alignment/lifetime assumptions on VM memory.
        std::memcpy(&pointer, data, sizeof(pointer));
        using R = typename Call<Pointer>::Result;
        if constexpr (std::is_void_v<R>) {
            Call<Pointer>::invoke(vm, pointer);
            return 0;
        } else {
            PushResult<R>::push(vm, Call<Pointer>::invoke(vm, pointer));
            return 1;
        }
    } catch (const std::exception& e) {
        return sq_throwerror(vm, e.what());
    }
}

}  // namespace detail

/**
 * @brief Binds a free/member function directly, without std::function or a heap wrapper.
 * @param cls Squirrel class receiving the method.
 * @param name Script method name (non-null).
 * @param pointer Non-null function pointer or const/non-const member function pointer.
 * Free functions receive the script receiver as their first argument, like Class::addFunc.
 * @param isStatic Whether to create a static class slot (same convention as Class::addFunc).
 * @throws ssq::RuntimeException If the class has no VM.
 * @throws ssq::TypeException If Squirrel rejects registration.
 * @ownership Borrows cls during registration; the VM owns the copied pointer userdata.
 * @nullable name and pointer must be non-null.
 * @lifetime The target code and receiver must remain alive for every invocation.
 * @thread Register and invoke only on the owning VM thread.
 * @reentrancy Registration does not invoke the target; callback reentrancy follows its contract.
 * Capturing callables and constructors continue to use the existing ssq API.
 */
template <class Pointer>
void bindMethod(ssq::Class& cls, const char* name, Pointer pointer, bool isStatic = false) {
    static_assert(std::is_trivially_copyable_v<Pointer>);
    using Call = detail::Call<Pointer>;
    char params[Call::arity + 1];
    Call::params(params);
    detail::bindPointer(cls, name, &pointer, sizeof(pointer), &detail::invokePointer<Pointer>,
                        Call::arity, params, isStatic);
}

}  // namespace eve::script
