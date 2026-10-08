// dear imgui: Renderer Backend for WebGPU (wgpu), adapted for EVEngine's
// vendored imgui v1.83. Uses the stable webgpu.h C API so the same file
// compiles against Dawn (native) and Emscripten (browser).

#pragma once

#include "imgui.h"

#include <webgpu/webgpu.h>

/** @brief Im gui impl wgpu init. */
IMGUI_IMPL_API bool ImGui_ImplWGPU_Init(WGPUDevice device, int num_frames_in_flight,
                                        WGPUTextureFormat rt_format);
/** @brief Im gui impl wgpu shutdown. */
IMGUI_IMPL_API void ImGui_ImplWGPU_Shutdown();
/** @brief Im gui impl wgpu new frame. */
IMGUI_IMPL_API void ImGui_ImplWGPU_NewFrame();
/** @brief Im gui impl wgpu render draw data. */
IMGUI_IMPL_API void ImGui_ImplWGPU_RenderDrawData(ImDrawData *draw_data,
                                                  WGPURenderPassEncoder pass_encoder);
/** @brief Im gui impl wgpu create fonts texture. */
IMGUI_IMPL_API bool ImGui_ImplWGPU_CreateFontsTexture();
/** @brief Im gui impl wgpu invalidate device objects. */
IMGUI_IMPL_API void ImGui_ImplWGPU_InvalidateDeviceObjects();
