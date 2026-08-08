#include "common/RenderTrace.h"

namespace eve::debug {
namespace {
IRenderTracer* g_tracer = nullptr;
}

void setRenderTracer(IRenderTracer* tracer) { g_tracer = tracer; }

IRenderTracer* renderTracer() { return g_tracer; }

}  // namespace eve::debug
