// Board and hardware specific configuration
#define MICROPY_HW_BOARD_NAME                   "AE Pico2"
#define MICROPY_HW_ROMFS_BYTES                  (1024 * 1024)
#define MICROPY_HW_FLASH_STORAGE_BYTES          (PICO_FLASH_SIZE_BYTES - (1024*1024) -MICROPY_HW_ROMFS_BYTES)
