// Board and hardware specific configuration
#define MICROPY_HW_BOARD_NAME                   "Adafruit Feather RP2350 with HSTX Port"
#define MICROPY_HW_FLASH_STORAGE_BYTES          (PICO_FLASH_SIZE_BYTES - 1024 * 1024)

#define MICROPY_HW_USB_VID (0x239A)
#define MICROPY_HW_USB_PID (0x80F2)

// PSRAM support
// #define MICROPY_HW_PSRAM_CS_PIN (8)
// #define MICROPY_HW_ENABLE_PSRAM (1)

// Flash configuration
// #define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64 // already done in pico-sdk/src/boards/include/boards/adafruit_feather_rp2350.h

//
// #define PICO_BOOT_STAGE2_CHOOSE_GENERIC_03H 1
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

// #ifndef PICO_FLASH_SPI_CLKDIV
// #define PICO_FLASH_SPI_CLKDIV 3
// #endif
