#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ssq {
class VM;
class Table;
class Class;
}  // namespace ssq

#define SSQ_REG                            \
    static void expose(ssq::Table& table); \
    static void expose(ssq::Class& vm);

#define Module_REG \
    SSQ_REG \
    virtual std::string getName() const { return name; } \
    static const char* name

#define Module_IMPL(ModuleName) \
    const char* ModuleName::name = #ModuleName

namespace eve {

class Module {
public:
    virtual ~Module() {}
    virtual std::string getName() const = 0;

    template <typename T>
    static T* getInstance() {
        return static_cast<T*>(registered_modules[T::name]);
    }

protected:
    static std::unordered_map<std::string, Module*> registered_modules;
};

}  // namespace eve
