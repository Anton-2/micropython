#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "py/dynruntime.h"

#define PHASE_SHIFT (10)
#define PHASE_MASK ((1<<PHASE_SHIFT)-1)
#define MAX_SAMPLE_COUNT (1<<(32-PHASE_SHIFT))

#define SAMPLE_PER_BLOCK (256)

typedef struct {
    uint32_t start;
    uint32_t end;
    int16_t * data;
} SampleBloc;

typedef struct {
    _Bool running;

    int16_t * src;
    uint32_t src_count;

    uint32_t phase;
    uint32_t phase_inc;

    uint32_t loop_start;
    uint32_t loop_end;

    SampleBloc bloc;

    uint32_t loop_end_save;


} SamplePlayer;


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

// This is the internal state of a Wave instance.
typedef struct {
    mp_obj_base_t base;
    SamplePlayer player;
    EG eg;
} mp_obj_wave_t;

_Bool player_fill(SamplePlayer * self, int32_t * amp, int32_t * dest, uint32_t dest_count);

_Bool eg_fill(EG * eg, int32_t * dest, uint32_t dest_count);


static inline void set_region(EG * eg, uint32_t region_idx) {
    if (region_idx > eg->region_count) {
        region_idx = eg->region_count;
    }
    eg->region_idx = region_idx;
}
