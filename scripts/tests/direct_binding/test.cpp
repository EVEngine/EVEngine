#include "common/ScriptBinding.h"
#include <stdexcept>
#include <iostream>
struct Probe {
    int value = 3;
    int add(int n) { return value + n; }
    int sub(int n) { return value - n; }
    int get() const { return value; }
    void set(int n) { value = n; }
    int fail() { throw std::runtime_error("binding failure"); }
};
int twice(Probe* p, int n) { return p->value + 2*n; }
int thrice(Probe* p, int n) { return p->value + 3*n; }
std::vector<int> values(Probe*) { return {4, 5}; }
std::tuple<int, std::string> pair(Probe*) { return {6, "seven"}; }
int main() {
    try {
        for (bool direct : {false, true}) {
            ssq::VM vm(1024);
            auto cls = vm.addClass("Probe", ssq::Class::Ctor<Probe()>());
            auto bind = [&](const char* name, auto pointer) {
                const auto top = sq_gettop(cls.getHandle());
                if (direct) eve::script::bindMethod(cls, name, pointer);
                else cls.addFunc(name, pointer);
                if (sq_gettop(cls.getHandle()) != top) throw std::runtime_error("stack leak");
            };
            bind("add", &Probe::add); bind("sub", &Probe::sub);
            bind("get", &Probe::get); bind("set", &Probe::set);
            bind("fail", &Probe::fail);
            bind("twice", &twice); bind("thrice", &thrice);
            bind("values", &values); bind("pair", &pair);
            cls.addFunc("captured", [offset=9](Probe*) { return offset; });
            vm.run(vm.compileSource(R"(
                local p = Probe();
                if (p.add(2)!=5 || p.sub(2)!=1 || p.twice(2)!=7 || p.thrice(2)!=9) throw "dispatch";
                p.set(12); if(p.get()!=12 || p.captured()!=9) throw "state";
                if(p.values()[1]!=5 || p.pair()[1]!="seven") throw "array return";
                local errors=0;
                try {p.add();} catch(e) {errors++;}
                try {p.add("bad");} catch(e) {errors++;}
                try {p.fail();} catch(e) {if(e=="binding failure") errors++;}
                if(errors!=3) throw "error propagation";
            )"));
            if (direct) {
                // Force a registration failure through an invalid parameter mask.
                const auto top = sq_gettop(cls.getHandle());
                bool rejected = false;
                auto pointer = &Probe::get;
                try {
                    eve::script::detail::bindPointer(cls, "invalid", &pointer, sizeof(pointer),
                        &eve::script::detail::invokePointer<decltype(pointer)>, 1, "?", false);
                }
                catch (const ssq::TypeException&) { rejected = true; }
                if (!rejected || sq_gettop(cls.getHandle()) != top)
                    throw std::runtime_error("registration failure/stack restoration");
            }
        }
        std::cout << "PASS: baseline/direct VM dispatch, conversion, state, errors, stack, captured fallback\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
