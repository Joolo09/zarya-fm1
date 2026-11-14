/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Parameter descriptors: names, ranges, defaults and display formats.
 * The web editor's schema.json is generated from these tables. */
#include "zr_dsp.h"

void zr_zero(void *p, uint32_t bytes) { uint8_t *d = (uint8_t *)p; while (bytes--) *d++ = 0; }
void zr_copy(void *d, const void *s, uint32_t bytes)
{
    uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s;
    while (bytes--) *a++ = *b++;
}
int zr_equal(const void *a, const void *b, uint32_t bytes)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    while (bytes--) if (*x++ != *y++) return 0;
    return 1;
}
unsigned zr_strlen(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
void zr_name_set(char name[ZR_NAME_LEN], const char *s)
{
    unsigned i = 0;
    for (; i < ZR_NAME_LEN && s[i]; ++i) name[i] = s[i];
    for (; i < ZR_NAME_LEN; ++i) name[i] = ' ';
}
char *zr_utoa(unsigned v, char *out)
{
    char tmp[11]; unsigned n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char *p = out;
    while (n) *p++ = tmp[--n];
    *p = 0;
    return out;
}

const char *const zr_note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char *const zr_scale_names[ZR_SCALE_COUNT] = {
    "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYD", "HARM MIN", "PENT MAJ", "PENT MIN",
    "BLUES", "WHOLE", "CHROMA"};
ZR_AUDIO_DATA const uint16_t zr_scale_mask[ZR_SCALE_COUNT] = {
    0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x9ad, 0x295, 0x4a9, 0x4e9, 0x555, 0xfff};
const char *const zr_chord_names[ZR_CHORD_COUNT] = {"NOTE", "OCT", "5TH", "MAJ", "MIN", "SUS4", "MAJ7", "MIN7"};
const char *const zr_drum_names[ZR_DRUMS] = {"KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "TOM", "RIM", "BELL"};
const char *const zr_category_names[8] = {"BASS", "LEAD", "KEYS", "PAD", "PLUCK", "BELL", "FX", "KIT"};
/* Gate length of a step in eighths of a step. */
ZR_AUDIO_DATA const uint8_t zr_gate_eighths[16] = {1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32, 48, 64, 96, 128};

static const char *const n_mode[] = {"POLY", "MONO", "LEGATO"};
static const char *const n_filter[] = {"OFF", "LOWPASS", "BANDPASS", "HIGHPASS"};
static const char *const n_lfo_shape[] = {"TRI", "SINE", "SAW", "SQUARE", "S+H"};
static const char *const n_lfo_dest[] = {"OFF", "PITCH", "FILTER", "AMP", "FM", "PAN"};
static const char *const n_wave[] = {"SINE", "TRI", "SAW", "SQUARE", "HALF", "DOUBLE", "FOLD", "NOISE"};
static const char *const n_macro[] = {"OFF", "CUTOFF", "RESO", "FM AMT", "FEEDBACK", "ATTACK", "DECAY", "RELEASE",
                                      "LFO RATE", "LFO DEPTH", "DLY SEND", "REV SEND", "FLT ENV", "PITCH"};
static const char *const n_arp_mode[] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "ORDER"};
static const char *const n_arp_rate[] = {"1/8", "1/16", "1/32", "1/8T", "1/16T"};
static const char *const n_dly_time[] = {"1/32", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4"};
static const char *const n_gen_len[] = {"16", "32", "48", "64"};
static const char *const n_gen_style[] = {"MELODY", "BASS", "ARP", "CHORDS"};

#define P(name, min, max, def, fmt) {name, min, max, def, fmt, 0}
#define E(name, names, def) {name, 0, (uint8_t)(sizeof(names) / sizeof(names[0]) - 1), def, ZF_ENUM, names}

static const zr_pdesc patch_desc[ZP_OP_BASE] = {
    P("ALGO", 0, 7, 0, ZF_INT1), P("FEEDBACK", 0, 127, 0, ZF_INT), P("TRANSP", 40, 88, 64, ZF_SEMI),
    P("GLIDE", 0, 127, 0, ZF_INT), E("MODE", n_mode, 0), P("BEND", 0, 12, 2, ZF_INT),
    P("SPREAD", 0, 127, 0, ZF_INT), P("LEVEL", 0, 127, 100, ZF_INT),
    E("FILTER", n_filter, 0), P("CUTOFF", 0, 127, 127, ZF_INT), P("RESO", 0, 127, 0, ZF_INT),
    P("FLT ENV", 1, 127, 64, ZF_BIPOLAR), P("KEYTRACK", 0, 127, 0, ZF_INT), P("FLT VEL", 0, 127, 0, ZF_INT),
    P("F.ATTACK", 0, 127, 0, ZF_INT), P("F.DECAY", 0, 127, 60, ZF_INT), P("F.SUST", 0, 127, 0, ZF_INT),
    P("F.REL", 0, 127, 60, ZF_INT),
    P("LFO RATE", 0, 127, 70, ZF_INT), E("LFO WAVE", n_lfo_shape, 0), E("LFO DEST", n_lfo_dest, 0),
    P("LFO DPTH", 0, 127, 0, ZF_INT), P("LFO FADE", 0, 127, 0, ZF_INT), P("LFO SYNC", 0, 1, 0, ZF_ONOFF),
    E("M1 DEST", n_macro, ZMD_FM), P("M1 AMT", 1, 127, 100, ZF_BIPOLAR),
    E("M2 DEST", n_macro, ZMD_CUTOFF), P("M2 AMT", 1, 127, 100, ZF_BIPOLAR),
    E("M3 DEST", n_macro, ZMD_RELEASE), P("M3 AMT", 1, 127, 100, ZF_BIPOLAR),
    E("M4 DEST", n_macro, ZMD_REV_SEND), P("M4 AMT", 1, 127, 100, ZF_BIPOLAR),
    P("MACRO 1", 0, 127, 0, ZF_INT), P("MACRO 2", 0, 127, 0, ZF_INT), P("MACRO 3", 0, 127, 0, ZF_INT),
    P("MACRO 4", 0, 127, 0, ZF_INT),
    P("DLY SEND", 0, 127, 0, ZF_INT), P("REV SEND", 0, 127, 0, ZF_INT), P("PAN", 1, 127, 64, ZF_PAN),
    P("-", 0, 0, 0, ZF_INT),
};
static const zr_pdesc op_desc[ZO_COUNT] = {
    P("RATIO", 0, 16, 1, ZF_RATIO), P("FINE", 0, 127, 0, ZF_INT), P("DETUNE", 0, 14, 7, ZF_DETUNE),
    P("LEVEL", 0, 127, 0, ZF_INT), E("WAVE", n_wave, 0), P("ATTACK", 0, 127, 0, ZF_INT),
    P("DECAY", 0, 127, 70, ZF_INT), P("SUSTAIN", 0, 127, 127, ZF_INT), P("RELEASE", 0, 127, 40, ZF_INT),
    P("VEL SENS", 0, 127, 64, ZF_INT), P("KEY SCL", 0, 127, 0, ZF_INT),
};
static const zr_pdesc drum_desc[ZD_COUNT] = {
    P("TUNE", 0, 127, 64, ZF_INT), P("DECAY", 0, 127, 64, ZF_INT), P("TONE", 0, 127, 64, ZF_INT),
    P("LEVEL", 0, 127, 100, ZF_INT),
};
static const zr_pdesc kit_desc[ZK_COUNT - ZK_LEVEL] = {
    P("KIT LVL", 0, 127, 110, ZF_INT), P("DLY SEND", 0, 127, 0, ZF_INT), P("REV SEND", 0, 127, 20, ZF_INT),
    P("WIDTH", 0, 127, 80, ZF_INT),
};
static const zr_pdesc global_desc[ZG_TRACK_BASE] = {
    P("TEMPO", 0, 200, 80, ZF_TEMPO), P("SWING", 0, 25, 0, ZF_SWING), P("KEY", 0, 11, 0, ZF_NOTE),
    E("SCALE", zr_scale_names, 1), P("SCL LOCK", 0, 1, 0, ZF_ONOFF), P("MIDI OUT", 0, 1, 1, ZF_ONOFF),
    P("OCTAVE", 0, 6, 3, ZF_OCT), P("SCENE", 0, 7, 0, ZF_INT1),
};
static const zr_pdesc track_desc[ZT_COUNT] = {
    P("VOLUME", 0, 127, 100, ZF_INT), P("MUTE", 0, 1, 0, ZF_ONOFF), P("ARP", 0, 1, 0, ZF_ONOFF),
    E("ARP MODE", n_arp_mode, 0), E("ARP RATE", n_arp_rate, 1), P("ARP OCT", 0, 2, 0, ZF_INT1),
    P("ARP GATE", 1, 127, 64, ZF_INT), P("ARP HOLD", 0, 1, 0, ZF_ONOFF),
};
static const zr_pdesc fx_desc[ZG_COUNT - ZG_FX_BASE] = {
    E("DLY TIME", n_dly_time, 6), P("DLY FDBK", 0, 120, 60, ZF_INT), P("DLY TONE", 0, 127, 80, ZF_INT),
    P("DLY LVL", 0, 127, 100, ZF_INT), P("REV SIZE", 0, 127, 80, ZF_INT), P("REV DAMP", 0, 127, 60, ZF_INT),
    P("REV LVL", 0, 127, 100, ZF_INT), P("DRIVE", 0, 127, 0, ZF_INT), P("DJ FILT", 1, 127, 64, ZF_BIPOLAR),
    P("PUMP", 0, 127, 0, ZF_INT), P("PUMP REL", 0, 127, 60, ZF_INT), P("-", 0, 0, 0, ZF_INT),
    P("SEED LO", 0, 127, 42, ZF_INT), P("SEED HI", 0, 127, 0, ZF_INT), P("DENSITY", 0, 127, 80, ZF_INT),
    P("COMPLEX", 0, 127, 50, ZF_INT), P("RANGE", 0, 2, 1, ZF_INT1), E("LENGTH", n_gen_len, 0),
    P("VARIATN", 0, 127, 40, ZF_INT), E("STYLE", n_gen_style, 0),
};

const zr_pdesc *zr_patch_desc(unsigned id)
{
    if (id < ZP_OP_BASE) return &patch_desc[id];
    if (id < ZR_PATCH_PARAMS) return &op_desc[(id - ZP_OP_BASE) % ZO_COUNT];
    return 0;
}
const zr_pdesc *zr_kit_desc(unsigned id)
{
    if (id < ZK_LEVEL) return &drum_desc[id % ZD_COUNT];
    if (id < ZK_COUNT) return &kit_desc[id - ZK_LEVEL];
    return 0;
}
const zr_pdesc *zr_global_desc(unsigned id)
{
    if (id < ZG_TRACK_BASE) return &global_desc[id];
    if (id < ZG_FX_BASE) return &track_desc[(id - ZG_TRACK_BASE) % ZT_COUNT];
    if (id < ZG_COUNT) return &fx_desc[id - ZG_FX_BASE];
    return 0;
}
unsigned zr_clamp_param(const zr_pdesc *d, int value)
{
    if (value < (int)d->min) return d->min;
    if (value > (int)d->max) return d->max;
    return (unsigned)value;
}

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_signed(char *p, int v)
{
    if (v > 0) *p++ = '+';
    else if (v < 0) { *p++ = '-'; v = -v; }
    zr_utoa((unsigned)v, p);
    return p + zr_strlen(p);
}
char *zr_format(const zr_pdesc *d, unsigned value, char out[12])
{
    char *p = out;
    *p = 0;
    switch (d->fmt) {
    case ZF_INT1: zr_utoa(value + 1, p); break;
    case ZF_BIPOLAR: put_signed(p, (int)value - 64); break;
    case ZF_ENUM: put(p, value <= d->max && d->names ? d->names[value] : "?"); break;
    case ZF_ONOFF: put(p, value ? "ON" : "OFF"); break;
    case ZF_TEMPO: zr_utoa(value + 40, p); break;
    case ZF_SWING: p = zr_utoa(value + 50, p) + zr_strlen(p); put(p, "%"); break;
    case ZF_NOTE: put(p, zr_note_names[value % 12]); break;
    case ZF_RATIO: put(p, value ? "" : "0.5"); if (value) zr_utoa(value, p); break;
    case ZF_PAN:
        if (value == 64) put(p, "C");
        else { *p++ = value < 64 ? 'L' : 'R'; zr_utoa(value < 64 ? 64 - value : value - 64, p); }
        break;
    case ZF_SEMI: put_signed(p, (int)value - 64); break;
    case ZF_OCT: put_signed(p, (int)value - 3); break;
    case ZF_DETUNE: put_signed(p, (int)value - 7); break;
    default: zr_utoa(value, p); break;
    }
    return out;
}
