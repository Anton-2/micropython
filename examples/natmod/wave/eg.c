#include "wave_mod.h"
#include "eg.h"

//
// ** EG **
//
//
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
