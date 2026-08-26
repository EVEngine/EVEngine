#include "graphics/webgpu/InitFlow.h"

#include "common/Exception.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace eve::graphics::webgpu {

InstanceDone InitFlow::createInstance() {
    wgpu::Instance instance;
#if defined(__EMSCRIPTEN__)
    instance = wgpu::CreateInstance();
#else
    // Native (Dawn) requires the TimedWaitAny instance feature so the
    // ProcessEvents busy-wait below can drive the request callbacks.
    const WGPUInstanceFeatureName requiredFeatures[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = requiredFeatures;
    instance = wgpu::CreateInstance(reinterpret_cast<const wgpu::InstanceDescriptor *>(&instanceDesc));
#endif
    if (!instance) throw Exception("WebGPU: wgpuCreateInstance failed");
    return InstanceDone{std::move(instance)};
}

AdapterDone InitFlow::requestAdapter(InstanceDone &&prev, const wgpu::Surface &compatibleSurface) {
    wgpu::Instance &instance = prev.instance;

    struct Result {
        wgpu::Adapter adapter;
        std::string error;
        std::atomic<bool> received{false};
    } result;

    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = compatibleSurface.Get();
    opts.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo cbInfo{};
    cbInfo.nextInChain = nullptr;
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView msg,
                         void *userdata1, void * /*userdata2*/) {
        auto *r = static_cast<Result *>(userdata1);
        if (status == WGPURequestAdapterStatus_Success && a) {
            r->adapter = wgpu::Adapter(a);
        } else {
            r->error = msg.data ? std::string(msg.data, msg.length) : "unknown adapter error";
        }
        r->received.store(true);
    };
    cbInfo.userdata1 = &result;
    cbInfo.userdata2 = nullptr;

    wgpuInstanceRequestAdapter(instance.Get(), &opts, cbInfo);
    while (!result.received.load()) {
#if defined(__EMSCRIPTEN__)
        // Yield to the browser event loop so the JS requestAdapter promise can
        // resolve and fire the callback, then flush it with ProcessEvents.
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
    }
    if (!result.adapter) {
        throw Exception("WebGPU: no adapter found (%s)", result.error.c_str());
    }
    return AdapterDone{std::move(instance), std::move(result.adapter)};
}

DeviceDone InitFlow::requestDevice(AdapterDone &&prev) {
    wgpu::Instance &instance = prev.instance;
    wgpu::Adapter &adapter = prev.adapter;

    struct Result {
        wgpu::Device device;
        std::string error;
        std::atomic<bool> received{false};
    } result;

    WGPUDeviceDescriptor devDesc{};
    devDesc.label = sv("eve_device");
    devDesc.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const *, WGPUErrorType type, WGPUStringView message, void *, void *) {
            std::fprintf(stderr, "[webgpu] uncaptured error type=%d: %.*s\n", int(type),
                         int(message.length), message.data ? message.data : "");
        };

    WGPURequestDeviceCallbackInfo cbInfo{};
    cbInfo.nextInChain = nullptr;
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView msg,
                         void *userdata1, void * /*userdata2*/) {
        auto *r = static_cast<Result *>(userdata1);
        if (status == WGPURequestDeviceStatus_Success && d) {
            r->device = wgpu::Device(d);
        } else {
            r->error = msg.data ? std::string(msg.data, msg.length) : "unknown device error";
        }
        r->received.store(true);
    };
    cbInfo.userdata1 = &result;
    cbInfo.userdata2 = nullptr;

    wgpuAdapterRequestDevice(adapter.Get(), &devDesc, cbInfo);
    while (!result.received.load()) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
    }
    if (!result.device) {
        throw Exception("WebGPU: device request failed (%s)", result.error.c_str());
    }

    wgpu::Queue queue = result.device.GetQueue();
    Capabilities caps;
    caps.capture(adapter, result.device);
    return DeviceDone{std::move(instance), std::move(adapter), std::move(result.device),
                      std::move(queue), std::move(caps)};
}

}  // namespace eve::graphics::webgpu