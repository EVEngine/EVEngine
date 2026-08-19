#include <iostream>
#include <cassert>

#include "ScriptTest.h"
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqtable.h"
#include "sqclass.h"

extern const char* simplesquirrel_content;
UnitSciptTest(SimpleSquirrelTest, simplesquirrel_content);

TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.ExportClass") {
    CHECK(vm.callFunc(vm.findFunc("exportClass"), vm).toBool());
}

class A {
public:
    SSQ_REG
    A() {
        data = new char[32];
        size = 0;
        std::cerr << "A()" << std::endl;
    }
    ~A() {
        delete data;
        std::cerr << "~A()" << std::endl;
    }
    void setString(std::string name) {
        if (name.size() < 32) {
            strncpy(data, name.c_str(), name.size());
            data[name.size()] = '\0';
            size              = name.size();
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
    CHECK(vm.callFunc(vm.findFunc("refTest"), vm).toBool());
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
                if(_isfield(p)) { printf("field %d\n", _member_idx(p));}
                if(_ismethod(p)) {printf("method %d\n", _member_idx(p));}
            }
        }
    }

    ssq::Object obj = cls.find("attr1");
    ssq::Class attr = obj.toClass();

    CHECK(vm.callFunc(vm.findFunc("testDefClass"), vm).toBool());
}
#endif


TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.PerformInteration") {
    ssq::Class cls = vm.findClass("Test1");
    auto& v = vm.getHandle();
    sq_pushobject(v, cls.getRaw());
    sq_pushnull(v);  //null iterator
    while(SQ_SUCCEEDED(sq_next(v,-2)))
    {
        //here -1 is the value and -2 is the key
        const char* name;
        sq_getstring(v, -2, &name);
        printf("%s\n", name);
        sq_pop(v,2); //pops key and val before the nex iteration
    }

    sq_pop(v,2); //pops the null iterator
}


TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.GetterTest") {
    CHECK(vm.callFunc(vm.findFunc("getterTest"), vm).toBool());
}


TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.GetAttr") {
    ssq::Class cls = vm.findClass("Avatar");
    auto& v = vm.getHandle();
    sq_pushobject(v, cls.getRaw());
    sq_pushnull(v);  //null iterator
    while(SQ_SUCCEEDED(sq_next(v,-2)))
    {
        //here -1 is the value and -2 is the key
        const char* name;
        sq_getstring(v, -2, &name);
        printf("%s\n", name);

        sq_pop(v,1); //pops key and val before the nex iteration
        sq_getattributes(v, -3);

        // loop table
        sq_pushnull(v);  //null iterator
        while(SQ_SUCCEEDED(sq_next(v,-2))) {
            const char* key;
            sq_getstring(v, -2, &key);
            SQObjectType t = sq_gettype(v, -1);
            printf("  %s %d\n", key,t==OT_CLASS? 1:0);
            sq_pop(v,2);
        }
        sq_pop(v,2); // pops iterator and table
    }

    sq_pop(v,2); //pops the null iterator and object
}
