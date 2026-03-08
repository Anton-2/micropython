#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef uint16_t q16_t;

typedef enum { ADSR_SHAPE_LINEAR = 0, ADSR_SHAPE_EXP = 1 } adsr_shape_t;
typedef enum { ADSR_IDLE=0, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE } adsr_state_t;

/* ------------------------------------------------------------------ */
/* Tables de forme — 256 entrées Q16, [0 → 0xFFFF]                    */
/* ------------------------------------------------------------------ */
#define ADSR_TABLE_SIZE 256

/* Généré une fois au startup (ou en const Flash si LTO) */
static q16_t adsr_table_linear[ADSR_TABLE_SIZE];
static q16_t adsr_table_exp[ADSR_TABLE_SIZE];

static inline void adsr_tables_init(void)
{
    for (int i = 0; i < ADSR_TABLE_SIZE; i++) {
        /* Linéaire : y = i / 255 */
        adsr_table_linear[i] = (q16_t)((uint32_t)i * 0xFFFF / (ADSR_TABLE_SIZE - 1));

        /* Exponentielle : y = (e^(k*x) - 1) / (e^k - 1), k=4
         * Approximé en entier : quadratique x^2 suffit perceptuellement,
         * mais ici on peut faire mieux avec une vraie courbe précalculée.
         * x = i/255, y = x^3  (cubique → plus "naturel" que quadratique) */
        uint32_t x = (uint32_t)i * 0x100 / (ADSR_TABLE_SIZE - 1); /* Q8 */
        uint32_t x2 = x * x >> 8;   /* Q8 */
        uint32_t x3 = x2 * x >> 8;  /* Q8 */
        adsr_table_exp[i] = (q16_t)(x3 * 0xFFFF >> 8);
    }
}

/* ------------------------------------------------------------------ */
/* Lookup avec interpolation linéaire                                  */
/* t: phase [0..total-1], total: durée segment                         */
/* Retourne Q16 dans [0, 0xFFFF] représentant la progression [0→1]    */
/* ------------------------------------------------------------------ */
static inline q16_t adsr_table_lookup(uint32_t t, uint32_t total,
                                       const q16_t *table)
{
    if (total == 0) return 0xFFFF;

    /* index Q8 dans la table : [0 .. (TABLE_SIZE-1)] */
    uint32_t idx_q8 = (uint32_t)((uint64_t)t * (ADSR_TABLE_SIZE - 1) * 256 / total);
    uint32_t idx    = idx_q8 >> 8;
    uint32_t frac   = idx_q8 & 0xFF;  /* 0..255 */

    if (idx >= ADSR_TABLE_SIZE - 1) return table[ADSR_TABLE_SIZE - 1];

    /* Interpolation linéaire */
    uint32_t a = table[idx];
    uint32_t b = table[idx + 1];
    return (q16_t)(a + ((b - a) * frac >> 8));
}

/* ------------------------------------------------------------------ */
/* Application de la forme sur [start → end]                           */
/* forward=true : suit la table dans l'ordre (montée)                  */
/* forward=false: lit la table à l'envers (descente)                   */
/* ------------------------------------------------------------------ */
static inline q16_t adsr_shape(uint32_t t, uint32_t total,
                                q16_t start, q16_t end,
                                adsr_shape_t shape)
{
    const q16_t *table = (shape == ADSR_SHAPE_EXP) ? adsr_table_exp
                                                    : adsr_table_linear;
    /* Progression normalisée [0→1] selon direction */
    q16_t norm;
    if (end >= start) {
        norm = adsr_table_lookup(t, total, table);          /* 0→1 */
    } else {
        norm = adsr_table_lookup(total - 1 - t, total, table); /* 1→0 */
    }

    /* Scale vers [start, end] */
    uint32_t range;
    uint32_t val;
    if (end >= start) {
        range = end - start;
        val   = start + ((uint64_t)range * norm >> 16);
    } else {
        range = start - end;
        val   = end   + ((uint64_t)range * norm >> 16);
    }
    if (val > 0xFFFF) val = 0xFFFF;
    return (q16_t)val;
}

/* ------------------------------------------------------------------ */
/* Structs et contrôle — identiques à la version précédente            */
/* ------------------------------------------------------------------ */
typedef struct adsr_s {
    uint32_t     attack_samples;
    adsr_shape_t attack_shape;
    uint32_t     decay_samples;
    adsr_shape_t decay_shape;
    q16_t        sustain_level;
    uint32_t     release_samples;
    adsr_shape_t release_shape;

    adsr_state_t state;
    uint32_t     phase;
    q16_t        release_start;
    q16_t        last_amp;       // last output of adsr_process_block (for note_off)
} adsr_t;

static inline void adsr_init(adsr_t *e)  { memset(e, 0, sizeof(*e)); }

static inline void adsr_note_on(adsr_t *e) {
    e->state = ADSR_ATTACK;
    e->phase = 0;
}

static inline void adsr_note_off(adsr_t *e) {
    e->release_start = e->last_amp;
    e->state         = ADSR_RELEASE;
    e->phase         = 0;
}

static inline bool adsr_done(adsr_t *e) {
    return (e->state == ADSR_IDLE);
}

static inline q16_t adsr_process_block(adsr_t *e, q16_t *buf, uint32_t n)
{
    q16_t amp = 0;
    for (uint32_t i = 0; i < n; i++) {
        switch (e->state) {
        case ADSR_IDLE:
            amp = 0;
            break;
        case ADSR_ATTACK:
            amp = adsr_shape(e->phase, e->attack_samples,
                             0, 0xFFFF, e->attack_shape);
            if (++e->phase >= e->attack_samples) { e->phase=0; e->state=ADSR_DECAY; }
            break;
        case ADSR_DECAY:
            amp = adsr_shape(e->phase, e->decay_samples,
                             0xFFFF, e->sustain_level, e->decay_shape);
            if (++e->phase >= e->decay_samples)  { e->phase=0; e->state=ADSR_SUSTAIN; }
            break;
        case ADSR_SUSTAIN:
            amp = e->sustain_level;
            break;
        case ADSR_RELEASE:
            amp = adsr_shape(e->phase, e->release_samples,
                             e->release_start, 0, e->release_shape);
            if (++e->phase >= e->release_samples){ e->phase=0; e->state=ADSR_IDLE; }
            break;
        }
        buf[i] = amp;
    }
    e->last_amp = amp;
    return amp;
}
