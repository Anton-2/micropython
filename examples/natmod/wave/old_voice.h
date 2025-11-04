#pragma once

#include "wave_mod.h"
#include "eg.h"

#define RING_SIZE_IN_BLOCK (8)
#define RING_SIZE_IN_SAMPLES (SAMPLE_PER_BLOCK*RING_SIZE_IN_BLOCK)
#define RING_MASK ((2*RING_SIZE_IN_SAMPLES)-1)

#define HALF_RING_SIZE_IN_BLOCK (RING_SIZE_IN_BLOCK/2)
#define HALF_RING_SIZE_IN_SAMPLES (RING_SIZE_IN_SAMPLES/2)

#define HALF_RING_EMPTY    (0)
#define HALF_RING_READY    (1)
#define HALF_RING_REQ      (2)
#define HALF_RING_FETCHING (2)


typedef struct {
    uint32_t first_sample;      // first sample# at start of half ring
    uint32_t last_sample;      // last sample# in half ring
    uint32_t state;
} HalfRing_t;


typedef struct {
    int16_t * addr;             // start of ring, len 2*HALF_RING_SIZE_IN_SAMPLES
    HalfRing_t hr[2];
    int32_t current;
} Ring_t;

// _Static_assert(sizeof(SampleRing) == 20, "SampleRing size error");

typedef struct {
    uint32_t start_sector;
    uint32_t sample_offset;
    uint32_t length;

    uint32_t loop_start;       // sample#, inclusive
    uint32_t loop_end;         // sample#, inclusive, 0 means no loop
} Sample_t;


#define VOICE_FREE  (0)
#define VOICE_INIT  (1)
#define VOICE_RUNNING  (2)
#define VOICE_DONE  (3)

typedef struct Voice {
    uint32_t state;

    Sample_t * sample;
    Ring_t ring;

    uint32_t phase;
    uint32_t phase_inc;

    uint32_t loop_start;
    uint32_t loop_end;
} Voice_t;


_Bool voice_fill_bloc(Voice_t * voice, int32_t * amp, int32_t * dest);
void _voice_init(Voice_t * voice, Sample_t * sample, uint32_t phase_inc);
