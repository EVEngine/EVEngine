#pragma once
#include <cstdint>

namespace ssq
{
    class VM;
	class Table;
	class Class;
} // namespace ssq

#define SSQ_REG \
    static void expose(ssq::Table& table); \
	static void expose(ssq::Class& vm);


namespace eve
{

class Module {
public:
    enum class ModuleType : uint32_t
    {
        event = 1,
        filesystem,
        graphics,
        image,
        math,
        network,
        system,
        thread,
        window,
    };


    virtual ~Module() {}

    virtual uint32_t getModuleType() const = 0;
    // virtual const char *getName() const = 0;

    static void expose(ssq::VM& vm);
};


} // namespace eve
