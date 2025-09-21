#include "wave.h"
#include <stdint.h>


static inline _Bool sample_get_bloc(SamplePlayer* self, uint32_t sample_pos) {
    uint32_t bloc_idx = sample_pos/SAMPLE_PER_BLOCK;
    uint32_t first_pos = bloc_idx*SAMPLE_PER_BLOCK;
    uint32_t next_bloc_pos = first_pos+SAMPLE_PER_BLOCK;

    // last bloc ?
    if (next_bloc_pos >= self->src_count) {
        // end of buffer ?
        if (sample_pos >= self->src_count) {
            // mp_printf(&mp_plat_print, "end run pos %u count %u\n", sample_pos, self->src_count);
            return false;
        }
        next_bloc_pos = self->src_count;
    }

    self->bloc.start = first_pos;
    self->bloc.end = next_bloc_pos;
    self->bloc.data = self->src + first_pos;

    return true;
}

static inline int16_t get_sample(SamplePlayer* self, int pos) {

    if (!(self->bloc.start <= pos && pos < self->bloc.end)) {
        if (! sample_get_bloc(self, pos)) {
            self->running = false;
            return 0;
        }
    }

    return self->bloc.data[pos - self->bloc.start];
}

static inline int32_t smulh(int32_t a, int32_t b) {
    return ((int64_t)a * (int64_t)b)>>32;
}

static inline int16_t next_sample(SamplePlayer* self) {
    uint16_t pos = self->phase>>PHASE_SHIFT;
    uint16_t frac = self->phase&PHASE_MASK;

    uint32_t s1 = get_sample(self, pos);
    uint32_t s2 = get_sample(self, pos+1);

    self->phase += self->phase_inc;

    if (self->loop_end) {
        int32_t delta = self->phase - self->loop_end;
        while(delta >= 0) {
            // uint32_t old_phase = self->phase;
            self->phase = self->loop_start + delta;
            delta = self->phase - self->loop_end;
            self->running = true;
            // mp_printf(&mp_plat_print, "loop delta start %u end %u delta %d old %u new %u \n", self->loop_start, self->loop_end, delta, old_phase, self->phase);
        }
    }

    return ((s1<<PHASE_SHIFT) + (s2-s1)*frac)>>PHASE_SHIFT;
}

_Bool player_fill(SamplePlayer * self, int32_t * amp, int32_t * dest, uint32_t dest_count) {
    // int32_t delta = self->phase - self->loop_end;
    // mp_printf(&mp_plat_print, "phase %u end %u delta %d \n", self->phase, self->loop_end, delta);

    while (dest_count--) {
        *dest++ += smulh(*amp++, next_sample(self));
    }
    return self->running;
}

static inline int32_t fill_loop(int32_t * dest, int32_t * last, int32_t val, int32_t coef, int32_t incr) {
    while (dest < last) {
        int32_t tmp = smulh(val, coef);
        val = (tmp<<2) + incr;
        if (val < 0) {
            val = 0;
        }
        *dest++ = val;
    }
    return val;
}


_Bool eg_fill(EG * eg, int32_t * dest, uint32_t dest_count) {
    while(dest_count) {
        int32_t val = eg->value;
        if (eg->region_idx == eg->region_count) {
            // infinite flat seg (sustain or end of release)
            while (dest_count--) {
                *dest++ = val;
            }
            return true;
        } else {
            EgRegion * region = eg->regions + eg->region_idx;
            uint32_t seg_count = (region->count < dest_count) ? region->count : dest_count;
            dest_count -= seg_count;
            region->count -= seg_count;

            eg->value = fill_loop(dest, dest+seg_count, val, region->coef, region->incr);
            dest += seg_count;

            if (region->count<=0) {
                set_region(eg, eg->region_idx+1);
            }
        }
    }
    // end of voice if we are at the end of the regions
    return eg->region_idx == eg->region_count;
}

/*
def process(self):
    pos = self.phase>>16
    frac = self.phase&0xFFFF
    self.phase += self.phase_inc

    s1 = self._get_sample(pos)
    s2 = self._get_sample(pos+1)

    return ((s1<<16) + int((s2-s1)*frac))>>16
*/
