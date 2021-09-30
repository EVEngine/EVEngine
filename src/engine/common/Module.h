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

namespace eve {

class Module {
public:
    virtual ~Module() {}
    virtual std::string getName() const = 0;

    template <typename T>
    T* getInstance() {
        return static_cast<T*>(registered_modules[T::name]);
    }

protected:
    static std::unordered_map<std::string, Module*> registered_modules;
};

}  // namespace eve
