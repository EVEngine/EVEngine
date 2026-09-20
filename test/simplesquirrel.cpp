#include <cassert>
#include <iostream>
#include <map>
#include <string>

#include "ScriptTest.h"
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqtable.h"
#include "sqclass.h"

extern const char* simplesquirrel_content;
UnitSciptTest(SimpleSquirrelTest, simplesquirrel_content);

TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.ExportClass") { REQUIRE(callScript("exportClass")); }

class A {
public:
    SSQ_REG
    A() {
        data = new char[32];
        size = 0;
        std::cerr << "A()" << std::endl;
    }
    ~A() {
        delete[] data;
        std::cerr << "~A()" << std::endl;
    }
    void setString(std::string name) {
        if (name.size() < 32) {
            strncpy(data, name.c_str(), name.size());
            data[name.size()] = '\0';
            size              = static_cast<int>(name.size());
        }
    }
    char* data;
    int   size;
};

class B {
public:
    SSQ_REG
    B() { std::cerr << "B()" << std::endl; }
    ~B() { std::cerr << "~B()" << std::endl; }

    void          setA(ssq::Instance a) { this->a = a; }
    void          print() { std::cerr << a.to<A*>()->data << std::endl; }
    ssq::Instance a;
};

void A::expose(ssq::Table& table) {
    auto cls = table.addClass("A", ssq::Class::Ctor<A()>());
    expose(cls);
}

void A::expose(ssq::Class& cls) { cls.addFunc("setString", &A::setString); }

void B::expose(ssq::Table& table) {
    auto cls = table.addClass("B", ssq::Class::Ctor<B()>());
    expose(cls);
}

void B::expose(ssq::Class& cls) {
    cls.addFunc("setA", &B::setA);
    cls.addFunc("print", &B::print);
}

TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.RefTest") {
    A::expose(vm);
    B::expose(vm);
    REQUIRE(callScript("refTest"));
}

#if !(defined(__APPLE__) && defined(NDEBUG))
TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.TestDefClass") {
    auto root = vm.getRaw();
    CHECK_EQ(sq_type(root), OT_TABLE);
    auto rt = _table(root);
    for (SQInteger i = 0; i < rt->_numofnodes; i++) {
        if (sq_type(rt->_nodes[i].key) != OT_NULL) {
            auto p = rt->_nodes[i].val;
            if (sq_type(p) == OT_CLASS) {
                printf("%s ", _string(rt->_nodes[i].key)->_val);
            }
        }
    }
    printf("\n");

    ssq::Class cls = vm.findClass("Test1");
    auto o = cls.getRaw();
    CHECK_EQ(o._type, OT_CLASS);
    auto pclass = o._unVal.pClass;

    auto t = pclass->_members;
    for (SQInteger i = 0; i < t->_numofnodes; i++) {
        if (sq_type(t->_nodes[i].key) != OT_NULL) {
            printf("%s ", _string(t->_nodes[i].key)->_val);
            auto p = t->_nodes[i].val;
            if (sq_type(p) == OT_INTEGER) {
                if (_isfield(p)) { printf("field %d\n", static_cast<int>(_member_idx(p))); }
                if (_ismethod(p)) { printf("method %d\n", static_cast<int>(_member_idx(p))); }
            }
        }
    }

    ssq::Object obj = cls.find("attr1");
    ssq::Class attr = obj.toClass();

    REQUIRE(callScript("testDefClass"));
}
#endif


TEST_CASE("SimpleSquirrelTest.PerformInteration") {
    ssq::VM vm(128);
    auto    script = vm.compileSource(R"(
        class Attr {}
        class Test1 {
            attr1 = Attr;
            attr2 = null;
            constructor(a, b) { attr1 = a; attr2 = b; }
            function test() {}
        }
    )");
    vm.run(script);
    auto                                cls        = vm.findClass("Test1");
    auto                                v          = vm.getHandle();
    const auto                          initialTop = sq_gettop(v);
    std::map<std::string, SQObjectType> members;
    sq_pushobject(v, cls.getRaw());
    sq_pushnull(v);
    while (SQ_SUCCEEDED(sq_next(v, -2))) {
        const char* name   = nullptr;
        const auto  result = sq_getstring(v, -2, &name);
        REQUIRE(SQ_SUCCEEDED(result));
        REQUIRE(name != nullptr);
        members.emplace(name, sq_gettype(v, -1));
        sq_pop(v, 2);
    }
    sq_pop(v, 2);
    const std::map<std::string, SQObjectType> expected = {
        {"attr1", OT_CLASS}, {"attr2", OT_NULL}, {"constructor", OT_CLOSURE}, {"test", OT_CLOSURE}};
    REQUIRE(members == expected);
    REQUIRE(sq_gettop(v) == initialTop);
}


TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.GetterTest") { REQUIRE(callScript("getterTest")); }


TEST_CASE("SimpleSquirrelTest.GetAttr") {
    ssq::VM vm(128);
    auto    script = vm.compileSource(R"(
        class Canvas {}
        class Avatar {
            name = "";
            </ type = Canvas /> can = null;
        }
    )");
    vm.run(script);
    auto       cls        = vm.findClass("Avatar");
    auto       v          = vm.getHandle();
    const auto initialTop = sq_gettop(v);
    sq_pushobject(v, cls.getRaw());
    sq_pushstring(v, "can", -1);
    const auto result = sq_getattributes(v, -2);
    REQUIRE(SQ_SUCCEEDED(result));
    REQUIRE(sq_gettype(v, -1) == OT_TABLE);
    std::map<std::string, SQObjectType> attributes;
    sq_pushnull(v);
    while (SQ_SUCCEEDED(sq_next(v, -2))) {
        const char* name      = nullptr;
        const auto  keyResult = sq_getstring(v, -2, &name);
        REQUIRE(SQ_SUCCEEDED(keyResult));
        REQUIRE(name != nullptr);
        attributes.emplace(name, sq_gettype(v, -1));
        sq_pop(v, 2);
    }
    sq_pop(v, 2);
    const std::map<std::string, SQObjectType> expected = {{"type", OT_CLASS}};
    REQUIRE(attributes == expected);
    sq_pushstring(v, "name", -1);
    const auto emptyResult = sq_getattributes(v, -2);
    REQUIRE(SQ_SUCCEEDED(emptyResult));
    REQUIRE(sq_gettype(v, -1) == OT_NULL);
    sq_pop(v, 2);
    REQUIRE(sq_gettop(v) == initialTop);
}

// ---------------------------------------------------------------------------
// Instance `_get` / `_set` metamethods.
//
// The plan's step 2 makes the engine the authoritative store for script component
// fields. Whether that forces every script to switch to setters depends on these
// three VM behaviours, so they are pinned here rather than assumed.
// ---------------------------------------------------------------------------

TEST_CASE("SimpleSquirrelTest.InstanceGetAndSetMetamethodsAreConsulted") {
    ssq::VM vm(128);
    vm.run(vm.compileSource(R"(
        class Probe {
            log = ""
            function _get(key) { log = "get:" + key; return 42 }
            function _set(key, value) { log = "set:" + key + ":" + value; return value }
        }
        probe <- Probe()
        readMissing <- probe.missing
        probe.other = 7
        writeLog <- probe.log
    )"));

    // 未声明的成员读写都落到元方法上。
    REQUIRE_EQ(vm.find("readMissing").toInt(), 42);
    REQUIRE_EQ(vm.find("writeLog").toString(), std::string("set:other:7"));
}

TEST_CASE("SimpleSquirrelTest.DeclaredFieldShadowsTheMetamethods") {
    ssq::VM vm(128);
    vm.run(vm.compileSource(R"(
        class Probe {
            x = 0.0
            calls = 0
            function _get(key) { calls = calls + 1; return 99.0 }
            function _set(key, value) { calls = calls + 1 }
        }
        probe <- Probe()
        declaredRead <- probe.x
        probe.x = 5.0
        declaredReread <- probe.x
        calls <- probe.calls
    )"));

    // 声明过的字段走实例槽位，元方法完全不会被调用：这正是"字段留在类里"就没法让引擎当权威的原因。
    REQUIRE_EQ(vm.find("declaredRead").toFloat(), 0.0);
    REQUIRE_EQ(vm.find("declaredReread").toFloat(), 5.0);
    REQUIRE_EQ(vm.find("calls").toInt(), 0);
}

TEST_CASE("SimpleSquirrelTest.RemovingAClassFieldFallsThroughToTheMetamethods") {
    // 结论性的一条：把一个已声明的字段从**类**的成员表里摘掉之后，同名成员的读写是否就落到
    // 元方法上？若是，引擎就能在登记组件时"取走字段名与默认值、再把槽位摘掉"，从而既让引擎
    // 持有权威数据，又**不改用户脚本的 `e.pos.x` 写法**。
    ssq::VM vm(128);
    vm.run(vm.compileSource(R"(
        class Probe {
            x = 0.0
            log = ""
            function _get(key) { log = "get:" + key; return 99.0 }
            function _set(key, value) { log = "set:" + key; return value }
        }
        probe <- Probe()
    )"));

    HSQUIRRELVM v   = vm.getHandle();
    const auto  top = sq_gettop(v);
    sq_pushroottable(v);
    sq_pushstring(v, "probe", -1);
    REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
    SQObjectPtr instance;
    sq_getstackobj(v, -1, &instance);
    REQUIRE(sq_gettype(v, -1) == OT_INSTANCE);
    _class(instance)->_members->Remove(SQObjectPtr(SQString::Create(_ss(v), "x")));
    sq_settop(v, top);

    vm.run(vm.compileSource(R"(
        afterRead <- probe.x
        probe.x = 5.0
        afterLog <- probe.log
    )"));

    REQUIRE_EQ(vm.find("afterRead").toFloat(), 99.0);                  // 读到的是元方法给的值
    REQUIRE_EQ(vm.find("afterLog").toString(), std::string("set:x"));  // 写入也走了元方法
}
