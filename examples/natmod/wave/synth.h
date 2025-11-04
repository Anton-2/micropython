#pragma once
#include "voice.h"
#include <stdbool.h>

#define MAX_VOICES 16

typedef struct PrefetchRequest {
    uint8_t voice_id:7;
    uint8_t hr_idx:1;
} PrefetchRequest_t;

_Static_assert(sizeof(PrefetchRequest_t) == 1, "PrefetchRequest_t size error");


typedef struct Prefetch {
    Voice_t *voice;
    HalfRing_t *hr;

    PrefetchRequest_t requests[MAX_VOICES];

    uint8_t req_read;
    uint8_t req_write;
} Prefetch_t;

typedef struct Synth {
    Voice_t voices[MAX_VOICES];
    Prefetch_t prefetch;
} Synth_t;

extern Synth_t synth;

void prefetch_check(Prefetch_t *prefetch);

void prefetch_enqueue(Prefetch_t *prefetch, uint8_t voice_id, uint8_t hr_idx);

void prefetch_start(Prefetch_t *prefetch);
