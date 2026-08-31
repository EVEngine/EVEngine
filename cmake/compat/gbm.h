#pragma once

// Minimal production declarations required by Dawn's DRMUtils. Full GBM APIs
// are only used by Dawn's own disabled test targets.
#ifdef __cplusplus
extern "C" {
#endif

struct gbm_bo;
struct gbm_device;

void gbm_bo_destroy(struct gbm_bo *bo);
void gbm_device_destroy(struct gbm_device *device);

#ifdef __cplusplus
}
#endif
