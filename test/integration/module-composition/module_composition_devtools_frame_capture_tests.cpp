#include <Poco/InflatingStream.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include "common/Capability.h"
#include "common/FrameCaptureImage.h"
#include "devtools/FrameCapturePreview.hpp"
#include "devtools/McpFrameCapture.hpp"
#include "devtools/McpToolFilter.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("mcp.frameCapture.providerAbsentAndSchemaRoutes") {
    auto* previous = eve::cap::query<eve::IRenderFrameCapture>();
    if (previous) eve::cap::revoke<eve::IRenderFrameCapture>(previous);
    const auto response = eve::dev::callMcpFrameCaptureTool("eve_frame_capture_capabilities", nullptr);
    ASSERT(response.find("ProviderAbsent") != std::string::npos);
    Poco::JSON::Parser parser;
    auto               tools =
        parser.parse("[" + std::string(eve::dev::mcpFrameCaptureSchemas()) + "]").extract<Poco::JSON::Array::Ptr>();
    ASSERT(tools->size() == 7);
    ASSERT(eve::dev::mcpToolMutates("eve_frame_capture_export"));
    ASSERT(!eve::dev::mcpToolMutates("eve_frame_capture"));
    for (unsigned i = 0; i < tools->size(); ++i)
        ASSERT(eve::dev::isMcpFrameCaptureTool(tools->getObject(i)->getValue<std::string>("name")));
    if (previous) eve::cap::provide<eve::IRenderFrameCapture>(previous);
}
