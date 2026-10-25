#include "common/ScriptBinding.h"
#include "network/NetStream.h"
#include <iostream>
#include <stdexcept>
using namespace eve::network;

int main() {
    try {
        for (bool direct : {false, true}) {
            NetWriter initial;
            initial.writeBool(true); initial.writeString("hello"); initial.writeString("");
            const std::string payload = initial.toString();
            ssq::VM vm(1024);
            vm.addFunc("expected", [&payload]() { return payload; });
            auto writer = vm.addClass("Writer", ssq::Class::Ctor<NetWriter()>());
            auto reader = vm.addClass("Reader", [&payload]() { return new NetReader(payload.data(), payload.size()); });
            if (direct) {
                eve::script::bindMethod(writer, "writeBool", &NetWriter::writeBool);
                eve::script::bindMethod(writer, "writeString", &NetWriter::writeString);
                eve::script::bindMethod(writer, "toString", &NetWriter::toString);
                eve::script::bindMethod(reader, "bool", &NetReader::b);
                eve::script::bindMethod(reader, "str", &NetReader::str);
                eve::script::bindMethod(reader, "ok", &NetReader::ok);
            } else {
                writer.addFunc("writeBool", &NetWriter::writeBool);
                writer.addFunc("writeString", &NetWriter::writeString);
                writer.addFunc("toString", [](NetWriter* w) { return w->toString(); });
                reader.addFunc("bool", &NetReader::b);
                reader.addFunc("str", [](NetReader* r) { return r->str(); });
                reader.addFunc("ok", [](NetReader* r) { return r->ok(); });
            }
            vm.run(vm.compileSource(R"(
                local w=Writer(); local r=Reader();
                w.writeBool(true); w.writeString("hello"); w.writeString("");
                if(w.toString()!=expected()) throw "writer encoding";
                if(!r.ok() || r.bool()!=true) throw "bool read";
                if(r.str()!="hello" || r.str()!="" || !r.ok()) throw "string read";
                r.bool(); if(r.ok()) throw "truncation not detected";
                if(r.str()!="" || r.ok()) throw "sticky error";
                local errors=0;
                try {w.writeString(12);} catch(e) {errors++;}
                try {w.writeBool();} catch(e) {errors++;}
                try {r.str(1);} catch(e) {errors++;}
                if(errors!=3) throw "argument validation";
            )"));
        }
        std::cout << "PASS: network baseline/direct roundtrip and failures\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
