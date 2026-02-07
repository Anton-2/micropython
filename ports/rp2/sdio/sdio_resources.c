/* SDIO resource allocation for MicroPython */

#include "include/sdio_rp2350_config.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "py/mphal.h"

// Global resources instance
sdio_resources_t g_sdio_resources = {
    .pio = NULL,
    .sm = -1,
    .dma_chan_a = -1,
    .dma_chan_b = -1,
    .dma_irq_idx = 0,
    .initialized = false,
};

// PIO instances
static const PIO pio_instances[NUM_PIOS] = {pio0, pio1, pio2};


int sdio_find_ressources(void) {
    if (g_sdio_resources.initialized) {
        return 0; // Already found
    }

    for (uint8_t ch = 0; ch < NUM_DMA_CHANNELS ; ch++) {
        if (dma_channel_is_claimed(ch)) {
            continue;
        }
        if (g_sdio_resources.dma_chan_a >= 0) {
            g_sdio_resources.dma_chan_b = ch;
            break;
        } else {
            g_sdio_resources.dma_chan_a = ch;
        }
    }

    if (g_sdio_resources.dma_chan_b < 0) {
        return -1; // Failed to find free DMA channels
    }

    // Try to find a PIO with free state machine
    // TODO: check programm space
    PIO candidate_pio = NULL;
    int8_t free_sm = -1;

    for (uint8_t p = 0; p < NUM_PIOS && free_sm < 0; p++) {
        candidate_pio = pio_instances[p];

        // Check if there's a free state machine
        for (uint8_t sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
            if (!pio_sm_is_claimed(candidate_pio, sm)) {
                free_sm = sm;
                break;
            }
        }
    }

    if (free_sm<0) {
        return -1; // No free state machine
    }

    g_sdio_resources.pio = candidate_pio;
    g_sdio_resources.sm = free_sm;

    // Use DMA IRQ 1 by default
    g_sdio_resources.dma_irq_idx = 1;

    g_sdio_resources.initialized = true;

    return 0;
}

void sdio_free_resources(void) {
    if (!g_sdio_resources.initialized) {
        return;
    }

    // Unclaim DMA channels
    if (g_sdio_resources.dma_chan_a >= 0) {
        dma_channel_unclaim(g_sdio_resources.dma_chan_a);
    }
    if (g_sdio_resources.dma_chan_b >= 0) {
        dma_channel_unclaim(g_sdio_resources.dma_chan_b);
    }

    // Unclaim PIO state machine
    if (g_sdio_resources.pio && g_sdio_resources.sm >= 0) {
        pio_sm_unclaim(g_sdio_resources.pio, g_sdio_resources.sm);
    }

    g_sdio_resources.initialized = false;
    g_sdio_resources.pio = NULL;
}
