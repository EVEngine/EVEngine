#pragma once

#include <functional>
#include <new>
#include <type_traits>
#include <utility>

// Included after upstream binding.hpp by the generated header overlay. Only
// Member pointers and stateless callables use VM-owned pointer storage.
// Capturing callables retain upstream ownership and conversion rules.
namespace ssq::detail {

template <typename F>
constexpr bool directFunctionCompatible = requires(const F& function) {
    requires std::is_pointer_v<decltype(+function)> && std::is_function_v<std::remove_pointer_t<decltype(+function)>>;
};

template <typename T>
struct DirectMemberResult {
    static SQInteger pushResult(HSQUIRRELVM vm, T&& result) {
        push(vm, std::forward<T>(result));
        return 1;
    }
};

template <typename... T>
struct DirectMemberResult<std::tuple<T...>> {
    static SQInteger pushResult(HSQUIRRELVM vm, std::tuple<T...>&& result) {
        Array array(vm, std::move(result));
        push(vm, array);
        return 1;
    }
};

template <typename T>
struct DirectMemberResult<std::vector<T>> {
    static SQInteger pushResult(HSQUIRRELVM vm, std::vector<T>&& result) {
        Array array(vm, std::move(result));
        push(vm, array);
        return 1;
    }
};

template <typename Member>
struct DirectMemberTraits;

template <typename Return, typename Object, typename... Args>
struct DirectMemberTraits<Return (Object::*)(Args...)> {
    using Result    = Return;
    using Class     = Object;
    using Arguments = std::tuple<Args...>;
};

template <typename Return, typename Object, typename... Args>
struct DirectMemberTraits<Return (Object::*)(Args...) const> : DirectMemberTraits<Return (Object::*)(Args...)> {};

template <typename Member>
struct DirectMemberCall {
    using Traits    = DirectMemberTraits<Member>;
    using Return    = typename Traits::Result;
    using Object    = typename Traits::Class;
    using Arguments = typename Traits::Arguments;
    using Indices   = std::make_index_sequence<std::tuple_size_v<Arguments>>;
    template <std::size_t... I>
    static Return invoke(HSQUIRRELVM vm, Member member, std::index_sequence<I...>) {
        auto* object = pop<Object*>(vm, 1);
        return (object->*member)(pop<std::remove_reference_t<std::tuple_element_t<I, Arguments>>>(vm, I + 2)...);
    }

    static SQInteger call(HSQUIRRELVM vm) {
        try {
            Member* member = nullptr;
            sq_getuserdata(vm, -1, reinterpret_cast<void**>(&member), nullptr);
            if constexpr (std::is_void_v<Return>) {
                invoke(vm, *member, Indices{});
                return 0;
            } else {
                return DirectMemberResult<Return>::pushResult(vm, invoke(vm, *member, Indices{}));
            }
        } catch (std::exception& error) {
            return sq_throwerror(vm, error.what());
        }
    }

    template <std::size_t... I>
    static void packParams(char* params, std::index_sequence<I...>) {
        paramPacker<Object*, std::tuple_element_t<I, Arguments>...>(params);
    }

    static void bind(HSQUIRRELVM vm, const char* name, Member member, bool isStatic) {
        static_assert(std::is_trivially_destructible_v<Member>);
        sq_pushstring(vm, name, -1);
        // Squirrel owns this trivially destructible member pointer until the
        // closure is collected. No std::function allocation/release hook.
        new (sq_newuserdata(vm, sizeof(Member))) Member(member);
        char params[std::tuple_size_v<Arguments> + 2];
        packParams(params, Indices{});
        sq_newclosure(vm, &call, 1);
        sq_setparamscheck(vm, std::tuple_size_v<Arguments> + 1, params);
        if (SQ_FAILED(sq_newslot(vm, -3, isStatic))) throw TypeException("Failed to bind member function");
    }
};

template <typename Member>
struct DirectMember : DirectMemberCall<Member> {};

template <typename Function, bool Member>
struct DirectFunction;

template <bool Member, typename Return, typename... Args>
struct DirectFunction<Return (*)(Args...), Member> {
    using Function = Return (*)(Args...);
    template <std::size_t... I>
    static Return invoke(HSQUIRRELVM vm, Function function, std::index_sequence<I...>) {
        if (function == nullptr) throw std::bad_function_call();
        return function(pop<std::remove_reference_t<Args>>(vm, I + (Member ? 1 : 2))...);
    }

    static SQInteger call(HSQUIRRELVM vm) {
        try {
            Function* function = nullptr;
            sq_getuserdata(vm, -1, reinterpret_cast<void**>(&function), nullptr);
            if constexpr (std::is_void_v<Return>) {
                invoke(vm, *function, std::index_sequence_for<Args...>{});
                return 0;
            } else {
                return DirectMemberResult<Return>::pushResult(
                    vm, invoke(vm, *function, std::index_sequence_for<Args...>{}));
            }
        } catch (std::exception& error) {
            return sq_throwerror(vm, error.what());
        }
    }

    static void bind(HSQUIRRELVM vm, const char* name, Function function, bool isStatic = false) {
        sq_pushstring(vm, name, -1);
        new (sq_newuserdata(vm, sizeof(Function))) Function(function);
        char params[sizeof...(Args) + 2];
        if constexpr (Member) {
            paramPacker<Args...>(params);
        } else {
            paramPacker<void, Args...>(params);
        }
        sq_newclosure(vm, &call, 1);
        sq_setparamscheck(vm, sizeof...(Args) + (Member ? 0 : 1), params);
        if (SQ_FAILED(sq_newslot(vm, -3, isStatic))) throw TypeException("Failed to bind function");
    }
};

template <bool Member, typename Return, typename... Args>
struct DirectFunction<Return (*)(Args...) noexcept, Member> : DirectFunction<Return (*)(Args...), Member> {};

}  // namespace ssq::detail
