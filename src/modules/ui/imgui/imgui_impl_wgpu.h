// dear imgui: Renderer Backend for WebGPU (wgpu), adapted for EVEngine's
// vendored imgui v1.83. Uses the stable webgpu.h C API so the same file
// compiles against Dawn (native) and Emscripten (browser).

#pragma once

#include "imgui.h"

#include <webgpu/webgpu.h>

// Implemented and consumed inside EVUI. IMGUI_IMPL_API belongs to eve_imgui
// (core ImGui + SDL); importing it here would mark these local definitions dllimport.

/** @brief Internal EVUI compatibility entry point following the ImGui renderer backend convention. */
bool ImGui_ImplWGPU_Init(WGPUDevice device, int num_frames_in_flight, WGPUTextureFormat rt_format);
/** @brief Im gui impl wgpu shutdown. */
void ImGui_ImplWGPU_Shutdown();
/** @brief Im gui impl wgpu new frame. */
void ImGui_ImplWGPU_NewFrame();
/** @brief Im gui impl wgpu render draw data. */
void ImGui_ImplWGPU_RenderDrawData(ImDrawData *draw_data, WGPURenderPassEncoder pass_encoder);
/** @brief Internal EVUI font-upload compatibility entry point following the ImGui backend convention. */
bool ImGui_ImplWGPU_CreateFontsTexture();
/** @brief Im gui impl wgpu invalidate device objects. */
void ImGui_ImplWGPU_InvalidateDeviceObjects();
