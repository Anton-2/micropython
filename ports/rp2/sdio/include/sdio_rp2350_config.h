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
    uint8_t clk_pin;
    uint8_t cmd_pin;
    uint8_t d0_pin;
    bool initialized;
} sdio_resources_t;

// Global resources instance
extern sdio_resources_t g_sdio_resources;

// Function to allocate SDIO resources (called from machine_sdcard.c)
int sdio_find_ressources(uint8_t clk_pin, uint8_t cmd_pin, uint8_t d0_pin);
void sdio_free_resources(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

// Timeouts for operations, in microseconds
// #define SDIO_CMD_TIMEOUT_US 2000
// #define SDIO_TRANSFER_TIMEOUT_US (1000 * 1000)
// #define SDIO_INIT_TIMEOUT_US (1000 * 1000)

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

// DMA IRQ: fixed to 1
#define SDIO_DMAIRQ_IDX 1
#define SDIO_DMAIRQ DMA_IRQ_1

// PIO IOBASE: forced to 0 to support pins 0-31 only
// (cannot be dynamic due to compile-time usage in external sdio_rp2350 files)
#define SDIO_PIO_IOBASE 0

// GPIO pins (dynamic, from g_sdio_resources)
#define SDIO_CLK (g_sdio_resources.clk_pin)
#define SDIO_CMD (g_sdio_resources.cmd_pin)
#define SDIO_D0  (g_sdio_resources.d0_pin)
#define SDIO_D1  (g_sdio_resources.d0_pin + 1)
#define SDIO_D2  (g_sdio_resources.d0_pin + 2)
#define SDIO_D3  (g_sdio_resources.d0_pin + 3)



// 3V GND CLK DO CMD D3 D1 D2 DET
