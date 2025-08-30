// Include the header file to get access to the MicroPython API
#include "py/dynruntime.h"
#include <stdint.h>

#if !defined(__linux__)
void *memcpy(void *dst, const void *src, size_t n) {
    return mp_fun_table.memmove_(dst, src, n);
}
void *memset(void *s, int c, size_t n) {
    return mp_fun_table.memset_(s, c, n);
}
#endif
/*

FNT3
[nb_blocs][nb_glyphs]
[asc][dsc]

[cp_start][cp_end]
...
[cp_start][cp_end]

[bmap of7]
...
[bmap of7]

[left] [top] [width] [height]
[adv_x] [adv_y]
...
[left] [top] [width] [height]
[adv_x] [adv_y]

[bmap]
...
[bmap]


 */



typedef struct {
    int8_t top;
    uint8_t height;
    int8_t left;
    uint8_t width;
    uint8_t adv_x;
    uint8_t adv_y;
} glyph_t;

typedef struct {
    uint16_t cp_min;
    uint16_t cp_max;
} bloc_t;

typedef struct {
    uint32_t magic;
    uint16_t nb_blocs;
    uint16_t nb_glyphs;
    int16_t asc;
    int16_t dsc;
} font_t;


// -- Text type
//
//

// This is type(Text)
mp_obj_full_type_t mp_type_text;

// This is the internal state of a Text instance.
typedef struct {
    mp_obj_base_t base;

    // start of font
    font_t * font;

    // buffer
    uint16_t * buf;
    size_t buf_length;

    // set in layout
    uint16_t * indexes;
    uint16_t nb_indexes;            // nb glyphs to render

    uint16_t width;                 // total render width in pixels

    uint8_t ov_left;                // offset from first pixel to visual start (positive)
    uint8_t ov_right;               // offset from last pixel to visual end (positive)

    int8_t asc;                     // nb pixels above baseline in this text (may be negative: _)
    int8_t dsc;                     // nb pixels bellow baseline in this text (may be nagative: ')

} mp_obj_text_t;

static void setup_buffer(mp_obj_text_t *self, void* ptr, size_t size) {
    // put buffer after
    self->buf = ptr;
    self->buf_length = size;
}
static void set_buffer(mp_obj_text_t *self, mp_obj_t buffer_in) {
    mp_buffer_info_t buffer;
    if (!mp_get_buffer(buffer_in, &buffer, MP_BUFFER_RW)) {
        mp_raise_TypeError("buffer expected");
    }

    setup_buffer(self, buffer.buf, buffer.len);
}

static inline uint16_t alphaBlendRGB565( uint32_t fg_prep, uint32_t bg, uint8_t alpha ){
    alpha = ( alpha + 4 ) >> 3;
    bg = (bg | (bg << 16)) & 0b00000111111000001111100000011111;
    uint32_t result = ((((fg_prep - bg) * alpha) >> 5) + bg) & 0b00000111111000001111100000011111;
    return (uint16_t)((result >> 16) | result);
}

static inline uint32_t * get_bmaps_ptr(font_t * font) {
    void * base = font;
    return (uint32_t *) (base + sizeof(font_t) + sizeof(bloc_t) * font->nb_blocs);
}

static inline glyph_t * get_glyph_ptr(font_t * font) {
    return (glyph_t *) (get_bmaps_ptr(font) + font->nb_glyphs);
}

static void render_scanline(mp_obj_text_t *self, int y, uint16_t*src_ptr, uint16_t*dest_ptr, uint32_t fg_prep, int left, int right) {

    int width = self->width + left + right;
    // copy bg to buffer
    memcpy(dest_ptr, src_ptr, 2*width);
    int x = self->ov_left + left;

    glyph_t * glyphs = get_glyph_ptr(self->font);
    uint32_t * bmaps_offset = get_bmaps_ptr(self->font);

    for(int idx=0; idx < self->nb_indexes; idx++) {
        int glyph_index = self->indexes[idx];
        glyph_t glyph = glyphs[glyph_index];
        int gline = glyph.top + y;

        int x_start = x + glyph.left;

        if (x_start >= width) {
            // glyph is fully hidden in negative right pad, nothing to draw anymore
            break;
        }

        if (((x_start+glyph.width) >= 0) && (gline>=0) && (gline < glyph.height)) {

            int pix_offset = bmaps_offset[glyph_index] + glyph.width * gline;
            uint8_t * fg_base = ((uint8_t *) self->font) + pix_offset;

            int pix_idx = 0;
            if ((x_start + pix_idx) < 0) {
                pix_idx = x_start;
            }

            int end_idx = glyph.width;
            if (x_start + end_idx > width) {
                end_idx = width-x_start;
            }

            for(; pix_idx < end_idx; pix_idx++) {
                int x_pos = x_start + pix_idx;
                if (x_pos < 0) {
                    continue;
                }
                uint16_t bg_color = dest_ptr[x_pos];
                // dest_ptr[x_pos] = alphaBlendRGB565(fg_prep, bg_color, 0);
                dest_ptr[x_pos] = alphaBlendRGB565(fg_prep, bg_color, fg_base[pix_idx]);
            }
        }
        x += glyph.adv_x;
    }
}

static inline void _blit(uint16_t*buffer, size_t length, mp_obj_t blit_cb) {
    mp_obj_t args[2];
    args[0] = mp_obj_new_int((uint32_t) buffer);
    args[1] = mp_obj_new_int(length);
    mp_fun_table.call_function_n_kw(blit_cb, 2, args);
}

static void render(mp_obj_text_t *self, uint16_t*src_ptr, size_t src_stride, uint16_t fg_color, int nbl, mp_obj_t blit_cb, int pad_left, int pad_right, int pad_top, int pad_bottom) {
    uint32_t fg_prep = (fg_color | (fg_color << 16)) & 0b00000111111000001111100000011111;

    int line_in_buffer = 0;
    int width = self->width + pad_left + pad_right;
    uint16_t * dst_ptr = self->buf;

    int start_y = -self->asc;
    int end_y = self->dsc;


    for(int yline = start_y-pad_top; yline<(end_y+pad_bottom); yline++) {

        if ((yline>=start_y) && (yline<end_y)) {
            render_scanline(self, yline, src_ptr, dst_ptr, fg_prep, pad_left, pad_right);
        } else {
            memcpy(dst_ptr, src_ptr, width*2);
        }
        src_ptr += src_stride;
        line_in_buffer += 1;

        if (line_in_buffer == nbl) {
            _blit(self->buf, width * line_in_buffer, blit_cb);
            dst_ptr = self->buf;
            line_in_buffer = 0;
        } else {
            dst_ptr += width;
        }
    }

    if (line_in_buffer) {
        _blit(self->buf, width * line_in_buffer, blit_cb);
    }


}



// Essentially Text.__new__ (but also kind of __init__).
// Takes a single argument (not used)
static mp_obj_t text_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args_in) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);

    mp_obj_text_t *o = mp_obj_malloc(mp_obj_text_t, type);
    o->font = NULL;
    o->buf = NULL;
    o->buf_length = 0;

    o->indexes = NULL;
    o->nb_indexes = 0;

    if (n_args == 1) {
        set_buffer(o, args_in[0]);
    }

    return MP_OBJ_FROM_PTR(o);
}

// Locals dict for the Text type.
mp_map_elem_t text_locals_dict_table[4];
static MP_DEFINE_CONST_DICT(text_locals_dict, text_locals_dict_table);

// Implements Text.set_font()
static mp_obj_t text_set_font(mp_obj_t self_in, mp_obj_t font_in) {
    mp_buffer_info_t buffer;
    if (!mp_get_buffer(font_in, &buffer, MP_BUFFER_READ)) {
        mp_raise_TypeError("font data with buffer protocol expected");
    }

    font_t * font = (font_t * ) buffer.buf;

    if ((buffer.len < sizeof(font_t)) || (font->magic != 0x33544e46)) {
        mp_raise_TypeError("expected FNT3");
    }

    // TODO: check global length (get last bloc, last glyph, calc bitmap offset + glyph size)

    mp_obj_text_t *self = MP_OBJ_TO_PTR(self_in);

    self->font = font;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(text_set_font_obj, text_set_font);

static int cp_to_glyph(const uint16_t cp, const bloc_t* blocs, const size_t nb_blocs) {
    int glyph_offset = 0;
    for (size_t i = 0; i < nb_blocs; i++) {
        int dt = cp - blocs[i].cp_min;
        if (dt < 0) {
            return -1;
        }
        if (cp <= blocs[i].cp_max) {
            return glyph_offset + dt;
        }
        glyph_offset += (blocs[i].cp_max - blocs[i].cp_min) + 1;
    }
    return -1;
}




// Implements Text.layout()
static mp_obj_t text_layout(mp_obj_t self_in, mp_obj_t codepoint_in, mp_obj_t length_in) {
    mp_obj_text_t *self = MP_OBJ_TO_PTR(self_in);

    if (! self->font) {
        mp_raise_ValueError("font not set");
    }

    size_t length = mp_obj_get_int(length_in);

    void * base = self->font;
    bloc_t * blocs = (bloc_t *) (base + sizeof(font_t));
    size_t nb_blocs = self->font->nb_blocs;
    glyph_t * glyphs = get_glyph_ptr(self->font);

    mp_buffer_info_t buffer;
    if (!mp_get_buffer(codepoint_in, &buffer, MP_BUFFER_RW)) {
        mp_raise_TypeError("buffer expected");
    }

    if (length*sizeof(uint16_t) > buffer.len) {
        mp_raise_TypeError("input too small");
    }

    uint16_t * codepoints = buffer.buf;

    int width = 0;
    uint ov_left = 0;
    uint ov_right = 0;
    int asc = 0;
    int dsc = 0;

    int last_idx = length-1;
    int dest_idx = 0;

    if (length == 0) {
        self->indexes = NULL;
        self->nb_indexes = 0;

        self->asc = 0;
        self->dsc = 0;

        self->width = 0;
        self->ov_left = 0;
        self->ov_right = 0;

        return mp_obj_new_int(0);
    }

    for (size_t i = 0; i <= last_idx; i++) {

        int glyph_idx = cp_to_glyph(codepoints[i], blocs, nb_blocs);

        if (glyph_idx < 0) {
            continue;
        }

        codepoints[dest_idx++] = glyph_idx;

        glyph_t glyph = glyphs[glyph_idx];

        int glyph_asc = glyph.top;
        if ((i==0)||(glyph_asc > asc)) {
            asc = glyph_asc;
        }

        int glyph_dsc = glyph.height - glyph.top;
        if ((i==0)||(glyph_dsc > dsc)) {
            dsc = glyph_dsc;
        }


        if (i==0 && glyph.left < 0) {
            ov_left = -glyph.left;
        }

        if (i == last_idx) {
            int right_overflow = (glyph.left + glyph.width) - glyph.adv_x;
            if (right_overflow > 0) {
                ov_right = right_overflow;
            }
        }

        width += glyph.adv_x;
    }

    // rgb565 -> 2 bytes per pixel
    int line_size = 2*width;
    if (! self->buf) {
        setup_buffer(self, m_new(uint8_t, line_size), line_size);
    } else {
        if (self->buf_length < line_size) {
            mp_raise_ValueError("buffer too small");
        }
    }

    self->indexes = buffer.buf;
    self->nb_indexes = dest_idx;

    self->asc = asc;
    self->dsc = dsc;

    self->width = width;
    self->ov_left = ov_left;
    self->ov_right = ov_right;

    return mp_obj_new_int(dest_idx);

}
static MP_DEFINE_CONST_FUN_OBJ_3(text_layout_obj, text_layout);


// Implements Text.set_buffer()
static mp_obj_t text_set_buffer(mp_obj_t self_in, mp_obj_t buffer_in) {
    mp_obj_text_t *self = MP_OBJ_TO_PTR(self_in);
    set_buffer(self, buffer_in);
    return mp_const_none;

}
static MP_DEFINE_CONST_FUN_OBJ_2(text_set_buffer_obj, text_set_buffer);



// TODO : we are here
// Implements Text.render(src_addr, src_stride, fg_color)

static mp_obj_t text_render(size_t n_args, const mp_obj_t *args) {
    mp_obj_text_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t * src_addr = (uint16_t *) mp_obj_get_int(args[1]);
    size_t src_stride = mp_obj_get_int(args[2]);
    uint16_t fg_color = mp_obj_get_int(args[3]);

    int pad_left = mp_obj_get_int(args[5]);
    int pad_right = mp_obj_get_int(args[6]);

    int pad_top = mp_obj_get_int(args[7]);
    int pad_bottom = mp_obj_get_int(args[8]);


    int width = self->width + pad_left + pad_right;

    int nbl = self->buf_length / (width*2);

    render(self, src_addr, src_stride, fg_color, nbl, args[4], pad_left, pad_right, pad_top, pad_bottom);
    return mp_const_none;

}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(text_render_obj, 9, 9, text_render);

// Implements Text attributes read
static void ensure_params(mp_obj_text_t *self) {
    if (! self->indexes) {
        mp_raise_ValueError("no layout set");
    }
}

static void propertyclass_attr(mp_obj_t self_in, qstr attribute, mp_obj_t *destination) {
    if (destination[0] != MP_OBJ_NULL) {
        // not load attribute
        return;
    }

    mp_obj_text_t *self = MP_OBJ_TO_PTR(self_in);

    if(attribute == MP_QSTR_max_asc) {
        destination[0] = mp_obj_new_int((uintptr_t) self->font->asc);
    } else if(attribute == MP_QSTR_max_dsc) {
        destination[0] = mp_obj_new_int((uintptr_t) self->font->dsc);
    } else if(attribute == MP_QSTR_length) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->buf_length);
    } else if(attribute == MP_QSTR_width) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->width);
    } else if(attribute == MP_QSTR_ov_left) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->ov_left);
    } else if(attribute == MP_QSTR_ov_right) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->ov_right);
    } else if(attribute == MP_QSTR_asc) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->asc);
    } else if(attribute == MP_QSTR_dsc) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->dsc);
    } else if(attribute == MP_QSTR_height) {
        ensure_params(self);
        destination[0] = mp_obj_new_int(self->asc + self->dsc);
    } else {
        // Need to forward to locals dict.
        destination[1] = MP_OBJ_SENTINEL;
    }
}

// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    // This must be first, it sets up the globals dict and other things
    MP_DYNRUNTIME_INIT_ENTRY


    // Initialise the type.
    mp_type_text.base.type = (void*)&mp_type_type;
    mp_type_text.flags = MP_TYPE_FLAG_NONE;
    mp_type_text.name = MP_QSTR_Text;
    MP_OBJ_TYPE_SET_SLOT(&mp_type_text, make_new, text_make_new, 0);

    text_locals_dict_table[0] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_set_buffer), MP_OBJ_FROM_PTR(&text_set_buffer_obj) };
    text_locals_dict_table[1] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_set_font), MP_OBJ_FROM_PTR(&text_set_font_obj) };
    text_locals_dict_table[2] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_layout), MP_OBJ_FROM_PTR(&text_layout_obj) };
    text_locals_dict_table[3] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_render), MP_OBJ_FROM_PTR(&text_render_obj) };


    MP_OBJ_TYPE_SET_SLOT(&mp_type_text, locals_dict, (void*)&text_locals_dict, 1);

    MP_OBJ_TYPE_SET_SLOT(&mp_type_text, attr, propertyclass_attr, 2);

    // Make the Factorial type available on the module.
    mp_store_global(MP_QSTR_Text, MP_OBJ_FROM_PTR(&mp_type_text));


    // This must be last, it restores the globals dict
    MP_DYNRUNTIME_INIT_EXIT
}
