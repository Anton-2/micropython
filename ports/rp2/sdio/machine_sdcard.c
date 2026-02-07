// Include MicroPython API.
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"
#include "include/sdio_rp2350.h"
#include "include/sdio_rp2350_config.h"

// SD Card commands
#define SD_CMD_GO_IDLE_STATE        0
#define SD_CMD_ALL_SEND_CID         2
#define SD_CMD_SEND_RELATIVE_ADDR   3
#define SD_CMD_SELECT_CARD          7
#define SD_CMD_SEND_IF_COND         8
#define SD_CMD_SEND_CSD             9
#define SD_CMD_STOP_TRANSMISSION    12
#define SD_CMD_SET_BLOCKLEN         16
#define SD_CMD_READ_SINGLE_BLOCK    17
#define SD_CMD_READ_MULTIPLE_BLOCK  18
#define SD_CMD_WRITE_BLOCK          24
#define SD_CMD_WRITE_MULTIPLE_BLOCK 25
#define SD_CMD_APP_CMD              55
#define SD_ACMD_SEND_OP_COND        41
#define SD_CMD_APP_SET_BUS_WIDTH    6

#define SDCARD_BLOCK_SIZE 512

typedef struct _machine_sdcard_obj_t {
    mp_obj_base_t base;
    uint32_t block_count;
    uint16_t block_len;
    bool initialized;
    uint8_t card_type;
    rp2350_sdio_mode_t timing_mode;
    uint8_t clk_pin;
    uint8_t cmd_pin;
    uint8_t d0_pin;
} machine_sdcard_obj_t;



// Helper function to initialize the SD card
static int sdcard_init_card(machine_sdcard_obj_t *self) {
    if (self->initialized) {
        return 0;
    }

    // Allocate SDIO resources (PIO, SM, DMA channels)
    if (sdio_find_ressources(self->clk_pin, self->cmd_pin, self->d0_pin) != 0) {
        return -MP_ENODEV; // Failed to allocate resources
    }

    uint32_t response;
    uint32_t ocr;
    uint32_t rca;
    sdio_status_t status;

    // Initialize SDIO interface at 400 kHz
    rp2350_sdio_timing_t timing = rp2350_sdio_get_timing(SDIO_INITIALIZE);
    rp2350_sdio_init(timing);

    // Wait for initial clock cycles
    mp_hal_delay_us(1000);

    // Establish initial connection with the card
    for (int retries = 0; retries < 5; retries++) {
        mp_hal_delay_us(1000);

        // CMD0: GO_IDLE_STATE (no response expected)
        rp2350_sdio_command(SD_CMD_GO_IDLE_STATE, 0, NULL, 0, SDIO_FLAG_NO_LOGMSG);

        mp_hal_delay_us(1000);

        // CMD8: SEND_IF_COND (check voltage range)
        status = rp2350_sdio_command_u32(SD_CMD_SEND_IF_COND, 0x1AA, &response, SDIO_FLAG_NO_LOGMSG);

        if (status == SDIO_OK && response == 0x1AA) {
            self->card_type = 2; // SDHC/SDXC
            break;
        }
    }

    if (response != 0x1AA || status != SDIO_OK) {
        return -MP_EIO;
    }

    // Initialize card with ACMD41
    uint32_t start = mp_hal_ticks_us();
    do {
        // CMD55: APP_CMD
        status = rp2350_sdio_command_u32(SD_CMD_APP_CMD, 0, &response, 0);
        if (status != SDIO_OK) {
            return -MP_EIO;
        }

        // ACMD41: SEND_OP_COND
        status = rp2350_sdio_command_u32(SD_ACMD_SEND_OP_COND,
                                          ((1 << 30) | (1 << 28) | (1 << 20)), // SDIO_CARD_OCR_MODE
                                          &ocr,
                                          SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG);
        if (status != SDIO_OK) {
            return -MP_EIO;
        }

        if ((mp_hal_ticks_us() - start) > 1000000) { // 1 second timeout
            return -MP_ETIMEDOUT;
        }
    } while (!(ocr & (1 << 31)));

    // Check if SDHC/SDXC
    if (ocr & (1 << 30)) {
        self->card_type = 2;
    } else {
        self->card_type = 1;
    }

    // CMD2: ALL_SEND_CID
    uint8_t cid[16];
    status = rp2350_sdio_command(SD_CMD_ALL_SEND_CID, 0, cid, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG);
    if (status != SDIO_OK) {
        return -MP_EIO;
    }

    // CMD3: SEND_RELATIVE_ADDR
    status = rp2350_sdio_command_u32(SD_CMD_SEND_RELATIVE_ADDR, 0, &rca, 0);
    if (status != SDIO_OK) {
        return -MP_EIO;
    }

    // CMD9: SEND_CSD (use RCA as argument)
    uint8_t csd[16];
    status = rp2350_sdio_command(SD_CMD_SEND_CSD, rca, csd, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG);
    if (status != SDIO_OK) {
        return -MP_EIO;
    }

    // Calculate block count based on CSD
    uint8_t csd_version = csd[0] >> 6;
    if (csd_version == 1) {
        // SDHC/SDXC: C_SIZE is in bytes 7-9
        uint32_t c_size = ((csd[7] & 0x3F) << 16) | (csd[8] << 8) | csd[9];
        self->block_count = (c_size + 1) * 1024;
    } else {
        // SDSC: C_SIZE and C_SIZE_MULT calculation
        uint32_t c_size = ((csd[6] & 0x03) << 10) | (csd[7] << 2) | ((csd[8] & 0xC0) >> 6);
        uint32_t c_size_mult = ((csd[9] & 0x03) << 1) | ((csd[10] & 0x80) >> 7);
        uint32_t read_bl_len = csd[5] & 0x0F;
        self->block_count = (c_size + 1) << (c_size_mult + read_bl_len + 2 - 9);
    }

    // CMD7: SELECT_CARD
    status = rp2350_sdio_command_u32(SD_CMD_SELECT_CARD, rca, &response, 0);
    if (status != SDIO_OK) {
        return -MP_EIO;
    }

    // CMD55: APP_CMD + ACMD6: SET_BUS_WIDTH (4-bit mode)
    status = rp2350_sdio_command_u32(SD_CMD_APP_CMD, rca, &response, 0);
    if (status != SDIO_OK) {
        return -MP_EIO;
    }

    status = rp2350_sdio_command_u32(SD_CMD_APP_SET_BUS_WIDTH, 2, &response, 0);
    if (status != SDIO_OK) {
        return -MP_EIO;
    }

    self->block_len = SDCARD_BLOCK_SIZE;

    // Switch to requested speed mode
    timing = rp2350_sdio_get_timing(self->timing_mode);
    rp2350_sdio_init(timing);

    self->initialized = true;
    return 0;
}

// present()
static mp_obj_t machine_sdcard_present(mp_obj_t self_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(self->initialized && self->block_count != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_sdcard_present_obj, machine_sdcard_present);

// readblocks(block_num, buf)
static mp_obj_t machine_sdcard_readblocks(mp_obj_t self_in, mp_obj_t block_num_in, mp_obj_t buf_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t block_num = mp_obj_get_int(block_num_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    if (!self->initialized) {
        mp_raise_OSError(MP_EPERM);
    }

    // Calculate address based on card type
    uint32_t address = (self->card_type == 2) ? block_num : (block_num * SDCARD_BLOCK_SIZE);
    uint32_t num_blocks = bufinfo.len / SDCARD_BLOCK_SIZE;

    sdio_status_t status;
    uint32_t response;

    if (num_blocks == 1) {
        // Single block read
        status = rp2350_sdio_command_u32(SD_CMD_SET_BLOCKLEN, SDCARD_BLOCK_SIZE, &response, 0);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        status = rp2350_sdio_command_u32(SD_CMD_READ_SINGLE_BLOCK, address, &response, SDIO_FLAG_STOP_CLK);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        status = rp2350_sdio_rx_start(bufinfo.buf, 1, SDCARD_BLOCK_SIZE);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        while ((status = rp2350_sdio_rx_poll(NULL)) == SDIO_BUSY) {
            mp_event_handle_nowait();
        }

        rp2350_sdio_stop();

        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }
    } else {
        // Multiple block read
        status = rp2350_sdio_command_u32(SD_CMD_READ_MULTIPLE_BLOCK, address, &response, SDIO_FLAG_STOP_CLK);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        status = rp2350_sdio_rx_start(bufinfo.buf, num_blocks, SDCARD_BLOCK_SIZE);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        while ((status = rp2350_sdio_rx_poll(NULL)) == SDIO_BUSY) {
            mp_event_handle_nowait();
        }

        // Send stop transmission command
        rp2350_sdio_command_u32(SD_CMD_STOP_TRANSMISSION, 0, &response, 0);
        rp2350_sdio_stop();

        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_sdcard_readblocks_obj, machine_sdcard_readblocks);

// writeblocks(block_num, buf)
static mp_obj_t machine_sdcard_writeblocks(mp_obj_t self_in, mp_obj_t block_num_in, mp_obj_t buf_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t block_num = mp_obj_get_int(block_num_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    if (!self->initialized) {
        mp_raise_OSError(MP_EPERM);
    }

    // Calculate address based on card type
    uint32_t address = (self->card_type == 2) ? block_num : (block_num * SDCARD_BLOCK_SIZE);
    uint32_t num_blocks = bufinfo.len / SDCARD_BLOCK_SIZE;

    sdio_status_t status;
    uint32_t response;

    if (num_blocks == 1) {
        // Single block write
        status = rp2350_sdio_command_u32(SD_CMD_SET_BLOCKLEN, SDCARD_BLOCK_SIZE, &response, 0);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        status = rp2350_sdio_command_u32(SD_CMD_WRITE_BLOCK, address, &response, SDIO_FLAG_STOP_CLK);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        status = rp2350_sdio_tx_start(bufinfo.buf, 1, SDCARD_BLOCK_SIZE);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        while ((status = rp2350_sdio_tx_poll(NULL)) == SDIO_BUSY) {
            mp_event_handle_nowait();
        }

        rp2350_sdio_stop();

        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }
    } else {
        // Multiple block write
        status = rp2350_sdio_command_u32(SD_CMD_WRITE_MULTIPLE_BLOCK, address, &response, SDIO_FLAG_STOP_CLK);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        status = rp2350_sdio_tx_start(bufinfo.buf, num_blocks, SDCARD_BLOCK_SIZE);
        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }

        while ((status = rp2350_sdio_tx_poll(NULL)) == SDIO_BUSY) {
            mp_event_handle_nowait();
        }

        // Send stop transmission command
        rp2350_sdio_command_u32(SD_CMD_STOP_TRANSMISSION, 0, &response, 0);
        rp2350_sdio_stop();

        if (status != SDIO_OK) {
            mp_raise_OSError(MP_EIO);
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_sdcard_writeblocks_obj, machine_sdcard_writeblocks);

// ioctl(op, arg)
static mp_obj_t machine_sdcard_ioctl(mp_obj_t self_in, mp_obj_t cmd_in, mp_obj_t arg_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t cmd = mp_obj_get_int(cmd_in);

    switch (cmd) {
        case MP_BLOCKDEV_IOCTL_INIT: {
            int ret = sdcard_init_card(self);
            return MP_OBJ_NEW_SMALL_INT(ret);
        }
        case MP_BLOCKDEV_IOCTL_DEINIT:
            self->initialized = false;
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_SYNC:
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(self->block_count);
        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(self->block_len);
        case MP_BLOCKDEV_IOCTL_BLOCK_ERASE: {
            // SD cards don't require explicit erase before write
            return MP_OBJ_NEW_SMALL_INT(0);
        }
        default:
            return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_sdcard_ioctl_obj, machine_sdcard_ioctl);

static mp_obj_t sdcard_obj_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    // Parse arguments
    enum { ARG_clk, ARG_cmd, ARG_d0, ARG_timing };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_clk, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0} },
        { MP_QSTR_cmd, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0} },
        { MP_QSTR_d0, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0} },
        { MP_QSTR_timing, MP_ARG_INT, {.u_int = SDIO_STANDARD} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    machine_sdcard_obj_t *self = mp_obj_malloc(machine_sdcard_obj_t, type);
    self->block_count = 0;
    self->block_len = SDCARD_BLOCK_SIZE;
    self->initialized = false;
    self->card_type = 0;
    self->timing_mode = args[ARG_timing].u_int;
    self->clk_pin = args[ARG_clk].u_int;
    self->cmd_pin = args[ARG_cmd].u_int;
    self->d0_pin = args[ARG_d0].u_int;

    // Validate timing mode
    if (self->timing_mode > SDIO_HIGHSPEED_OVERCLOCK) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid timing mode"));
    }

    return MP_OBJ_FROM_PTR(self);
}

static const mp_rom_map_elem_t sdcard_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_present),     MP_ROM_PTR(&machine_sdcard_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_readblocks),  MP_ROM_PTR(&machine_sdcard_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&machine_sdcard_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl),       MP_ROM_PTR(&machine_sdcard_ioctl_obj) },

    // Timing mode constants
    { MP_ROM_QSTR(MP_QSTR_INITIALIZE),            MP_ROM_INT(SDIO_INITIALIZE) },
    { MP_ROM_QSTR(MP_QSTR_MMC),                   MP_ROM_INT(SDIO_MMC) },
    { MP_ROM_QSTR(MP_QSTR_STANDARD),              MP_ROM_INT(SDIO_STANDARD) },
    { MP_ROM_QSTR(MP_QSTR_HIGHSPEED),             MP_ROM_INT(SDIO_HIGHSPEED) },
    { MP_ROM_QSTR(MP_QSTR_HIGHSPEED_OVERCLOCK),   MP_ROM_INT(SDIO_HIGHSPEED_OVERCLOCK) },
};
static MP_DEFINE_CONST_DICT(sdcard_locals_dict, sdcard_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_sdcard_type,
    MP_QSTR_SDCard,
    MP_TYPE_FLAG_NONE,
    make_new, sdcard_obj_make_new,
    locals_dict, &sdcard_locals_dict
    );
