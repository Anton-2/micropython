
#include "py/obj.h"
#include "py/mphal.h"
#include "py/runtime.h"

#include <stdio.h>
#include "dexed_wrapper.h"

static mp_obj_t init(void) {
    dexed_init(4, 24000);
    printf("Init !\r\n");
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(init_obj, init);

static mp_obj_t destroy(void) {
    dexed_destroy();
    printf("Done !\r\n");
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(destroy_obj, destroy);


static const mp_rom_map_elem_t mp_module_synth_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_synth) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&init_obj) },
    { MP_ROM_QSTR(MP_QSTR_destroy), MP_ROM_PTR(&destroy_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_synth_globals, mp_module_synth_globals_table);

const mp_obj_module_t mp_module_synth = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_synth_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_synth, mp_module_synth);
