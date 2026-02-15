// Voice buffer for continuous circular SD card reading using scatter gather DMA

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "include/sdio_rp2350.h"


//Number of bits to index ring buffer
#define RING_BITS 12                              //   12
#define RING_BYTES (1<<RING_BITS)                 // 4096 bytes ring buffer
#define RING_SIZE  (RING_BYTES/SDIO_BLOCK_SIZE)   //    8 sectors in ring

// amplitude (?), sample accumulator
typedef int32_t Q16_15_t;


// pitch
typedef uint32_t UQ15_17_t;     // 0..32767


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


// Voice structure
typedef struct {
    // SD card position
    uint32_t sd_current_sector;     // Current sector being read from SD card
    uint32_t sd_size_bytes;         // Total size in bytes to read from SD card, buffer is 0 filled after this
    uint32_t sd_bytes_read;         // Number of bytes already read from SD card

    // Ring buffer position
    uint32_t write_sector_idx;      // Current sector index being written by DMA (0 to RING_SIZE-1)
    uint32_t sectors_filled;        // Number of sectors currently filled with data
    uint32_t bytes_consumed;        // Total number of bytes consumed by reader

    // Voice
    sample_t * sample;
    omega_t omega_inc;              // phase increment § OMEGA_BITS.FRAC_BITS
    omega_t omega;                  // phase position

    uint32_t state:3;

    // Buffer data (aligned for DMA)
    int16_t buffer[RING_BYTES/sizeof(int16_t)] __attribute__((aligned(4)));  // Continuous buffer for RING_SIZE sectors
} sdio_voice_t;


// Voice manager structure
typedef struct {
    sdio_voice_t *voices;                            // Dynamically allocated array of voices
    uint32_t num_voices;                             // Number of active voices
    int active_voice_idx;                            // Index of voice with active transfer (-1 if none)
    uint32_t transfer_num_blocks;                    // Number of blocks in current transfer
    uint32_t transfer_blocks_completed;              // Number of blocks already processed from current transfer
} sdio_voice_manager_t;

// Initialize voice manager
// num_voices: number of voices to allocate
// Allocates voices dynamically but does not configure them yet
sdio_status_t sdio_voice_manager_init(sdio_voice_manager_t *manager,
                                      uint32_t num_voices);

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
// size_bytes: total size in bytes to read (sectors beyond this are filled with zeros)
// Returns SDIO_ERR_INVALID_PARAM if a transfer is in progress on this voice
sdio_status_t sdio_voice_start(sdio_voice_manager_t *manager,
                                uint32_t voice_index,
                                uint32_t sd_sector,
                                uint32_t size_bytes);

// Update all voices (should be called regularly)
// Checks if current transfer is complete and starts next transfer on the least filled voice
// Returns SDIO_OK if voices are healthy, SDIO_BUSY if transfer in progress,
// or error code on failure
sdio_status_t sdio_voice_manager_update(sdio_voice_manager_t *manager);

// Get number of bytes available for reading
// Consumer maintains its own read pointer
uint32_t sdio_voice_available(const sdio_voice_t *voice);

// Notify voice that bytes have been consumed
// bytes_read: number of bytes consumed from buffer
void sdio_voice_consume(sdio_voice_t *voice, uint32_t bytes_read);

// Print diagnostic information about a voice (for debugging)
void sdio_voice_print_diag(const sdio_voice_t *voice, const char *label);

// Print diagnostic information about the manager (for debugging)
void sdio_voice_manager_print_diag(const sdio_voice_manager_t *manager, const char *label);
