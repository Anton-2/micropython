#include "synth.h"
#include <assert.h>

/*
    got 3 wait lists

    1/ free voice       # you can take a voice in this to play : just iter over voices and get state
    2/ active list      # need to look at it for buffer fill : just iter over voices and get state
    1/ need prefetch    # ordered, prefetch needed

*/


Synth_t synth = {
    .prefetch = {
        .voice = NULL,
        .hr = NULL,
        .req_read = 0,
        .req_write = 0,
    }
};


int get_free_voice() {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (synth.voices[i].state == VOICE_FREE) {
            return i;
        }
    }
    return -1;
}

void loop() {

    prefetch_check(&synth.prefetch);
    prefetch_start(&synth.prefetch);



    // Your loop implementation here
}

void prefetch_enqueue(Prefetch_t *prefetch, uint8_t voice_id, uint8_t hr_idx) {
  prefetch->requests[prefetch->req_write].voice_id = voice_id;
  prefetch->requests[prefetch->req_write].hr_idx = hr_idx;
  uint8_t req_write = (prefetch->req_write + 1) % MAX_VOICES;
  if (req_write == prefetch->req_read) {
    // SIGNAL OvERFLOW
  } else {
    prefetch->req_write = req_write;
  }
}

void prefetch_check(Prefetch_t *prefetch) {
    if (! prefetch->voice) {
        return;
    }

    if (true) { // done
        prefetch->hr->state = HALF_RING_READY;
        prefetch->voice = NULL;
    }
}

void prefetch_start(Prefetch_t *prefetch) {

  if (prefetch->voice ||
      (prefetch->req_read == prefetch->req_write)) {
    // not ready, or nothing to do
    return;
  }

  int voice_id = prefetch->requests[prefetch->req_read].voice_id;
  int hr_idx = prefetch->requests[prefetch->req_read].hr_idx;

  prefetch->voice = &synth.voices[voice_id];
  prefetch->hr = &prefetch->voice->ring.hr[hr_idx];

  assert(prefetch->hr->state == HALF_RING_REQ);
  prefetch->hr->state = HALF_RING_FETCHING;
  prefetch->req_read = (prefetch->req_read + 1) % MAX_VOICES;

  // TODO : trigger prefetch
}
