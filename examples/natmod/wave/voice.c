#include "voice.h"

// (dest_pitch / sample.pitch) * ( sample.sampling_rate / SYSTEM_SAMPLING_RATE
// (dest_pitch / sample.pitch) * ( sample.sampling_rate / SYSTEM_SAMPLING_RATE


void _voice_init(Voice_t *voice, Sample_t *sample, UQ17_15t pitch) {
    voice->sample = sample;
    voice->current_sector = 0;
    voice->current_pos = sample->offset<<FRAC_BITS;

    // UQ_34_30
    uint64_t unscaled_omega = (pitch / voice->sample->pitch) * (voice->sample->sampling_rate / SYSTEM_SAMPLING_RATE);

    // UQ_10_22
    voice->omega = unscaled_omega>>(34-RING_BITS);

    voice->state = VOICE_INIT;
    voice->prefetch = PREFETCH_A;
}

_Bool _voice_fill_bloc(Voice_t * voice, const Q16_16t * amp, Q16_16t * dest) {
    uint32_t count = BLOC_SIZE;
    UQ10_22t pos = voice->current_pos;

    while(count--) {
        int16_t sample = voice->ring[pos];
        pos +=  voice->omega;
        *dest++ += (sample * (*amp++));
    }

    if (pos < voice->current_pos) {
    // if ((!(pos&IN_RING_B_MASK)) && (voice->current_pos&IN_RING_B_MASK)) {
        // did wrap, increment current_sector and ask for a refill in B
        voice->current_sector += SECTORS_IN_RING;
        voice->prefetch = PREFETCH_B;
    } else if ((pos&IN_RING_B_MASK) && (!(voice->current_pos&IN_RING_B_MASK))) {
        // entering ring B, ask for a refill in A
        voice->prefetch = PREFETCH_A;
    }

    voice->current_pos = pos;
    return false;
}
