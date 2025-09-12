#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "py/dynruntime.h"

typedef struct {
    uint32_t start;
    uint32_t end;
    int16_t * data;
} SampleBloc;

typedef struct {
    int16_t * src;
    uint32_t src_count;

    uint32_t phase;
    uint32_t phase_inc;

    _Bool running;

    SampleBloc bloc;

} SamplePlayer;

typedef struct {
    uint32_t count;
    int32_t coef;
    int32_t incr;
} EgRegion;


typedef struct {
    EgRegion * regions;
    uint32_t region_count;
    uint32_t region_idx;
    int32_t value;
    uint32_t count;
} EG;

// This is the internal state of a Wave instance.
typedef struct {
    mp_obj_base_t base;
    SamplePlayer player;
    EG eg;
} mp_obj_wave_t;

_Bool player_fill(SamplePlayer * self, int32_t * dest, uint32_t dest_count);

void eg_trigger(EG * eg, int32_t * dest, uint32_t dest_count);
_Bool eg_fill(EG * eg, int32_t * dest, uint32_t dest_count);


static inline void set_region(EG * eg, uint32_t region_idx) {
    if (region_idx >= eg->region_count) {
        region_idx = 0;
    }
    eg->region_idx = region_idx;
    eg->count = eg->regions[eg->region_idx].count;
}

/*
 * eg_on(regions)
 * eg_off() # or eg_trigger(regions) and eg_tigger() ?
 * eg_fill(buffer)
 */
