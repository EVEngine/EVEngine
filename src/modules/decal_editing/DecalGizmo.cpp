#include "decal_editing/DecalTarget.h"

#include <cmath>

namespace eve::decal_editing {

EditorGizmoSnapshot DecalGizmoPreviewService::build(const DecalDocumentTarget& document) const {
    EditorGizmoSnapshot result; result.target=document.targetId().value();result.targetRevision=document.revision();
    const auto diagnostics=document.validate();for(const auto&diagnostic:diagnostics)if(diagnostic.severity()==DiagnosticSeverity::Error){result.status=EditorStatus::Rejected;result.diagnostics=diagnostics;return result;}
    const auto&position=*document.value("transform.position")->getIf<EditorValue::Array>();
    const auto&normal=*document.value("transform.normal")->getIf<EditorValue::Array>();
    std::array<double,3> p{},n{};double length2=0;
    for(int i=0;i<3;++i){p[i]=*position[i].getIf<double>();n[i]=*normal[i].getIf<double>();length2+=n[i]*n[i];}
    const double length=std::sqrt(length2);for(double&component:n)component/=length;
    const double size=*document.value("projection.size")->getIf<double>();
    const double depth=*document.value("projection.depth")->getIf<double>();
    result.primitives.push_back({"decal-volume","oriented-wire-box",p,{size,size,depth},n,{0.2,0.75,1.0,0.9},0,0,true});
    result.primitives.push_back({"decal-normal","arrow",p,{},n,{1.0,0.55,0.1,1.0},0,std::max(depth,size*0.5),false});
    result.diagnostics=diagnostics;result.status=EditorStatus::Applied;return result;
}

} // namespace eve::decal_editing
