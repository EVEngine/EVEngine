#include "network_editing/NetworkTelemetry.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace eve::network_editing { namespace {
template<class T>EditorResult<T>fail(EditorStatus s,const char*r,std::string m){return EditorResult<T>::error(s,RuleId(r),std::move(m));}
}
NetworkTelemetryModel::NetworkTelemetryModel(std::size_t capacity):capacity_(std::clamp<std::size_t>(capacity,2,36000)){}
EditorResult<void>NetworkTelemetryModel::ingest(NetworkTelemetrySample value){if(!std::isfinite(value.timeSeconds)||value.timeSeconds<0)return fail<void>(EditorStatus::Rejected,"editor.network.time","Network sample time is invalid");if(!samples_.empty()){const auto&previous=samples_.back();if(value.revision<previous.revision||value.timeSeconds<=previous.timeSeconds)return fail<void>(EditorStatus::Conflict,"editor.network.stale","Network telemetry sample is stale");const double dt=value.timeSeconds-previous.timeSeconds;const bool reset=value.sentBytes<previous.sentBytes||value.receivedBytes<previous.receivedBytes||value.completions<previous.completions||value.errors<previous.errors;if(!reset){value.sendBytesPerSecond=static_cast<double>(value.sentBytes-previous.sentBytes)/dt;value.receiveBytesPerSecond=static_cast<double>(value.receivedBytes-previous.receivedBytes)/dt;const auto events=value.completions-previous.completions;const auto errors=value.errors-previous.errors;value.errorRate=events?static_cast<double>(errors)/events:0;}}samples_.push_back(value);while(samples_.size()>capacity_)samples_.pop_front();return EditorResult<void>::applied();}
std::vector<EditorDiagnostic>NetworkTelemetryModel::diagnostics()const{std::vector<EditorDiagnostic>out;if(samples_.empty())return out;const auto&v=samples_.back();if(v.queuedTcpBytes>512*1024)out.push_back({RuleId("editor.network.backpressure"),DiagnosticSeverity::Warning,"TCP pending-send queue exceeds 512 KiB"});if(v.errorRate>.25&&v.completions>0)out.push_back({RuleId("editor.network.error-rate"),DiagnosticSeverity::Warning,"Recent network completion error rate exceeds 25%"});if(v.watchedTcp+v.watchedUdp>4096)out.push_back({RuleId("editor.network.socket-budget"),DiagnosticSeverity::Error,"Watched socket count exceeds the editor safety budget"});return out;}
void NetworkTelemetryModel::clear(){samples_.clear();}
} // namespace eve::network_editing
