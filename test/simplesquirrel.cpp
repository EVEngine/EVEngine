#include <iostream>

#include "ScriptTest.h"

extern const char* simplesquirrel_content;
UnitSciptTest(SimpleSquirrelTest, simplesquirrel_content);

TEST_F(SimpleSquirrelTest, ExportClass) { EXPECT_TRUE(vm.callFunc(vm.findFunc("exportClass"), vm).toBool()); }

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

    // void print() { std::cerr << a->data << std::endl; }
    // void setA(A* a) { this->a = a; }
    // A* a;
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

TEST_F(SimpleSquirrelTest, RefTest) {
    A::expose(vm);
    B::expose(vm);
    EXPECT_TRUE(vm.callFunc(vm.findFunc("refTest"), vm).toBool());
}