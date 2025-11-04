#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "voice.h"

// This is the internal state of a Wave instance.
typedef struct mp_obj_voice {
    mp_obj_base_t base;
    Voice_t voice;
} mp_obj_voice_t;
