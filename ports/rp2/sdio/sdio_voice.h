// Voice buffer for continuous circular SD card reading using scatter gather DMA

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <include/sdio_rp2350.h>

#include "sdio_adsr.h"

//Number of bits to index ring buffer
#define RING_BITS    12                              //   12   13
#define RING_BYTES   (1<<RING_BITS)                  // 4096 8192 bytes ring buffer
#define RING_SAMPLES (RING_BYTES/sizeof(int16_t))    // 2048 4096 samples in ring
#define RING_SIZE    (RING_BYTES/SDIO_BLOCK_SIZE)    //    8   16 sectors in ring
#define SECTOR_SAMPLES (SDIO_BLOCK_SIZE/2)           // samples per sector (int16)

#define SAMPLE_BYTES  sizeof(int16_t)                // bytes per sample
#define samples_to_bytes(n)  ((n) * SAMPLE_BYTES)
#define bytes_to_samples(n)  ((n) / SAMPLE_BYTES)

// amplitude (?)
typedef int16_t Q1_14_t;        // -2.0 ...1.99994

// sample accumulator
typedef int32_t Q17_14_t;


// pitch
typedef uint32_t UQ15_17_t;     // 0.0 ... 32767.999992


// omega
typedef uint32_t omega_t;                // fractional sample position.
#define OMEGA_BITS (RING_BITS-1)         // -1 because it's in sample (2 bytes)
#define OMEGA_SHIFT (32-OMEGA_BITS)
#define ONE_OMEGA (1<<OMEGA_SHIFT)

// size and offsets are in sample
typedef struct Sample {
    uint32_t first_sector;      // first sector on sdcard

    uint32_t header_length;     // offset of first sample in sector, in sample (22 for a std wav 16 bit wav file)
    uint32_t length;            // nb samples
    uint32_t loop_start;        // if loop : sample index for start of loop
    uint32_t loop_end;          // 0 -> no loop, else sample index for end of loop (first sample out of loop)

    UQ15_17_t pitch;            //
} sample_t;

#define VOICE_FREE              0
#define VOICE_INIT              1
#define VOICE_RUNNING           2
#define VOICE_DONE              3
#define VOICE_CANCEL            4


// Number of samples in loop cache (half the ring buffer)
#define LOOP_CACHE_SAMPLES (RING_SAMPLES)

// Voice structure, every counter / size is in sample
typedef struct {
    // SD card position
    uint32_t sd_start_sector;       // First sector being read from SD card
    uint32_t sd_size_samples;       // Total size in samples to read from SD card, buffer is 0 filled after this
    uint32_t sd_samples_read;       // Number of samples already read from SD card

    // Ring buffer position
    uint32_t write_sector_idx;      // Current sector index being written by DMA (0 to RING_SIZE-1)
    uint32_t sectors_filled;        // Number of sectors currently filled with data
    uint32_t samples_consumed;      // Total number of samples consumed by reader

    // Voice
    omega_t omega_inc;              // phase increment § OMEGA_BITS.FRAC_BITS
    omega_t omega;                  // phase position

    // Loop points (in samples, 0 = no loop)
    uint32_t loop_start_sample;     // Sample index of loop start (absolute, from beginning of sample data)
    uint32_t loop_end_sample;       // Sample index of loop end (first sample past the loop), 0 = no loop

    uint32_t state:3;

    // Optional per-voice ADSR envelope (NULL = use manager amplitude_buffer)
    adsr_t *adsr;

    // Buffer data (word aligned for DMA)
    int16_t buffer[RING_BYTES/sizeof(int16_t)] __attribute__((aligned(4)));  // Continuous buffer for RING_SIZE sectors

    // Loop cache: stores up to LOOP_CACHE_SAMPLES samples from the sector-aligned boundary
    // before loop_start_sample. Filled once when playback first reaches loop_start_sample.
    uint32_t loop_cache_samples;    // actual number of samples stored in loop_cache (0 = not yet filled)
    int16_t loop_cache[LOOP_CACHE_SAMPLES];

} sdio_voice_t;


// Voice manager structure
typedef struct {
    sdio_voice_t *voices;                            // Dynamically allocated array of voices
    Q1_14_t *amplitude_buffer;                       // Amplitude envelope buffer (chunk_size samples)
    Q17_14_t *accumulator_buffer;                    // Sample accumulator buffer for mixing (chunk_size samples)
    uint32_t num_voices;                             // Number of active voices
    uint32_t chunk_size;                             // Size of buffers (in samples)
    int active_voice_idx;                            // Index of voice with active transfer (-1 if none)
    int free_voice_idx;                              // Index of a free voice (-1 if none)
    uint32_t transfer_num_blocks;                    // Number of blocks in current transfer
    uint32_t transfer_blocks_completed;              // Number of blocks already processed from current transfer
} sdio_voice_manager_t;

// Initialize voice manager
// num_voices: number of voices to allocate
// chunk_size: size of the accumulator buffer (in samples)
// Allocates voices dynamically but does not configure them yet
sdio_status_t sdio_voice_manager_init(sdio_voice_manager_t *manager,
                                      uint32_t num_voices,
                                      uint32_t chunk_size);

// Free voice manager resources
void sdio_voice_manager_free(sdio_voice_manager_t *manager);

// Get voice by index
// Returns NULL if index is out of range
static inline sdio_voice_t *sdio_voice_manager_get(sdio_voice_manager_t *manager, uint32_t index)
{
    if (manager == NULL || index >= manager->num_voices)
    {
        return NULL;
    }

    return &manager->voices[index];
}

// Start/restart a voice with new SD card configuration
// Can be called on an already used voice to restart it
// sd_sector: first sector to read
// size_samples: total size in samples to read (sectors beyond this are filled with zeros)
// start_sample: starting sample offset
// loop_start_sample: sample index where the loop begins (0 if no loop)
// loop_end_sample: sample index of first sample past the loop (0 = no loop)
// Returns SDIO_ERR_INVALID_PARAM if a transfer is in progress on this voice
sdio_status_t sdio_voice_start(sdio_voice_manager_t *manager,
                                uint32_t voice_index,
                                uint32_t sd_sector,
                                uint32_t size_samples,
                                omega_t omega_inc,
                                uint32_t start_sample,
                                uint32_t loop_start_sample,
                                uint32_t loop_end_sample);

// Update all voices (should be called regularly)
// Checks if current transfer is complete and starts next transfer on the least filled voice
// Returns SDIO_OK if voices are healthy, SDIO_BUSY if transfer in progress,
// or error code on failure
sdio_status_t sdio_voice_manager_update(sdio_voice_manager_t *manager);

// Get number of samples available for reading
// Consumer maintains its own read pointer
uint32_t sdio_voice_available(const sdio_voice_t *voice);

// Notify voice that samples have been consumed
// samples_read: number of samples consumed from buffer
void sdio_voice_consume(sdio_voice_t *voice, uint32_t samples_read);

// Print diagnostic information about a voice (for debugging)
void sdio_voice_print_diag(const sdio_voice_t *voice, const char *label);

// Print diagnostic information about the manager (for debugging)
void sdio_voice_manager_print_diag(const sdio_voice_manager_t *manager, const char *label);

// Fill chunk: mix all active voices into the accumulator buffer
uint32_t sdio_voice_fill_chunk(sdio_voice_manager_t *manager);

// Fill amplitude buffer with a constant value
// amplitude: Q1_14_t value to fill the buffer with
void sdio_voice_fill_amplitude(sdio_voice_manager_t *manager, Q1_14_t amplitude);

// Attach or detach an ADSR envelope to a voice (NULL to detach)
// When attached, fill_chunk generates the amplitude from the ADSR instead of amplitude_buffer
void sdio_voice_set_adsr(sdio_voice_t *voice, adsr_t *adsr);
