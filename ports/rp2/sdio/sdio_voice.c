#include "sdio_voice.h"
#include <stdint.h>
#include <string.h>
#include "py/mphal.h"
#include "py/runtime.h"

// SD Card command definitions
#define SD_CMD_SET_BLOCKLEN         16
#define SD_CMD_READ_SINGLE_BLOCK    17
#define SD_CMD_READ_MULTIPLE_BLOCK  18
#define SD_CMD_SET_BLOCK_COUNT      23

#define DEBUG false

// Forward declaration
static _Bool _voice_fill_bloc(sdio_voice_t *voice, const Q1_14_t *amp, Q17_14_t *dest, uint32_t count);

// Helper: linearise `count` samples from voice->buffer[src % RING_SAMPLES..] into a flat buffer.
static inline void ring_read(const int16_t *ring, uint32_t src, int16_t *out, uint32_t count) {
    src %= RING_SAMPLES;
    uint32_t first = RING_SAMPLES - src;
    if (first > count) first = count;
    memcpy(out,         ring + src, first * sizeof(int16_t));
    if (first < count) memcpy(out + first, ring, (count - first) * sizeof(int16_t));
}

// Helper: write `count` samples from flat buffer into voice->buffer[dst % RING_SAMPLES..].
static inline void ring_write(int16_t *ring, uint32_t dst, const int16_t *in, uint32_t count) {
    dst %= RING_SAMPLES;
    uint32_t first = RING_SAMPLES - dst;
    if (first > count) first = count;
    memcpy(ring + dst, in,         first * sizeof(int16_t));
    if (first < count) memcpy(ring, in + first, (count - first) * sizeof(int16_t));
}

static inline void ring_copy(int16_t *ring, uint32_t dst, uint32_t src, uint32_t count) {
    if (count == 0 || dst == src) return;

    uint32_t dist_src_to_dst =(dst - src + RING_SAMPLES) % RING_SAMPLES;
    int forward = (dist_src_to_dst >= count);
    uint32_t remaining = count;

    if (forward) {
        uint32_t d = dst, s = src;
        while (remaining > 0) {
            uint32_t chunk = remaining;
            if (s + chunk > RING_SAMPLES) chunk = RING_SAMPLES - s;
            if (d + chunk > RING_SAMPLES) chunk = RING_SAMPLES - d;
            memcpy(ring + d, ring + s, chunk);
            s = (s + chunk) % RING_SAMPLES;
            d = (d + chunk) % RING_SAMPLES;
            remaining -= chunk;
        }
    } else {
        uint32_t d = (dst + count) % RING_SAMPLES;
        uint32_t s = (src + count) % RING_SAMPLES;
        while (remaining > 0) {
            uint32_t chunk = remaining;
            if (d == 0) d = RING_SAMPLES;
            if (s == 0) s = RING_SAMPLES;
            chunk = remaining;
            if (chunk > d) chunk = d;
            if (chunk > s) chunk = s;
            d -= chunk; s -= chunk;
            memmove(ring + d, ring + s, chunk);
            remaining -= chunk;
        }
    }
}



sdio_status_t sdio_voice_manager_init(sdio_voice_manager_t *manager,
                                             uint32_t num_voices,
                                             uint32_t chunk_size)
{
    if (manager == NULL)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    if (num_voices == 0 || chunk_size == 0)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    // Initialize all pointers to NULL for safe cleanup
    manager->voices = NULL;
    manager->amplitude_buffer = NULL;
    manager->accumulator_buffer = NULL;
    manager->num_voices = 0;
    manager->chunk_size = 0;
    manager->active_voice_idx = -1;
    manager->free_voice_idx = -1;
    manager->transfer_num_blocks = 0;
    manager->transfer_blocks_completed = 0;

    // Allocate array of voice structures using MicroPython allocator
    // m_tracked_calloc allocates and zeros memory
    manager->voices = m_tracked_calloc(sizeof(sdio_voice_t), num_voices);
    if (manager->voices == NULL) {
        sdio_voice_manager_free(manager);
        return SDIO_ERR_INVALID_PARAM;
    }

    // Allocate amplitude buffer (chunk_size samples, initialized to 0)
    manager->amplitude_buffer = m_tracked_calloc(sizeof(Q1_14_t), chunk_size);
    if (manager->amplitude_buffer == NULL) {
        sdio_voice_manager_free(manager);
        return SDIO_ERR_INVALID_PARAM;
    }

    // Allocate accumulator buffer for sample mixing (chunk_size samples, initialized to 0)
    manager->accumulator_buffer = m_tracked_calloc(sizeof(Q17_14_t), chunk_size);
    if (manager->accumulator_buffer == NULL) {
        sdio_voice_manager_free(manager);
        return SDIO_ERR_INVALID_PARAM;
    }

    manager->num_voices = num_voices;
    manager->chunk_size = chunk_size;

    return SDIO_OK;
}

sdio_status_t sdio_voice_start(sdio_voice_manager_t *manager,
                                      uint32_t voice_index,
                                      uint32_t sd_sector,
                                      uint32_t size_bytes,
                                      omega_t omega_inc,
                                      uint32_t start_offset,
                                      uint32_t loop_start_sample,
                                      uint32_t loop_end_sample)
{
    if (manager == NULL || voice_index >= manager->num_voices || size_bytes == 0)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    // Check if a transfer is in progress on this voice
    if (manager->active_voice_idx == (int)voice_index)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    sdio_voice_t *voice = sdio_voice_manager_get(manager, voice_index);

    // Configure/reconfigure the voice
    voice->sd_current_sector = sd_sector;
    voice->sd_size_bytes = size_bytes;
    voice->sd_bytes_read = 0;
    voice->write_sector_idx = 0;
    voice->sectors_filled = 0;
    voice->bytes_consumed = start_offset;
    voice->omega_inc = omega_inc;
    voice->omega = bytes_to_samples(start_offset) << OMEGA_SHIFT;
    voice->loop_start_sample = loop_start_sample;
    voice->loop_end_sample = loop_end_sample;
    voice->loop_cache_samples = 0;
    voice->state = VOICE_INIT;

    return SDIO_OK;
}

void sdio_voice_manager_free(sdio_voice_manager_t *manager)
{
    if (manager == NULL)
    {
        return;
    }

    if (manager->active_voice_idx >= 0)
    {
        sdio_status_t status;
        while ((status = rp2350_sdio_rx_poll(NULL)) == SDIO_BUSY) {
            mp_event_handle_nowait();
        }
    }

    // Free the entire contiguous allocation (all voices in one call)
    if (manager->voices != NULL)
    {
        m_tracked_free(manager->voices);
        manager->voices = NULL;
    }

    // Free amplitude buffer
    if (manager->amplitude_buffer != NULL)
    {
        m_tracked_free(manager->amplitude_buffer);
        manager->amplitude_buffer = NULL;
    }

    // Free accumulator buffer
    if (manager->accumulator_buffer != NULL)
    {
        m_tracked_free(manager->accumulator_buffer);
        manager->accumulator_buffer = NULL;
    }

    manager->num_voices = 0;
    manager->chunk_size = 0;
    manager->active_voice_idx = -1;
    manager->transfer_num_blocks = 0;
    manager->transfer_blocks_completed = 0;
}

// Internal function to start transfer on a specific voice
static sdio_status_t sdio_voice_start_transfer(sdio_voice_t *voice, uint32_t *blocks_started)
{
    // Initialize to 0 by default (used in all error/early return cases)
    *blocks_started = 0;

    if (voice == NULL)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    // Calculate how many sectors we can fill
    uint32_t free_sectors = RING_SIZE - voice->sectors_filled;
    if (free_sectors == 0)
    {
        return SDIO_OK; // Buffer is full
    }

    // Check how many bytes remain to read
    if (voice->sd_bytes_read >= voice->sd_size_bytes)
    {
        return SDIO_OK; // Reached end of data
    }
    uint32_t bytes_remaining = voice->sd_size_bytes - voice->sd_bytes_read;

    // Calculate how many sectors contain real data to read
    uint32_t sectors_with_data = (bytes_remaining + SDIO_BLOCK_SIZE - 1) / SDIO_BLOCK_SIZE;

    // Limit to free sectors available
    uint32_t sectors_to_transfer = free_sectors;
    if (sectors_to_transfer > sectors_with_data)
    {
        sectors_to_transfer = sectors_with_data;
    }

    #if DEBUG
    mp_printf(&mp_plat_print, "START TX: sd_size_bytes=%u sd_bytes_read=%u free_sectors=%u bytes_remaining=%u sectors_with_data=%u sectors_to_transfer=%u\n", voice->sd_size_bytes, voice->sd_bytes_read, free_sectors, bytes_remaining, sectors_with_data, sectors_to_transfer);
    #endif

    // Limit to maximum blocks per request
    if (sectors_to_transfer > SDIO_MAX_BLOCKS_PER_REQ)
    {
        sectors_to_transfer = SDIO_MAX_BLOCKS_PER_REQ;
    }

    // Send SD card commands to start the read
    uint32_t response;
    sdio_status_t status;

    // Set block count for the transfer
    status = rp2350_sdio_command_u32(SD_CMD_SET_BLOCK_COUNT, sectors_to_transfer, &response, 0);
    if (status != SDIO_OK)
    {
        return status;
    }

    // Send READ_MULTIPLE_BLOCK command
    status = rp2350_sdio_command_u32(SD_CMD_READ_MULTIPLE_BLOCK, voice->sd_current_sector, &response, SDIO_FLAG_STOP_CLK);
    if (status != SDIO_OK)
    {
        return status;
    }

    // Setup scatter gather addresses
    static uint8_t *scatter_addrs[SDIO_MAX_BLOCKS_PER_REQ];

    for (uint32_t i = 0; i < sectors_to_transfer; i++)
    {
        uint32_t sector_idx = (voice->write_sector_idx + i) % RING_SIZE;
        scatter_addrs[i] = ((uint8_t *) voice->buffer) + sector_idx * SDIO_BLOCK_SIZE;
    }

    // Start the DMA receive
    status = rp2350_sdio_rx_start_scatter(scatter_addrs, sectors_to_transfer, SDIO_BLOCK_SIZE);
    if (status != SDIO_OK)
    {
        return status;
    }

    *blocks_started = sectors_to_transfer;
    return SDIO_OK;
}

sdio_status_t sdio_voice_manager_update(sdio_voice_manager_t *manager)
{
    if (manager == NULL)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    #if DEBUG
    mp_printf(&mp_plat_print, "Update\n");
    #endif

    if (manager->active_voice_idx >= 0)
    {
        // Transfer in progress, check status
        uint32_t blocks_complete = 0;

        sdio_voice_t *voice = sdio_voice_manager_get(manager, manager->active_voice_idx);
        if (voice == NULL)
        {
            // Should never happen, but safety check
            manager->active_voice_idx = -1;
            return SDIO_ERR_INVALID_PARAM;
        }

        sdio_status_t status = rp2350_sdio_rx_poll(&blocks_complete);

        #if DEBUG
        mp_printf(&mp_plat_print, "TX on %u, status=%u, ret complete=%u current transfer_blocks_completed=%u\n", manager->active_voice_idx, status, blocks_complete, manager->transfer_blocks_completed);
        #endif

        // Update sectors_filled and sd_bytes_read incrementally
        if (blocks_complete > manager->transfer_blocks_completed)
        {
            #if DEBUG
            mp_printf(&mp_plat_print, "voice#%u sectors_filled=%u sd_bytes_read=%u sd_current_sector=%u\n", manager->active_voice_idx, voice->sectors_filled, voice->sd_bytes_read, voice->sd_current_sector);
            #endif

            // Calculate how many NEW blocks were completed since last update
            uint32_t new_blocks = blocks_complete - manager->transfer_blocks_completed;

            #if DEBUG
            mp_printf(&mp_plat_print, "voice#%u new_blocks=%u\n", manager->active_voice_idx, new_blocks);
            #endif

            voice->sectors_filled += new_blocks;
            voice->sd_bytes_read += new_blocks * SDIO_BLOCK_SIZE;
            voice->sd_current_sector += new_blocks;
            manager->transfer_blocks_completed = blocks_complete;
            #if DEBUG
            mp_printf(&mp_plat_print, "voice#%u sectors_filled=%u sd_bytes_read=%u sd_current_sector=%u\n", manager->active_voice_idx, voice->sectors_filled, voice->sd_bytes_read, voice->sd_current_sector);
            #endif
        }

        // If transfer still in progress, return BUSY
        if (status == SDIO_BUSY)
        {
            return SDIO_BUSY;
        }

        // Transfer complete - update write position and zero-fill if needed
        if (status == SDIO_OK)
        {
            // Update write position in voice buffer
            voice->write_sector_idx = (voice->write_sector_idx + manager->transfer_num_blocks) % RING_SIZE;

            #if DEBUG
            mp_printf(&mp_plat_print, "voice#%u rx complete write_sector_idx=%u\n", manager->active_voice_idx, voice->write_sector_idx);
            #endif

            // Zero-fill sectors that are past sd_size_bytes
            if (voice->sd_bytes_read > voice->sd_size_bytes)
            {
                #if DEBUG
                mp_printf(&mp_plat_print, "DO WE ZERO?: sd_size_bytes=%u sd_bytes_read=%u\n", voice->sd_size_bytes, voice->sd_bytes_read);
                #endif

                uint32_t last_valid_byte = voice->sd_size_bytes % SDIO_BLOCK_SIZE;
                uint32_t last_sector_idx = (voice->sd_size_bytes / SDIO_BLOCK_SIZE) % RING_SIZE;

                // Zero-fill tail of the last partial sector
                if (last_valid_byte > 0)
                {
                    #if DEBUG
                    mp_printf(&mp_plat_print, "voice#%u zero fill partial sector %u start %u len %u\n",
                              manager->active_voice_idx, last_sector_idx,
                              last_valid_byte, SDIO_BLOCK_SIZE - last_valid_byte);
                    #endif
                    memset((uint8_t*)voice->buffer + last_sector_idx * SDIO_BLOCK_SIZE + last_valid_byte,
                           0, SDIO_BLOCK_SIZE - last_valid_byte);
                }

                // Zero-fill all free sectors between last_sector_idx+1 and the read pointer (wrap-around)
                uint32_t read_sector_idx = (voice->bytes_consumed / SDIO_BLOCK_SIZE) % RING_SIZE;
                uint32_t zero_sector = (last_sector_idx + 1) % RING_SIZE;
                while (zero_sector != read_sector_idx)
                {
                    #if DEBUG
                    mp_printf(&mp_plat_print, "voice#%u zero fill free sector %u\n",
                              manager->active_voice_idx, zero_sector);
                    #endif
                    memset((uint8_t*)voice->buffer + zero_sector * SDIO_BLOCK_SIZE,
                           0, SDIO_BLOCK_SIZE);
                    voice->sectors_filled++;
                    voice->sd_bytes_read += SDIO_BLOCK_SIZE;
                    zero_sector = (zero_sector + 1) % RING_SIZE;
                }
            }
        }

        // Clear active transfer
        manager->active_voice_idx = -1;
        manager->transfer_num_blocks = 0;
        manager->transfer_blocks_completed = 0;

        if (status != SDIO_OK) {
            return status;
        }
    }

    // No transfer in progress, find the least filled voice
    sdio_voice_t *least_filled_voice = NULL;
    int least_filled_idx = -1;
    uint32_t min_filled = RING_SIZE + 1;

    for (uint32_t i = 0; i < manager->num_voices; i++)
    {
        sdio_voice_t *voice = sdio_voice_manager_get(manager, i);
        if (voice == NULL)
        {
            continue; // Should never happen, skip this voice
        }

        // Only consider active voices (sd_size_bytes > 0)
        if (voice->sd_size_bytes > 0 &&
            voice->sectors_filled < min_filled)
        {
            min_filled = voice->sectors_filled;
            least_filled_voice = voice;
            least_filled_idx = i;
        }
    }

    // Start transfer on least filled voice
    if (least_filled_voice != NULL)
    {

        #if DEBUG
        mp_printf(&mp_plat_print, "voice#%u try start rx with %u filled\n", least_filled_idx, min_filled);
        #endif

        if (min_filled < 5) {

            uint32_t blocks_started = 0;
            sdio_status_t status = sdio_voice_start_transfer(least_filled_voice, &blocks_started);

            if (status == SDIO_OK && blocks_started > 0)
            {
                manager->active_voice_idx = least_filled_idx;
                manager->transfer_num_blocks = blocks_started;
                manager->transfer_blocks_completed = 0;  // Reset for new transfer
                #if DEBUG
                mp_printf(&mp_plat_print, "voice#%u START RX for %u blocks\n", least_filled_idx, blocks_started);
                #endif

            }

            return status;
        }
    }

    return SDIO_OK;
}

uint32_t sdio_voice_available(const sdio_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0;
    }

    // Return bytes written by DMA minus bytes consumed by reader
    return voice->sd_bytes_read - voice->bytes_consumed;
}

void sdio_voice_consume(sdio_voice_t *voice, uint32_t bytes_read)
{
    if (voice == NULL || bytes_read == 0)
    {
        return;
    }

    // Calculate how many complete sectors were consumed before this call
    uint32_t sectors_before = voice->bytes_consumed / SDIO_BLOCK_SIZE;

    // Update total bytes consumed
    voice->bytes_consumed += bytes_read;

    // Calculate how many complete sectors are consumed after this call
    uint32_t sectors_after = voice->bytes_consumed / SDIO_BLOCK_SIZE;

    // Calculate how many NEW complete sectors were freed
    uint32_t sectors_freed = sectors_after - sectors_before;

    // Update sectors_filled (can't free more than what's filled)
    if (sectors_freed > voice->sectors_filled)
    {
        sectors_freed = voice->sectors_filled;
    }
    voice->sectors_filled -= sectors_freed;
}

void sdio_voice_print_diag(const sdio_voice_t *voice, const char *label)
{
    if (voice == NULL)
    {
        mp_printf(&mp_plat_print, "[%s] Voice is NULL\n", label ? label : "VOICE");
        return;
    }

    mp_printf(&mp_plat_print, "=== Voice Diagnostic: %s ===\n", label ? label : "VOICE");
    mp_printf(&mp_plat_print, "  SD Card State:\n");
    mp_printf(&mp_plat_print, "    Current sector:    %u\n", voice->sd_current_sector);
    mp_printf(&mp_plat_print, "    Size (bytes):      %u\n", voice->sd_size_bytes);
    mp_printf(&mp_plat_print, "    Bytes read:        %u\n", voice->sd_bytes_read);
    mp_printf(&mp_plat_print, "  Voice Buffer State:\n");
    mp_printf(&mp_plat_print, "    Write sector idx:  %u\n", voice->write_sector_idx);
    mp_printf(&mp_plat_print, "    Sectors filled:    %u / %u\n", voice->sectors_filled, RING_SIZE);
    mp_printf(&mp_plat_print, "    Bytes consumed:    %u\n", voice->bytes_consumed);
    mp_printf(&mp_plat_print, "  Computed Values:\n");
    mp_printf(&mp_plat_print, "    Available bytes:   %u\n", voice->sd_bytes_read - voice->bytes_consumed);
    mp_printf(&mp_plat_print, "    Progress:          %u / %u (%.1f%%)\n",
              voice->sd_bytes_read, voice->sd_size_bytes,
              voice->sd_size_bytes > 0 ? (100.0f * voice->sd_bytes_read / voice->sd_size_bytes) : 0.0f);
    mp_printf(&mp_plat_print, "    Free sectors:      %u\n", RING_SIZE - voice->sectors_filled);
    mp_printf(&mp_plat_print, "===================================\n");
}

void sdio_voice_manager_print_diag(const sdio_voice_manager_t *manager, const char *label)
{
    if (manager == NULL)
    {
        mp_printf(&mp_plat_print, "[%s] Manager is NULL\n", label ? label : "MANAGER");
        return;
    }

    mp_printf(&mp_plat_print, "\n");
    mp_printf(&mp_plat_print, "========== Voice Manager Diagnostic: %s ==========\n", label ? label : "MANAGER");
    mp_printf(&mp_plat_print, "  Manager State:\n");
    mp_printf(&mp_plat_print, "    Number of voices:        %u\n", manager->num_voices);
    mp_printf(&mp_plat_print, "    Active voice index:      %d", manager->active_voice_idx);
    if (manager->active_voice_idx >= 0)
    {
        mp_printf(&mp_plat_print, " (transfer in progress)\n");
        mp_printf(&mp_plat_print, "    Transfer blocks:         %u\n", manager->transfer_num_blocks);
        mp_printf(&mp_plat_print, "    Transfer completed:      %u\n", manager->transfer_blocks_completed);
    }
    else
    {
        mp_printf(&mp_plat_print, " (idle)\n");
    }

    mp_printf(&mp_plat_print, "\n  Individual Voices:\n");
    for (uint32_t i = 0; i < manager->num_voices; i++)
    {
        sdio_voice_t *voice = sdio_voice_manager_get((sdio_voice_manager_t *)manager, i);
        if (voice == NULL)
        {
            mp_printf(&mp_plat_print, "    [%u] ERROR: NULL\n", i);
            continue;
        }

        bool is_active = (voice->sd_size_bytes > 0);
        const char *status = (manager->active_voice_idx == (int)i) ? "TRANSFERRING" :
                            (is_active ? "ACTIVE" : "INACTIVE");

        mp_printf(&mp_plat_print, "    [%u] %s", i, status);
        if (is_active)
        {
            uint32_t available = voice->sd_bytes_read - voice->bytes_consumed;
            float progress = voice->sd_size_bytes > 0 ? (100.0f * voice->sd_bytes_read / voice->sd_size_bytes) : 0.0f;
            mp_printf(&mp_plat_print, " - Sector:%u Size:%u Read:%u Consumed:%u Avail:%u (%.1f%%) Filled:%u/%u",
                     voice->sd_current_sector, voice->sd_size_bytes, voice->sd_bytes_read,
                     voice->bytes_consumed, available, progress, voice->sectors_filled, RING_SIZE);
        }
        mp_printf(&mp_plat_print, "\n");
    }
    mp_printf(&mp_plat_print, "===========================================================\n");
    mp_printf(&mp_plat_print, "\n");
}

void sdio_voice_fill_amplitude(sdio_voice_manager_t *manager, Q1_14_t amplitude)
{
    if (manager == NULL || manager->amplitude_buffer == NULL)
    {
        return;
    }

    // Fill the entire amplitude buffer with the given value
    for (uint32_t i = 0; i < manager->chunk_size; i++)
    {
        manager->amplitude_buffer[i] = amplitude;
    }
}

uint32_t sdio_voice_fill_chunk(sdio_voice_manager_t *manager)
{
    uint32_t nb_active = 0;
    if (manager == NULL)
    {
        return nb_active;
    }

    // Clear accumulator buffer
    memset(manager->accumulator_buffer, 0, manager->chunk_size * sizeof(Q17_14_t));

    manager->free_voice_idx = -1;

    for (uint32_t i = 0; i < manager->num_voices; i++)
    {
        sdio_voice_t *voice = &manager->voices[i];

        if (voice->state == VOICE_INIT && voice->sectors_filled == RING_SIZE)
        {
            voice->state = VOICE_RUNNING;
        }

        if (voice->state == VOICE_RUNNING)
        {
            _voice_fill_bloc(voice, manager->amplitude_buffer,
                             manager->accumulator_buffer, manager->chunk_size);
            nb_active += 1;
        }

        if (voice->state == VOICE_CANCEL && manager->active_voice_idx != (int)i)
        {
            voice->state = VOICE_FREE;
        }

        if (voice->state == VOICE_FREE && manager->free_voice_idx == -1)
        {
            manager->free_voice_idx = (int)i;
        }
    }
    return nb_active;
}

// Voices
static _Bool _voice_fill_bloc(sdio_voice_t * voice, const Q1_14_t * amp, Q17_14_t * dest, uint32_t count) {
    omega_t pos = voice->omega;

    #if DEBUG
    mp_printf(&mp_plat_print, "Fill block: state %u sector %u, pos %u, sample %u, count %u\n", voice->state, voice->sd_current_sector, pos, pos>>OMEGA_SHIFT, count);
    #endif

    uint32_t nb_samples = (voice->omega_inc * count) >> OMEGA_SHIFT;
    uint32_t nb_samples_needed = nb_samples + 1;

    uint32_t samples_avail = bytes_to_samples(sdio_voice_available(voice));

    if (nb_samples_needed > samples_avail) {
        mp_printf(&mp_plat_print, "underrun got %u needs %u\n", samples_avail, nb_samples_needed);
        voice->state = VOICE_CANCEL;
        return false;
    }

    // Current absolute sample position (bytes_consumed is in bytes, /2 for int16)
    uint32_t abs_sample = bytes_to_samples(voice->bytes_consumed);

    const bool has_loop = (voice->loop_end_sample != 0);

    // Fill loop cache when we are about to play loop_start_sample in this block.
    // Captured from the sector-aligned boundary before loop_start_sample.
    if (has_loop && voice->loop_cache_samples == 0 &&
        abs_sample <= voice->loop_start_sample &&
        voice->loop_start_sample < abs_sample + nb_samples) {

        uint32_t aligned_start_sample = (voice->loop_start_sample / SECTOR_SAMPLES) * SECTOR_SAMPLES;
        // samples available from aligned_start_sample onward, rounded down to full sectors
        uint32_t samples_from_aligned = samples_avail - (aligned_start_sample - abs_sample);
        uint32_t cache_count = (samples_from_aligned / SECTOR_SAMPLES) * SECTOR_SAMPLES;
        if (cache_count > LOOP_CACHE_SAMPLES) cache_count = LOOP_CACHE_SAMPLES;

        ring_read(voice->buffer, aligned_start_sample, voice->loop_cache, cache_count);

        voice->loop_cache_samples = cache_count;
        #if DEBUG
        mp_printf(&mp_plat_print, "Loop cache filled: aligned_start=%u loop_start=%u count=%u\n",
                  aligned_start_sample, voice->loop_start_sample, cache_count);
        #endif
    }

    // Loop inject: when loop_end_sample falls within this block, reorganise the ring buffer
    // so that the read loop continues seamlessly into the loop body.
    //
    // At the trigger point:
    //   remaining = loop_end_sample - abs_sample  (tail samples before the loop point)
    //   The read loop will consume them, then must continue from loop_start_sample.
    //
    // Ring reorganisation (B before A to avoid overwriting source data):
    //   Step B: move the tail [abs_sample..loop_end[ to just before loop_start % RING_SAMPLES.
    //   Step A: write loop_cache at loop_start % RING_SAMPLES.
    // Then fix voice state so that bytes_consumed points to the virtual start of this read
    // (loop_start - remaining), and sdio_voice_consume(nb_samples*2) will land correctly.
    //
    if (has_loop && voice->loop_cache_samples > 0 &&
        abs_sample <= voice->loop_end_sample &&
        voice->loop_end_sample < abs_sample + nb_samples_needed) {

        uint32_t loop_start = voice->loop_start_sample;
        uint32_t loop_end   = voice->loop_end_sample;
        uint32_t remaining  = loop_end - abs_sample;  // samples still to play before loop_start

        // Step B: move the tail [abs_sample..loop_end[ to just before loop_start in the ring.
        uint32_t tail_dst = (loop_start + RING_SAMPLES - remaining) % RING_SAMPLES;
        ring_copy(voice->buffer, tail_dst, abs_sample, remaining);

        // Step A: inject loop_cache at loop_start % RING_SAMPLES (from external buffer, no overlap).
        ring_write(voice->buffer, loop_start, voice->loop_cache, voice->loop_cache_samples);

        // Fix up state to reflect the new start-of-read position: loop_start - remaining.
        // sdio_voice_consume(nb_samples*2) below will then advance bytes_consumed correctly.
        uint32_t aligned_start_sample = (loop_start / SECTOR_SAMPLES) * SECTOR_SAMPLES;
        uint32_t cache_end_sample     = aligned_start_sample + voice->loop_cache_samples;
        voice->bytes_consumed   = samples_to_bytes(loop_start - remaining);
        voice->sd_bytes_read    = samples_to_bytes(aligned_start_sample + voice->loop_cache_samples);
        voice->write_sector_idx = (cache_end_sample / SECTOR_SAMPLES) % RING_SIZE;
        // sectors_filled: full sectors available from bytes_consumed onward
        uint32_t avail_bytes    = voice->sd_bytes_read - (voice->bytes_consumed / SDIO_BLOCK_SIZE) * SDIO_BLOCK_SIZE;
        voice->sectors_filled   = avail_bytes / SDIO_BLOCK_SIZE;
        if (voice->sectors_filled > RING_SIZE) voice->sectors_filled = RING_SIZE;

        #if DEBUG
        mp_printf(&mp_plat_print, "Loop inject: loop_end=%u loop_start=%u remaining=%u cache_samples=%u\n",
                  loop_end, loop_start, remaining, voice->loop_cache_samples);
        #endif
    }

    while(count--) {
        int16_t sample = voice->buffer[pos>>OMEGA_SHIFT];
        pos += voice->omega_inc;
        *dest++ += (sample * (*amp++));
    }

    voice->omega = pos;
    sdio_voice_consume(voice, samples_to_bytes(nb_samples));

    bool running = voice->bytes_consumed <= voice->sd_size_bytes;

    if (!running) {
        voice->state = VOICE_DONE;
    }

    #if DEBUG
    mp_printf(&mp_plat_print, "After fill: pos %u, sample %u, running %u\n", pos, pos>>OMEGA_SHIFT, running);
    #endif

    return running;
}
