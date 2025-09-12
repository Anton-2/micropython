#include "wave.h"

#define SAMPLE_PER_BLOCK (256)

static inline _Bool sample_get_bloc(SamplePlayer* self, uint32_t sample_pos) {
    uint32_t bloc_idx = sample_pos/SAMPLE_PER_BLOCK;
    uint32_t first_pos = bloc_idx*SAMPLE_PER_BLOCK;
    uint32_t next_bloc_pos = first_pos+SAMPLE_PER_BLOCK;

    // last bloc ?
    if (next_bloc_pos >= self->src_count) {
        // end of buffer ?
        if (sample_pos >= self->src_count) {
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


static inline uint16_t o_next_sample(SamplePlayer* self) {
    if (! self->running) {
        return 0;
    } else {
        uint16_t pos = self->phase>>16;
        uint16_t frac = self->phase&0xFFFF;

        self->phase += self->phase_inc;

        uint32_t s1 = get_sample(self, pos);
        uint32_t s2 = get_sample(self, pos+1);

        return ((s1<<16) + (s2-s1)*frac)>>16;
    }
}

static inline int16_t next_sample(SamplePlayer* self) {
    uint16_t pos = self->phase>>16;
    uint16_t frac = self->phase&0xFFFF;

    self->phase += self->phase_inc;

    uint32_t s1 = get_sample(self, pos);
    uint32_t s2 = get_sample(self, pos+1);

    return ((s1<<16) + (s2-s1)*frac)>>16;
}

_Bool player_fill(SamplePlayer * self, int32_t * dest, uint32_t dest_count) {
    while (dest_count--) {
        *dest++ += next_sample(self);
    }
    return self->running;
}

static inline int32_t fill_loop(int32_t * dest, int32_t * last, int32_t val, int32_t coef, int32_t incr) {
    while (dest < last) {
        int32_t tmp = ((int64_t)val * (int64_t)coef)>>32;
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
        uint32_t seg_count = (eg->count < dest_count) ? eg->count : dest_count;
        dest_count -= seg_count;
        eg->count -= seg_count;

        eg->value = fill_loop(dest, dest+seg_count, eg->value, eg->regions[eg->region_idx].coef, eg->regions[eg->region_idx].incr);

        if (eg->count<=0) {
            set_region(eg, eg->region_idx+1);
        }
    }
    return eg->region_idx!=1;
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
