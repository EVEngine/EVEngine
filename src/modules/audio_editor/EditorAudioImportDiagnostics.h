#pragma once

#include "audio_editor/EditorAudioWaveform.h"

/**
 * @file
 * @brief Compatibility include for audio import diagnostics.
 *
 * The canonical implementation belongs to the optional audio_editing
 * satellite. New code should include audio_editing/AudioImportDiagnostics.h directly.
 */
#include "audio_editing/AudioImportDiagnostics.h"

namespace eve::editor {
using AudioImportInspectionRequest  = audio_editing::AudioImportInspectionRequest;
using AudioChannelInspection        = audio_editing::AudioChannelInspection;
using AudioImportInspectionResult   = audio_editing::AudioImportInspectionResult;
using AudioImportDiagnosticsService = audio_editing::AudioImportDiagnosticsService;
}  // namespace eve::editor
