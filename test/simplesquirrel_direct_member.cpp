#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
struct MemberBindingProbe {
    inline static int live  = 0;
    int               value = 0;
    std::string       text  = "probe";
    MemberBindingProbe() { ++live; }
    ~MemberBindingProbe() { --live; }
    void                         set(int input) { value = input; }
    int                          get() const { return value; }
    int                          add(int input) { return value += input; }
    const std::string&           label() const { return text; }
    std::string                  echo(const std::string& input) const { return input + text; }
    std::vector<int>             values() const { return {value, value + 1}; }
    std::tuple<int, std::string> pair() const { return {value, text}; }
    int                          fail() const { throw std::runtime_error("member failure"); }
};
}  // namespace

TEST_CASE("SimpleSquirrel.DirectMemberConversionsAndLifetime") {
    REQUIRE(MemberBindingProbe::live == 0);
    {
        ssq::VM vm(2048, ssq::Libs::ALL);
        auto    cls = vm.addClass("MemberProbe", ssq::Class::Ctor<MemberBindingProbe()>());
        cls.addFunc("set", &MemberBindingProbe::set);
        cls.addFunc("get", &MemberBindingProbe::get);
        cls.addFunc("add", &MemberBindingProbe::add);
        cls.addFunc("label", &MemberBindingProbe::label);
        cls.addFunc("echo", &MemberBindingProbe::echo);
        cls.addFunc("values", &MemberBindingProbe::values);
        cls.addFunc("pair", &MemberBindingProbe::pair);
        cls.addFunc("fail", &MemberBindingProbe::fail);
        // Capturing callables still use the upstream std::function overload.
        const int offset = 3;
        cls.addFunc("captured", [offset](MemberBindingProbe* p) { return p->value + offset; });
        cls.addFunc("stateless", [](MemberBindingProbe* p) { return p->value + 2; });
        cls.addFunc("setStateless", [](MemberBindingProbe* p, int value) { p->value = value; });
        cls.addFunc("statelessValues", [](MemberBindingProbe* p) { return std::vector<int>{p->value, 8}; });
        cls.addFunc("statelessPair",
                    [](MemberBindingProbe* p) { return std::tuple<int, std::string>{p->value, "pair"}; });
        cls.addFunc("statelessFail", [](MemberBindingProbe*) -> int { throw std::runtime_error("stateless failure"); });
        vm.addFunc("sum", +[](int a, int b) { return a + b; });
        vm.addFunc("zero", []() { return 0; });
        vm.run(vm.compileSource(R"(
            function verifyMembers() {
                local p = MemberProbe();
                p.set(4);
                if (p.get() != 4 || p.add(2) != 6 || p.get() != 6) return false;
                if (p.label() != "probe" || p.echo("x") != "xprobe") return false;
                local values = p.values();
                local pair = p.pair();
                if (values[0] != 6 || values[1] != 7 || pair[0] != 6 || pair[1] != "probe") return false;
                if (p.captured() != 9) return false;
                if (p.stateless() != 8 || sum(3, 4) != 7 || zero() != 0) return false;
                p.setStateless(9);
                if (p.get() != 9) return false;
                local sv = p.statelessValues();
                local sp = p.statelessPair();
                if (sv[0] != 9 || sv[1] != 8 || sp[0] != 9 || sp[1] != "pair") return false;
                p.set(6);
                local caught = false;
                try { p.statelessFail(); } catch (e) { caught = (e.find("stateless failure") != null); }
                if (!caught) return false;
                caught = false;
                try { p.setStateless("wrong"); } catch (e) { caught = true; }
                if (!caught) return false;
                caught = false;
                try { sum(1); } catch (e) { caught = true; }
                if (!caught) return false;
                caught = false;
                try { p.fail(); } catch (e) { caught = (e.find("member failure") != null); }
                if (!caught) return false;
                caught = false;
                try { p.set("wrong"); } catch (e) { caught = true; }
                if (!caught) return false;
                caught = false;
                try { p.get(1); } catch (e) { caught = true; }
                return caught;
            }
        )"));
        CHECK(vm.callFunc(vm.findFunc("verifyMembers"), vm).toBool());
    }
    CHECK_EQ(MemberBindingProbe::live, 0);
}
