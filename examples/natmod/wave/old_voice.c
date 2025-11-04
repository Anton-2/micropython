#include <assert.h>
#include "wave_mod.h"
#include "voice.h"

static void hr_req(Voice_t * voice, uint32_t hr_index, uint32_t first_sample) {
    HalfRing_t * hring = voice->ring.hr + hr_index;
    assert(hring->state <= HALF_RING_READY);

    hring->first_sample = first_sample;
    hring->last_sample = first_sample + HALF_RING_SIZE_IN_SAMPLES - 1;
    hring->state = HALF_RING_REQ;
}

void _voice_init(Voice_t * voice, Sample_t * sample, uint32_t phase_inc) {
    voice->state = VOICE_INIT;
    voice->sample = sample;

    voice->phase = sample->sample_offset * ONE_PHASE;
    voice->phase_inc = phase_inc;

    if (sample->loop_end) {
        voice->loop_start = sample->loop_start + sample->sample_offset;
        voice->loop_end = sample->loop_end + sample->sample_offset;
    } else {
        voice->loop_start = 0;
        voice->loop_end = 0;
    }

    hr_req(voice, 0, 0);
}
