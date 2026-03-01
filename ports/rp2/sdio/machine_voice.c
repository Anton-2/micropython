// MicroPython wrapper for SDIO voice manager
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "sdio_voice.h"

// Singleton voice manager
static sdio_voice_manager_t g_voice_manager = {0};
static bool g_manager_initialized = false;

// init(num_voices, chunk_size)
static mp_obj_t voice_init(mp_obj_t num_voices_in, mp_obj_t chunk_size_in) {
    mp_int_t num_voices = mp_obj_get_int(num_voices_in);
    mp_int_t chunk_size = mp_obj_get_int(chunk_size_in);

    if (num_voices <= 0 || num_voices > 256) {
        mp_raise_ValueError(MP_ERROR_TEXT("num_voices must be 1-256"));
    }

    if (chunk_size <= 0 || chunk_size > 65536) {
        mp_raise_ValueError(MP_ERROR_TEXT("chunk_size must be 1-65536"));
    }

    // Free existing manager if already initialized
    if (g_manager_initialized) {
        sdio_voice_manager_free(&g_voice_manager);
        g_manager_initialized = false;
    }

    sdio_status_t status = sdio_voice_manager_init(&g_voice_manager, num_voices, chunk_size);
    if (status != SDIO_OK) {
        mp_raise_OSError(MP_ENOMEM);
    }

    g_manager_initialized = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(voice_init_obj, voice_init);

// free()
static mp_obj_t voice_free(void) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    sdio_voice_manager_free(&g_voice_manager);
    g_manager_initialized = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(voice_free_obj, voice_free);

// start(voice_index, sd_sector, size_bytes, omega_inc, start_offset, loop_start_sample=0, loop_end_sample=0)
static mp_obj_t voice_start(size_t n_args, const mp_obj_t *args) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    mp_int_t voice_index = mp_obj_get_int(args[0]);
    mp_int_t sd_sector = mp_obj_get_int(args[1]);
    mp_int_t size_bytes = mp_obj_get_int(args[2]);
    omega_t omega_inc = (omega_t)(mp_obj_get_float(args[3]) * ONE_OMEGA);
    mp_uint_t start_offset = mp_obj_get_int(args[4]);
    uint32_t loop_start_sample = (n_args > 5) ? (uint32_t)mp_obj_get_int(args[5]) : 0;
    uint32_t loop_end_sample   = (n_args > 6) ? (uint32_t)mp_obj_get_int(args[6]) : 0;

    if (voice_index < 0 || voice_index >= (mp_int_t)g_voice_manager.num_voices) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid voice_index"));
    }

    if (size_bytes <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size_bytes must be > 0"));
    }

    sdio_status_t status = sdio_voice_start(&g_voice_manager, voice_index, sd_sector, size_bytes, omega_inc, start_offset, loop_start_sample, loop_end_sample);
    if (status != SDIO_OK) {
        mp_raise_OSError(MP_EIO);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(voice_start_obj, 5, 7, voice_start);

// update()
static mp_obj_t voice_update(void) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    sdio_status_t status = sdio_voice_manager_update(&g_voice_manager);

    // Return status as integer (SDIO_OK=0, SDIO_BUSY=1, errors > 1)
    return MP_OBJ_NEW_SMALL_INT(status);
}
static MP_DEFINE_CONST_FUN_OBJ_0(voice_update_obj, voice_update);

// available(voice_index) -> bytes available
static mp_obj_t voice_available(mp_obj_t voice_index_in) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    mp_int_t voice_index = mp_obj_get_int(voice_index_in);

    if (voice_index < 0 || voice_index >= (mp_int_t)g_voice_manager.num_voices) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid voice_index"));
    }

    sdio_voice_t *voice = sdio_voice_manager_get(&g_voice_manager, voice_index);
    if (voice == NULL) {
        mp_raise_OSError(MP_EIO);
    }

    uint32_t available = sdio_voice_available(voice);
    return MP_OBJ_NEW_SMALL_INT(available);
}
static MP_DEFINE_CONST_FUN_OBJ_1(voice_available_obj, voice_available);

// consume(voice_index, bytes_read)
static mp_obj_t voice_consume(mp_obj_t voice_index_in, mp_obj_t bytes_read_in) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    mp_int_t voice_index = mp_obj_get_int(voice_index_in);
    mp_int_t bytes_read = mp_obj_get_int(bytes_read_in);

    if (voice_index < 0 || voice_index >= (mp_int_t)g_voice_manager.num_voices) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid voice_index"));
    }

    if (bytes_read < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bytes_read must be >= 0"));
    }

    sdio_voice_t *voice = sdio_voice_manager_get(&g_voice_manager, voice_index);
    if (voice == NULL) {
        mp_raise_OSError(MP_EIO);
    }

    sdio_voice_consume(voice, bytes_read);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(voice_consume_obj, voice_consume);

// diag(voice_index, label=None)
static mp_obj_t voice_diag(size_t n_args, const mp_obj_t *args) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    mp_int_t voice_index = mp_obj_get_int(args[0]);

    if (voice_index < 0 || voice_index >= (mp_int_t)g_voice_manager.num_voices) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid voice_index"));
    }

    const char *label = NULL;
    if (n_args > 1 && args[1] != mp_const_none) {
        label = mp_obj_str_get_str(args[1]);
    }

    sdio_voice_t *voice = sdio_voice_manager_get(&g_voice_manager, voice_index);
    if (voice == NULL) {
        mp_raise_OSError(MP_EIO);
    }

    sdio_voice_print_diag(voice, label);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(voice_diag_obj, 1, 2, voice_diag);

// manager_diag(label=None)
static mp_obj_t voice_manager_diag(size_t n_args, const mp_obj_t *args) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    const char *label = NULL;
    if (n_args > 0 && args[0] != mp_const_none) {
        label = mp_obj_str_get_str(args[0]);
    }

    sdio_voice_manager_print_diag(&g_voice_manager, label);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(voice_manager_diag_obj, 0, 1, voice_manager_diag);

// fill_amp(amplitude)
static mp_obj_t voice_fill_amp(mp_obj_t amplitude_in) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    // Convert float to Q1_14_t
    mp_float_t amplitude_float = mp_obj_get_float(amplitude_in);

    // Clamp to valid range [-2.0, +1.99994]
    if (amplitude_float < -2.0f) {
        amplitude_float = -2.0f;
    }
    if (amplitude_float > 1.99994f) {
        amplitude_float = 1.99994f;
    }

    // Convert to Q1_14_t: multiply by 2^14
    Q1_14_t amplitude_q = (Q1_14_t)(amplitude_float * (1 << 14));

    sdio_voice_fill_amplitude(&g_voice_manager, amplitude_q);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(voice_fill_amp_obj, voice_fill_amp);

// fill_chunk()
static mp_obj_t voice_fill_chunk(void) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }
    uint32_t nb_active = sdio_voice_fill_chunk(&g_voice_manager);
    return MP_OBJ_NEW_SMALL_INT(nb_active);
}
static MP_DEFINE_CONST_FUN_OBJ_0(voice_fill_chunk_obj, voice_fill_chunk);

// get_buffer(voice_index) -> memoryview
// Returns a memoryview of the entire voice buffer (circular buffer)
static mp_obj_t voice_get_buffer(mp_obj_t voice_index_in) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    mp_int_t voice_index = mp_obj_get_int(voice_index_in);

    if (voice_index < 0 || voice_index >= (mp_int_t)g_voice_manager.num_voices) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid voice_index"));
    }

    sdio_voice_t *voice = sdio_voice_manager_get(&g_voice_manager, voice_index);
    if (voice == NULL) {
        mp_raise_OSError(MP_EIO);
    }

    // Return a memoryview pointing to the voice buffer
    // Buffer size is RING_SIZE * SDIO_BLOCK_SIZE
    return mp_obj_new_memoryview('B', RING_SIZE * SDIO_BLOCK_SIZE, voice->buffer);
}
static MP_DEFINE_CONST_FUN_OBJ_1(voice_get_buffer_obj, voice_get_buffer);

// get_accum() -> memoryview
// Returns a memoryview of the accumulator buffer (Q17_14 signed 32-bit)
static mp_obj_t voice_get_accum(void) {
    if (!g_manager_initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("manager not initialized"));
    }

    return mp_obj_new_memoryview('i', g_voice_manager.chunk_size, g_voice_manager.accumulator_buffer);
}
static MP_DEFINE_CONST_FUN_OBJ_0(voice_get_accum_obj, voice_get_accum);

// Module globals
static const mp_rom_map_elem_t voice_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_voice) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&voice_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_free), MP_ROM_PTR(&voice_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&voice_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&voice_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_available), MP_ROM_PTR(&voice_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_consume), MP_ROM_PTR(&voice_consume_obj) },
    { MP_ROM_QSTR(MP_QSTR_diag), MP_ROM_PTR(&voice_diag_obj) },
    { MP_ROM_QSTR(MP_QSTR_manager_diag), MP_ROM_PTR(&voice_manager_diag_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_buffer), MP_ROM_PTR(&voice_get_buffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_amp), MP_ROM_PTR(&voice_fill_amp_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_chunk), MP_ROM_PTR(&voice_fill_chunk_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_accum), MP_ROM_PTR(&voice_get_accum_obj) },

    // Status constants
    { MP_ROM_QSTR(MP_QSTR_SDIO_OK), MP_ROM_INT(SDIO_OK) },
    { MP_ROM_QSTR(MP_QSTR_SDIO_BUSY), MP_ROM_INT(SDIO_BUSY) },
};
static MP_DEFINE_CONST_DICT(voice_module_globals, voice_module_globals_table);

const mp_obj_module_t mp_module_voice = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&voice_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_voice, mp_module_voice);
