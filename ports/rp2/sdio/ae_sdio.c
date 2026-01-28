#include <stdint.h>
#include <hardware/pio.h>
#include "ae_sdio.h"
#include "sdio_rp2350.h"


static uint32_t result;

// status= command_u32(command, arg, flags), set result as cmd response
static mp_obj_t sdio_command_u32(mp_obj_t command_in, mp_obj_t arg_in, mp_obj_t flags_in) {

    const uint32_t command = mp_obj_get_int(command_in);
    uint32_t arg = mp_obj_get_int(arg_in);
    const uint32_t flags = mp_obj_get_int(flags_in);

    // mpy use 31 bit ints, top 8 bits of arg are in command 8-15, and real command in 0-7
    arg |= ((command>>8)&0xFF)<<24;

    const sdio_status_t status = rp2350_sdio_command_u32(command&0xFF, arg, &result, flags);

    return mp_obj_new_int(status);
}
static MP_DEFINE_CONST_FUN_OBJ_3(command_u32_obj, sdio_command_u32);


// status = command(command, arg, response_buffer, flag)
static mp_obj_t sdio_command(size_t n_args, const mp_obj_t *args) {
    const uint8_t command = mp_obj_get_int(args[0]);
    const uint32_t arg = mp_obj_get_int(args[1]);
    // args[2] is response buffer
    const uint32_t flags = mp_obj_get_int(args[3]);

    void *response_buf = NULL;
    int resp_bytes = 0;

    if (args[2] != mp_const_none) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_WRITE);
        response_buf = bufinfo.buf;
        resp_bytes = bufinfo.len;
    }

    const sdio_status_t status = rp2350_sdio_command(command, arg, response_buf, resp_bytes, flags);

    return mp_obj_new_int(status);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(command_obj, 4, 4, sdio_command);


// status = rx_start(buffer, [blocksize])
static mp_obj_t sdio_rx_start(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_WRITE);

    uint32_t blocksize = (n_args==2) ? mp_obj_get_int(args[1]) : SDIO_BLOCK_SIZE;

    const uint32_t num_blocks = (blocksize >= SDIO_BLOCK_SIZE) ? bufinfo.len / blocksize : 1;

    const sdio_status_t status = rp2350_sdio_rx_start(bufinfo.buf, num_blocks, blocksize);
    return mp_obj_new_int(status);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rx_start_obj, 1, 2, sdio_rx_start);

// status = sdio_rx_poll(), set response as blocks_complete
static mp_obj_t sdio_rx_poll() {
    const sdio_status_t status = rp2350_sdio_rx_poll(&result);
    return mp_obj_new_int(status);
}
static MP_DEFINE_CONST_FUN_OBJ_0(rx_poll_obj, sdio_rx_poll);

// status = sdio_stop()
static mp_obj_t sdio_stop() {
    const sdio_status_t status = rp2350_sdio_stop();
    return mp_obj_new_int(status);
}
static MP_DEFINE_CONST_FUN_OBJ_0(stop_obj, sdio_stop);

// sdio_init(mode)
static mp_obj_t sdio_init(mp_obj_t mode_in) {
    const rp2350_sdio_mode_t mode = mp_obj_get_int(mode_in);
    const rp2350_sdio_timing_t timmings = rp2350_sdio_get_timing(mode);
    rp2350_sdio_init(timmings);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(init_obj, sdio_init);

// result()
static mp_obj_t sdio_result() {
    return mp_obj_new_int(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(result_obj, sdio_result);

// is_busy()
static mp_obj_t sdio_is_busy() {
    return mp_obj_new_bool(gpio_get(SDIO_D0) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(is_busy_obj, sdio_is_busy);


static const mp_rom_map_elem_t ae_sdio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ae_sdio) },
    { MP_ROM_QSTR(MP_QSTR_command_u32), MP_ROM_PTR(&command_u32_obj) },
    { MP_ROM_QSTR(MP_QSTR_command), MP_ROM_PTR(&command_obj) },
    { MP_ROM_QSTR(MP_QSTR_rx_start), MP_ROM_PTR(&rx_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_rx_poll), MP_ROM_PTR(&rx_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&init_obj) },
    { MP_ROM_QSTR(MP_QSTR_result), MP_ROM_PTR(&result_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_busy), MP_ROM_PTR(&is_busy_obj) },
    { MP_ROM_QSTR(MP_QSTR_SDCard), MP_ROM_PTR(&machine_sdcard_type) },
};
static MP_DEFINE_CONST_DICT(ae_sdio_module_globals, ae_sdio_module_globals_table);

// Define module object.
const mp_obj_module_t ae_sdio_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ae_sdio_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_ae_sdio, ae_sdio_user_cmodule);

/*
 * make BOARD=AE_PICO2 USER_C_MODULES=./sdio/micropython.cmake -j 10
 * mpremote bootloader
 * picotool load -v build-AE_PICO2/firmware.elf
 * picotool reboot
 */
