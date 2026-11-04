#pragma once
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/backend/bake/DeformRecording.h"
#include "physics/backend/bake/DeformTrack.h"
namespace eve::physics::bake_detail {
template <class Source>
ssq::Table appendCapturedDeform(HSQUIRRELVM vm, Source& source, const ssq::Object& recordingObject, float seconds) {
    if (recordingObject.getType() != ssq::Type::INSTANCE ||
        recordingObject.getTypeTag() != script::detail::squirrelTypeHash<DeformRecording*>())
        return script::projectStatusResult(
            vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "expected a live deformation recording", "physics.captureBake")));
    /** @ownership Local borrow from rooted recordingObject. @lifetime This synchronous call only. */
    auto* recording = recordingObject.to<DeformRecording*>();
    if (!recording)
        return script::projectStatusResult(
            vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "null deformation recording",
                                                  "physics.captureBake")));
    auto frame = captureDeformFrame(source, seconds);
    if (!frame) return script::projectStatusResult(vm, frame.status());
    return script::projectResult(vm, recording->append(std::move(frame).takeValue()));
}
}  // namespace eve::physics::bake_detail
