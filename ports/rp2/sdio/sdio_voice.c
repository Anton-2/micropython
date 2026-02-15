#include "sdio_voice.h"
#include <string.h>
#include "py/mphal.h"
#include "py/runtime.h"

// SD Card command definitions
#define SD_CMD_SET_BLOCKLEN         16
#define SD_CMD_READ_SINGLE_BLOCK    17
#define SD_CMD_READ_MULTIPLE_BLOCK  18
#define SD_CMD_SET_BLOCK_COUNT      23

sdio_status_t sdio_voice_manager_init(sdio_voice_manager_t *manager,
                                             uint32_t num_voices)
{
    if (manager == NULL)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    if (num_voices == 0)
    {
        return SDIO_ERR_INVALID_PARAM;
    }

    // Allocate array of voice structures using MicroPython allocator
    // m_new0 allocates and zeros memory
    manager->voices = m_new0(sdio_voice_t, num_voices);


    manager->num_voices = num_voices;
    manager->active_voice_idx = -1;
    manager->transfer_num_blocks = 0;
    manager->transfer_blocks_completed = 0;

    return SDIO_OK;
}

sdio_status_t sdio_voice_start(sdio_voice_manager_t *manager,
                                      uint32_t voice_index,
                                      uint32_t sd_sector,
                                      uint32_t size_bytes)
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
    voice->bytes_consumed = 0;

    return SDIO_OK;
}

void sdio_voice_manager_free(sdio_voice_manager_t *manager)
{
    if (manager == NULL)
    {
        return;
    }

    // Free the entire contiguous allocation (all voices in one call)
    if (manager->voices != NULL)
    {
        m_free(manager->voices);
        manager->voices = NULL;
    }

    manager->num_voices = 0;
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

    if (manager->active_voice_idx >= 0)
    {
        // Transfer in progress, check status
        uint32_t blocks_complete = 0;
        sdio_status_t status = rp2350_sdio_rx_poll(&blocks_complete);

        sdio_voice_t *voice = sdio_voice_manager_get(manager, manager->active_voice_idx);
        if (voice == NULL)
        {
            // Should never happen, but safety check
            manager->active_voice_idx = -1;
            return SDIO_ERR_INVALID_PARAM;
        }

        // Update sectors_filled and sd_bytes_read incrementally
        if (blocks_complete > manager->transfer_blocks_completed)
        {
            // Calculate how many NEW blocks were completed since last update
            uint32_t new_blocks = blocks_complete - manager->transfer_blocks_completed;
            voice->sectors_filled += new_blocks;
            voice->sd_bytes_read += new_blocks * SDIO_BLOCK_SIZE;
            voice->sd_current_sector += new_blocks;
            manager->transfer_blocks_completed = blocks_complete;
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

            // Zero-fill the last sector if we've read past sd_size_bytes
            if (voice->sd_bytes_read > voice->sd_size_bytes)
            {
                uint32_t last_valid_byte = voice->sd_size_bytes % SDIO_BLOCK_SIZE;
                if (last_valid_byte > 0)  // Partial sector
                {
                    uint32_t last_sector_idx = (voice->sd_size_bytes / SDIO_BLOCK_SIZE) % RING_SIZE;
                    memset(voice->buffer + last_sector_idx * SDIO_BLOCK_SIZE + last_valid_byte,
                           0, SDIO_BLOCK_SIZE - last_valid_byte);
                }
            }
        }

        // Clear active transfer
        manager->active_voice_idx = -1;
        manager->transfer_num_blocks = 0;
        manager->transfer_blocks_completed = 0;

        return status;
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
        uint32_t blocks_started = 0;
        sdio_status_t status = sdio_voice_start_transfer(least_filled_voice, &blocks_started);

        if (status == SDIO_OK && blocks_started > 0)
        {
            manager->active_voice_idx = least_filled_idx;
            manager->transfer_num_blocks = blocks_started;
            manager->transfer_blocks_completed = 0;  // Reset for new transfer
        }

        return status;
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


// Voices
_Bool _voice_fill_bloc(sdio_voice_t * voice, const Q16_15_t * amp, Q16_15_t * dest, uint32_t count) {
    omega_t pos = voice->omega;

    #if DEBUG
    mp_printf(&mp_plat_print, "Fill block: sector %u, pos %u, sample %u, count %u\n", voice->current_sector, pos, pos>>OMEGA_SHIFT, count);
    #endif

    uint32_t nb_samples = (voice->omega_inc * count) >> OMEGA_SHIFT;
    uint32_t nb_samples_needed =nb_samples+1;

    uint32_t samples_avail = sdio_voice_available(voice) / 2;

    if (nb_samples_needed < samples_avail) {
        mp_printf(&mp_plat_print, "underrun got %u needs %u\n", samples_avail, nb_samples_needed);
        voice->state = VOICE_CANCEL;
        return false;
    }

    while(count--) {
        int16_t sample = voice->buffer[pos>>OMEGA_SHIFT];
        // mp_printf(&mp_plat_print, "pos %u, sample %04x, ", pos>>FRAC_BITS, sample);
        pos +=  voice->omega_inc;
        *dest++ += (sample * (*amp++));
    }

    voice->omega = pos;
    sdio_voice_consume(voice, nb_samples*2);

    bool running = voice->bytes_consumed <= voice->sd_size_bytes;

    if (! running) {
        voice->state = VOICE_DONE;
    }

    #if DEBUG
    mp_printf(&mp_plat_print, "After fill: pos %u, sample %u, running %u", pos, pos>>OMEGA_SHIFT, running);
    #endif

    return running;
}
