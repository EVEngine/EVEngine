#include "MeshVfxBatchSubmission.h"

namespace eve::stylize {
namespace {

void markPartial(MeshVfxSubmissionReport& report) {
    if (report.status == MeshVfxQueueSubmitStatus::Complete) {
        report.status = MeshVfxQueueSubmitStatus::Partial;
    }
}

} // namespace

MeshVfxSubmissionReport MeshVfxBatchExecutor::submit(const MeshVfxRenderQueue& queue,
                                                      IMeshVfxBatchSink& sink) const {
    MeshVfxSubmissionReport report;
    for (const auto& batch : queue.batches) {
        const auto begin = sink.beginBatch(batch.key);
        if (begin == MeshVfxSubmitStatus::DeviceUnavailable) {
            report.status = MeshVfxQueueSubmitStatus::DeviceUnavailable;
            return report;
        }
        if (begin == MeshVfxSubmitStatus::Failed) {
            report.status = MeshVfxQueueSubmitStatus::Failed;
            return report;
        }
        if (begin == MeshVfxSubmitStatus::Skipped) {
            ++report.skippedBatches;
            report.skippedDraws += static_cast<std::uint32_t>(batch.draws.size());
            markPartial(report);
            continue;
        }

        ++report.acceptedBatches;
        for (const auto& draw : batch.draws) {
            const auto submitted = sink.submitDraw(draw);
            if (submitted == MeshVfxSubmitStatus::DeviceUnavailable) {
                report.status = MeshVfxQueueSubmitStatus::DeviceUnavailable;
                return report;
            }
            if (submitted == MeshVfxSubmitStatus::Failed) {
                ++report.failedDraws;
                report.status = MeshVfxQueueSubmitStatus::Failed;
                return report;
            }
            if (submitted == MeshVfxSubmitStatus::Skipped) {
                ++report.skippedDraws;
                markPartial(report);
            } else {
                ++report.acceptedDraws;
            }
        }

        const auto end = sink.endBatch();
        if (end == MeshVfxSubmitStatus::DeviceUnavailable) {
            report.status = MeshVfxQueueSubmitStatus::DeviceUnavailable;
            return report;
        }
        if (end == MeshVfxSubmitStatus::Failed) {
            report.status = MeshVfxQueueSubmitStatus::Failed;
            return report;
        }
        if (end == MeshVfxSubmitStatus::Skipped) {
            markPartial(report);
        }
    }
    return report;
}

} // namespace eve::stylize
