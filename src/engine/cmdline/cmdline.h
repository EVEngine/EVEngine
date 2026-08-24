#pragma once

#include <string>
#include <functional>
#include <memory>

#include "common/Module.h"
namespace CLI
{
    class App;
    class Formatter;
} // namespace CLI


namespace eve::cmd {

class Cmdline;
struct Register;

/** @brief One `eve <subcommand>` handler: CLI setup + argument parsing. */
struct Handler {
    virtual ~Handler() {}
    /** @brief Registers options on the CLI11 sub-app. */
    virtual void setup(CLI::App&, std::shared_ptr<CLI::Formatter>) = 0;
    /** @brief Parses arguments and runs the command; returns the exit code. */
    virtual int parse(CLI::App&, Cmdline&) = 0;
};

/** @brief 命令行模块（eve.cmd）：run / build / package / test / zip / dev-server 等子命令入口。 */
class Cmdline : public Module {

public:
    Module_REG(Cmdline);

    /** @brief 运行游戏（debug 时附加 DAP/MCP 端口）。 */
    int Run(std::string path, std::string root, bool debug = false, int dapPort = 0,
            int mcpPort = 0, std::string devServer = "");

    /** @brief 构建项目：委托仓库 Makefile 的 build/<platform>[-debug] 目标。 */
    int Build(std::string path, std::string output, std::string platform,
              std::string sdkRoot = "", bool debug = false);

    /** @brief 把游戏打包为单个可执行文件 / APK。 */
    int Package(std::string path, std::string output, std::string sdk);

    /** @brief 运行项目测试。 */
    int Test(std::string path);

    /** @brief 无头 MCP 宿主：stdio（默认）或 TCP，供 AI 代理驱动。 */
    int McpHost(std::string path, int port);

    /** @brief 把当前目录打成 .eve 压缩包。 */
    int Zip(std::string path);

    /** @brief 启动热重载开发服务器。 */
    int DevServer(std::string path, int port = 8765);

    /** @brief 获取第三方源码。 */
    int Get(std::string url);

    /** @brief 清理项目并移除内部产物。 */
    int Clean(std::string path);

    /** @brief 显示模块/函数/类型的文档。 */
    int Doc(std::string name, bool noOpen = false);

    /** @brief 创建新项目。 */
    int Create(std::string path, std::string name);

    /** @brief 解析并执行传入的 argv。 */
    int runArgs(unsigned argcIn, char** argvIn);

    /** @brief 第 i 个原始 argv（physfs 文件系统用）。 */
    std::string getArgv(unsigned i) {
        return (argv && i < argc && argv[i]) ? argv[i] : std::string{};
    }
    unsigned getArgc() { return argc; }

    /** @brief 子命令剩余位置参数。 */
    static std::string get_remaining(CLI::App* sub, std::string default_path = ".");

    friend Register;
protected:
    Cmdline();

    unsigned argc;
    char** argv;

    static std::vector<std::function<Handler*()>>& handers();
    static void registerCmd(std::function<Handler*()> handler);
};

#define CMD_REG(name) \
    static eve::cmd::Handler* name##_create() { return new name(); } \
    Register name##_cmd_reg(name##_create)

/** @brief 子命令静态注册器（CMD_REG 宏使用）。 */
struct Register {
    Register(std::function<Handler*()> handler) {
        Cmdline::registerCmd(handler);
    }
};


}  // namespace eve
