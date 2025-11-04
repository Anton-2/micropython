#pragma once
#include <stdint.h>
#include <stdbool.h>

//
#define SYSTEM_SAMPLING_RATE    48000

// all size here are in sample

#define SECTOR_BITS             8  // 8 bits -> 256 sample /sector
#define SECTOR_SIZE             (1<<(SECTOR_BITS-1))

#define NB_RING_BITS            2  // 2**2 = 4 sectors per ring
#define SECTORS_IN_RING         (1<<2)


#define RING_BITS               (SECTOR_BITS+NB_RING_BITS)
#define RING_SIZE               (1<<RING_BITS)
#define FRAC_BITS               (32-RING_BITS)
#define IN_RING_B_MASK          (1<<31)

#define BLOC_SIZE               (SECTOR_SIZE/2) // half a sector -> 128 samples / bloc

typedef uint32_t UQ10_22t;  // ring sized increment
typedef uint32_t UQ17_15t;  // 17.15 unsigned for freq 0..131071
typedef int32_t  Q16_16t;   // 16.16 signed for sample value and amplitude.


typedef struct Sample {
    uint32_t first_sector;      // first sector idx
    uint32_t offset;            // offset of first sample in sector, in sample
    uint32_t length;            // nb samples
    uint32_t sampling_rate;
    UQ17_15t pitch;
} Sample_t;

#define VOICE_FREE              0
#define VOICE_INIT              1
#define VOICE_RUNNING           2
#define VOICE_DONE              3

#define PREFETCH_IDLE           0
#define PREFETCH_A              1
#define PREFETCH_B              2

typedef struct Voice {
    Sample_t * sample;
    uint32_t current_sector;    // sector index of current_pos
    UQ10_22t current_pos;       // [pos].[frac] § RING_BITS.FRAC_BITS
    UQ10_22t omega;             // phase increment § RING_BITS.FRAC_BITS
    uint32_t state:3;
    uint32_t prefetch:3;
    int16_t ring[RING_SIZE];
} Voice_t;


_Static_assert ((RING_SIZE/256) == 4, "4 sectors / ring");

void _voice_init(Voice_t * voice, Sample_t * sample, UQ17_15t pitch);
_Bool _voice_fill_bloc(Voice_t * voice, const Q16_16t * amp, Q16_16t * dest);

static inline int32_t smulh(int32_t a, int32_t b) {
    return ((int64_t)a * (int64_t)b)>>32;
}
