// Board and hardware specific configuration
#define MICROPY_HW_BOARD_NAME                   "AE Pico2 nousb"
#define MICROPY_HW_ROMFS_BYTES                  (768 * 1024)
#define MICROPY_HW_FLASH_STORAGE_BYTES          (PICO_FLASH_SIZE_BYTES - (1536*1024) - MICROPY_HW_ROMFS_BYTES)

// No USB
#define MICROPY_HW_ENABLE_UART_REPL             (1)
#define MICROPY_HW_ENABLE_USBDEV                (0)

#define MICROPY_HW_USB_CDC                      (0)
#define MICROPY_HW_USB_MSC                      (0)
#define MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE    (0)


#define MICROPY_HW_USB_HOST                     (1)
