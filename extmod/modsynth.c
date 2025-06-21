
#include "py/obj.h"
#include "py/mphal.h"
#include "py/runtime.h"

#include <stdio.h>
#include "dexed_wrapper.h"

static mp_obj_t init(mp_obj_t nb_voices_in, mp_obj_t rate_in) {
    mp_int_t nb_voices = mp_obj_get_int(nb_voices_in);
    mp_int_t rate = mp_obj_get_int(rate_in);

    dexed_init(nb_voices, rate);
    printf("Init: %u voices, sr=%u\r\n", nb_voices, rate);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(init_obj, init);

static mp_obj_t destroy(void) {
    dexed_destroy();
    printf("Done !\r\n");
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(destroy_obj, destroy);

static mp_obj_t get_samples16(mp_obj_t buf_in) {
    // get the buffer to read into
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    dexed_get_samples16(bufinfo.buf, bufinfo.len);

    return mp_const_none;
}

static mp_obj_t get_samples32(mp_obj_t buf_in) {
    // get the buffer to read into
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    dexed_get_samples32(bufinfo.buf, bufinfo.len);

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(get_samples32_obj, get_samples32);
static MP_DEFINE_CONST_FUN_OBJ_1(get_samples16_obj, get_samples16);



static mp_obj_t keyup(mp_obj_t pitch_in) {
    mp_int_t pitch = mp_obj_get_int(pitch_in);
    dexed_keyup(pitch);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(keyup_obj, keyup);

static mp_obj_t keydown(mp_obj_t pitch_in, mp_obj_t velo_in) {
    mp_int_t pitch = mp_obj_get_int(pitch_in);
    mp_int_t velo = mp_obj_get_int(velo_in);
    dexed_keydown(pitch, velo);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(keydown_obj, keydown);

static mp_obj_t millis(void) {
    return MP_OBJ_NEW_SMALL_INT(dexed_get_millis());
}
static MP_DEFINE_CONST_FUN_OBJ_0(millis_obj, millis);


static mp_obj_t set_gain(mp_obj_t gain_in) {
    mp_int_t gain = mp_obj_get_int(gain_in);
    dexed_set_gain(gain);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(set_gain_obj, set_gain);






static const mp_rom_map_elem_t mp_module_synth_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_synth) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&init_obj) },
    { MP_ROM_QSTR(MP_QSTR_destroy), MP_ROM_PTR(&destroy_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_samples16), MP_ROM_PTR(&get_samples16_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_samples32), MP_ROM_PTR(&get_samples32_obj) },
    { MP_ROM_QSTR(MP_QSTR_keyup), MP_ROM_PTR(&keyup_obj) },
    { MP_ROM_QSTR(MP_QSTR_keydown), MP_ROM_PTR(&keydown_obj) },
    { MP_ROM_QSTR(MP_QSTR_millis), MP_ROM_PTR(&millis_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_gain), MP_ROM_PTR(&set_gain_obj) },


};

static MP_DEFINE_CONST_DICT(mp_module_synth_globals, mp_module_synth_globals_table);

const mp_obj_module_t mp_module_synth = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_synth_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_synth, mp_module_synth);
