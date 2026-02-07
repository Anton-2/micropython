/* Copy this file to rp2350_config.h in your own project */

#pragma once

// SDIO driver can optionally log debug and error messages.
// To enable this, uncomment the lines below and define the
// sdio_log() function in your own code.

#ifdef __cplusplus
extern "C" {
#endif

#include "py/runtime.h"
#include "hardware/pio.h"

#define SDIO_CRITMSG(txt, arg1, arg2) mp_printf(&mp_plat_print, "CRITICAL: %s %08X %08X\n", txt, arg1, arg2);
#define SDIO_ERRMSG(txt, arg1, arg2) mp_printf(&mp_plat_print, "ERROR: %s %08X %08X\n", txt, arg1, arg2);
// #define SDIO_DBGMSG(txt, arg1, arg2) mp_printf(&mp_plat_print, "DEBUG: %s %08X %08X\n", txt, arg1, arg2);


// Dynamic resource allocation structure
typedef struct {
    PIO pio;
    int8_t sm;
    int8_t dma_chan_a;
    int8_t dma_chan_b;
    int8_t dma_irq_idx;
    bool initialized;
} sdio_resources_t;

// Global resources instance
extern sdio_resources_t g_sdio_resources;

// Function to allocate SDIO resources (called from machine_sdcard.c)
int sdio_find_ressources(void);
void sdio_free_resources(void);

#ifdef __cplusplus
} /* extern "C" */
#endif


// void sdio_log(const char *txt, uint32_t arg1, uint32_t arg2);
// #define SDIO_ERRMSG(txt, arg1, arg2) sdio_log(txt, arg1, arg2)
// #define SDIO_DBGMSG(txt, arg1, arg2) sdio_log(txt, arg1, arg2)

// Maximum number of blocks queued for transmission or reception
// #define SDIO_MAX_BLOCKS_PER_REQ 128

// Timeouts for operations, in microseconds
// #define SDIO_CMD_TIMEOUT_US 2000
// #define SDIO_TRANSFER_TIMEOUT_US (1000 * 1000)
// #define SDIO_INIT_TIMEOUT_US (1000 * 1000)

// Enable the definition of SdFat library SdioCard class
// #define SDIO_USE_SDFAT 1

// Prefetch buffer in SdioCard, bytes
// Set to 0 to disable
// #define SDIO_SDFAT_PREFETCH_BUFFER 2048

// Number of retries for sector read/write
// #define SDIO_MAX_RETRYCOUNT 1

// When testing SDIO communication during init, which sector to read/write
// #define SDIO_COMMUNICATION_TEST_SECTOR_IDX 0

// Enable write check during initialization
// This writes back the same data as was read from the SD card
// #define SDIO_COMMUNICATION_TEST_DO_WRITE 1

// Default speed to use for SDIO communication
// If communication doesn't work, speed is automatically dropped
// #define SDIO_DEFAULT_SPEED SDIO_HIGHSPEED


// PIO block to use
#define SDIO_PIO (g_sdio_resources.pio)
#define SDIO_SM (g_sdio_resources.sm)

// GPIO configuration
#define SDIO_GPIO_FUNC (SDIO_PIO == pio0 ? GPIO_FUNC_PIO0 : (SDIO_PIO == pio1 ? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2))
#define SDIO_GPIO_SLEW GPIO_SLEW_RATE_FAST
#define SDIO_GPIO_DRIVE GPIO_DRIVE_STRENGTH_8MA

// DMA channels to use
#define SDIO_DMACH_A (g_sdio_resources.dma_chan_a)
#define SDIO_DMACH_B (g_sdio_resources.dma_chan_b)
#define SDIO_DMAIRQ_IDX (g_sdio_resources.dma_irq_idx)
#define SDIO_DMAIRQ (g_sdio_resources.dma_irq_idx == 0 ? DMA_IRQ_0 : DMA_IRQ_1)

// GPIO pins
/*
#define SDIO_CLK 16
#define SDIO_CMD 17
#define SDIO_D0  18
#define SDIO_D1  19
#define SDIO_D2  20
#define SDIO_D3  21
*/

#define SDIO_CLK 3
#define SDIO_CMD 4
#define SDIO_D0  5
#define SDIO_D1  6
#define SDIO_D2  7
#define SDIO_D3  8



// 3V GND CLK DO CMD D3 D1 D2 DET
