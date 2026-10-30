#include "GenerationClient.h"
#include <Poco/UUIDGenerator.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
int main() {
    auto root=std::filesystem::temp_directory_path()/("eve-ssui-test-"+Poco::UUIDGenerator::defaultGenerator().createRandom().toString());
    std::filesystem::create_directories(root);
    auto expectError=[](auto fn) { try {fn();} catch(const std::exception&) {return;} throw std::runtime_error("Expected failure"); };
    try {
        ssui::GenerationClient c;
        expectError([&]{c.configure("https://localhost",root.string());});
        c.configure("http://127.0.0.1:1",root.string());
        if(c.busy() || !c.candidate().empty() || !c.applied().empty()) throw std::runtime_error("Unexpected initial state");
        expectError([&]{c.start("mock.checker@1","[]","{}");});
        expectError([&]{c.apply();}); expectError([&]{c.undo();});
        if(!c.start("mock.checker@1","{}","{}")) throw std::runtime_error("Valid JSON rejected");
        c.cancel(); c.shutdown();
        if(!c.candidate().empty()) throw std::runtime_error("Cancelled result leaked");
        auto asset="generated/"+std::string(64,'a')+".png";
        {std::ofstream out(root/"generated/material.json"); out<<"{\"revision\":1,\"baseColor\":\""<<asset<<"\",\"history\":[\"\"]}";}
        c.configure("http://127.0.0.1:1",root.string());
        if(c.applied()!=asset || !c.undo().empty()) throw std::runtime_error("Restore/undo failed");
        {std::ofstream out(root/"generated/material.json"); out<<"{\"revision\":3,\"baseColor\":\"\",\"history\":[]}";}
        expectError([&]{c.undo();});
        {std::ofstream out(root/"generated/material.json"); out<<"{\"revision\":3,\"baseColor\":\"../outside.png\",\"history\":[]}";}
        expectError([&]{c.configure("http://127.0.0.1:1",root.string());});
        std::filesystem::remove_all(root);
        std::cout<<"PASS native JSON validation, cancellation, persistence, undo, revision conflict and asset paths\n";
    } catch(...) {std::filesystem::remove_all(root);throw;}
}
