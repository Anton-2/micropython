// Include the header file to get access to the MicroPython API
#include "py/dynruntime.h"
#include "wave_mod.h"


#if !defined(__linux__)
void *memcpy(void *dst, const void *src, size_t n) {
    return mp_fun_table.memmove_(dst, src, n);
}
void *memset(void *s, int c, size_t n) {
    return mp_fun_table.memset_(s, c, n);
}
#endif

// This is type(Voice)
mp_obj_full_type_t mp_type_voice;

// Locals dict for the Wave type.
mp_map_elem_t voice_locals_dict_table[2];

static MP_DEFINE_CONST_DICT(voice_locals_dict, voice_locals_dict_table);

// Essentially Voice.__new__ (but also kind of __init__).
static mp_obj_t voice_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args_in) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    mp_obj_voice_t *self = mp_obj_malloc(mp_obj_voice_t, type);

    return MP_OBJ_FROM_PTR(self);
}

// Implements Voice.init(sample, pitch)
static mp_obj_t voice_init(mp_obj_t self_in, mp_obj_t sample_in, mp_obj_t pitch_in) {
    mp_obj_voice_t *self = MP_OBJ_TO_PTR(self_in);
    Sample_t * sample = (Sample_t *) mp_obj_get_int(sample_in);
    UQ17_15t pitch = mp_obj_get_int(pitch_in);
    _voice_init(&self->voice, sample, pitch);
    return mp_obj_new_int((uint32_t) &self->voice);
}
static MP_DEFINE_CONST_FUN_OBJ_3(voice_init_obj, voice_init);



// Implements Voice.fill(const Q16_16t * amp, Q16_16t * dest)
static mp_obj_t voice_fill(mp_obj_t self_in, mp_obj_t amp_in, mp_obj_t dest_in) {
    mp_obj_voice_t *self = MP_OBJ_TO_PTR(self_in);

    mp_buffer_info_t amp_bufinfo;
    mp_get_buffer_raise(amp_in, &amp_bufinfo, MP_BUFFER_READ);

    mp_buffer_info_t dest_bufinfo;
    mp_get_buffer_raise(dest_in, &dest_bufinfo, MP_BUFFER_RW);

    if (amp_bufinfo.len != (BLOC_SIZE*4)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid amp buffer size"));
    }

    if (dest_bufinfo.len != (BLOC_SIZE*4)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid dest buffer size"));
    }

    _Bool ret = _voice_fill_bloc(&self->voice, amp_bufinfo.buf, dest_bufinfo.buf);

    return mp_obj_new_bool(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_3(voice_fill_obj, voice_fill);


// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    // This must be first, it sets up the globals dict and other things
    MP_DYNRUNTIME_INIT_ENTRY

    // Initialise the Voice type.
    mp_type_voice.base.type = (void*)&mp_type_type;
    mp_type_voice.flags = MP_TYPE_FLAG_NONE;
    mp_type_voice.name = MP_QSTR_Voice;
    MP_OBJ_TYPE_SET_SLOT(&mp_type_voice, make_new, voice_make_new, 0);

    voice_locals_dict_table[0] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_init), MP_OBJ_FROM_PTR(&voice_init_obj) };
    voice_locals_dict_table[1] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_fill), MP_OBJ_FROM_PTR(&voice_fill_obj) };

    MP_OBJ_TYPE_SET_SLOT(&mp_type_voice, locals_dict, (void*)&voice_locals_dict, 1);

    // Make the Voice type available on the module.
    mp_store_global(MP_QSTR_Voice, MP_OBJ_FROM_PTR(&mp_type_voice));

    // This must be last, it restores the globals dict
    MP_DYNRUNTIME_INIT_EXIT
}
