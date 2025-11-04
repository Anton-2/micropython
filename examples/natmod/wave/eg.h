#pragma once
#include "wave_mod.h"

typedef struct {
    uint32_t count;     // duration in samples
    int32_t coef;       // coef (fixed point)
    int32_t incr;       // incr (fixed point)
} EgRegion;

_Static_assert(sizeof(EgRegion) == 4*3, "EgRegion size error");

typedef struct {
    EgRegion * regions;
    uint32_t region_count;
    uint32_t region_idx;

    int32_t value;
} EG;

static inline void set_region(EG * eg, uint32_t region_idx) {
    if (region_idx > eg->region_count) {
        region_idx = eg->region_count;
    }
    eg->region_idx = region_idx;
}

_Bool eg_fill(EG * eg, int32_t * dest, uint32_t dest_count);
