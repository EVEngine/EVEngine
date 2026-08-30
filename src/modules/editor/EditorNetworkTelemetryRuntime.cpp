#include "editor/EditorNetworkTelemetry.h"
#include "network/Network.h"
#include <utility>
namespace eve::editor { namespace {
template<class T>EditorResult<T>fail(EditorStatus s,const char*r,std::string m){return EditorResult<T>::error(s,RuleId(r),std::move(m));}
}
EditorResult<void>NetworkTelemetryCollector::collect(network::Network*source,double time,NetworkTelemetryModel&model)const{if(!source)return fail<void>(EditorStatus::Rejected,"editor.network.source","Network telemetry source is required");const auto s=source->telemetrySnapshot();NetworkTelemetrySample value;value.revision=s.revision;value.sentBytes=s.sentBytes;value.receivedBytes=s.receivedBytes;value.completions=s.completions;value.errors=s.errors;value.connections=s.connections;value.watchedTcp=s.watchedTcp;value.watchedUdp=s.watchedUdp;value.channels=s.channels;value.queuedTcpBytes=s.queuedTcpBytes;value.timeSeconds=time;return model.ingest(value);}
} // namespace eve::editor
