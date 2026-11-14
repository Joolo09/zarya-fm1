/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA core — internal definitions shared by all core modules. */
#ifndef ZR_DSP_H
#define ZR_DSP_H
#include "zr.h"

#if defined(ZR_TARGET_FM1) && !defined(ZR_AUDIO_IN_FLASH)
/* Audio code and its tables run from internal SRAM, as the FM-1 SDK requires. */
#define ZR_AUDIO_CODE __attribute__((section(".volatile_ram_code"), noinline, used))
#define ZR_AUDIO_DATA __attribute__((section(".volatile_ram.fm1"), aligned(4)))
#else
#define ZR_AUDIO_CODE
#define ZR_AUDIO_DATA
#endif
#define ZR_INLINE static inline __attribute__((always_inline))

/* ---- sizes that cost RAM on the device (tunable at build time) ---- */
#ifndef ZR_DELAY_LEN
#define ZR_DELAY_LEN 16384 /* samples at 22.05 kHz: 743 ms, 32 KB */
#endif
#define ZR_CHUNK 16 /* control-rate sub-block */

/* ---- tables (zr_tables.c) ---- */
extern const int16_t zr_sin[4096];
extern const uint32_t zr_note_inc[128];
extern const uint32_t zr_frac[256];
extern const uint16_t zr_level[128];
extern const uint16_t zr_vol[128];
extern const uint32_t zr_env_rate[128];
extern const uint16_t zr_cutoff[128];
extern const uint32_t zr_lfo_rate[128];
extern const uint32_t zr_ratio[17];

/* ---- patch parameters (synth) ---- */
enum {
    ZP_ALGO, ZP_FEEDBACK, ZP_TRANSPOSE, ZP_GLIDE, ZP_MODE, ZP_BEND, ZP_SPREAD, ZP_LEVEL,
    ZP_FLT_TYPE, ZP_CUTOFF, ZP_RESO, ZP_FLT_ENV, ZP_FLT_KEY, ZP_FLT_VEL, ZP_FA, ZP_FD, ZP_FS, ZP_FR,
    ZP_LFO_RATE, ZP_LFO_SHAPE, ZP_LFO_DEST, ZP_LFO_DEPTH, ZP_LFO_FADE, ZP_LFO_SYNC,
    ZP_M1_DEST, ZP_M1_AMT, ZP_M2_DEST, ZP_M2_AMT, ZP_M3_DEST, ZP_M3_AMT, ZP_M4_DEST, ZP_M4_AMT,
    ZP_M1_VAL, ZP_M2_VAL, ZP_M3_VAL, ZP_M4_VAL,
    ZP_DLY_SEND, ZP_REV_SEND, ZP_PAN, ZP_RSV,
    ZP_OP_BASE /* = 40 */
};
enum { ZO_RATIO, ZO_FINE, ZO_DETUNE, ZO_LEVEL, ZO_WAVE, ZO_ATK, ZO_DEC, ZO_SUS, ZO_REL, ZO_VEL, ZO_KSC, ZO_COUNT };
#define ZP_OP(op, field) (ZP_OP_BASE + (op) * ZO_COUNT + (field))

enum { ZMODE_POLY, ZMODE_MONO, ZMODE_LEGATO };
enum { ZFLT_OFF, ZFLT_LP, ZFLT_BP, ZFLT_HP };
enum { ZLFO_TRI, ZLFO_SINE, ZLFO_SAW, ZLFO_SQUARE, ZLFO_SH };
enum { ZLD_OFF, ZLD_PITCH, ZLD_FILTER, ZLD_AMP, ZLD_FM, ZLD_PAN };
enum { ZW_SINE, ZW_TRI, ZW_SAW, ZW_SQUARE, ZW_HALF, ZW_DOUBLE, ZW_FOLD, ZW_NOISE };
/* macro destinations */
enum { ZMD_OFF, ZMD_CUTOFF, ZMD_RESO, ZMD_FM, ZMD_FEEDBACK, ZMD_ATTACK, ZMD_DECAY, ZMD_RELEASE,
       ZMD_LFO_RATE, ZMD_LFO_DEPTH, ZMD_DLY_SEND, ZMD_REV_SEND, ZMD_FLT_ENV, ZMD_PITCH, ZMD_COUNT };

/* ---- drum kit parameters: 8 drums x 4, then kit-wide ---- */
enum { ZD_TUNE, ZD_DECAY, ZD_TONE, ZD_LEVEL, ZD_COUNT };
enum { ZDR_KICK, ZDR_SNARE, ZDR_CHAT, ZDR_OHAT, ZDR_CLAP, ZDR_TOM, ZDR_RIM, ZDR_BELL };
#define ZK_DRUM(drum, field) ((drum) * ZD_COUNT + (field))
enum { ZK_LEVEL = 32, ZK_DLY_SEND, ZK_REV_SEND, ZK_PAN_WIDTH, ZK_COUNT };

/* ---- global / project parameters ---- */
enum {
    ZG_TEMPO, ZG_SWING, ZG_ROOT, ZG_SCALE, ZG_SCALE_LOCK, ZG_MIDI_OUT, ZG_KB_OCT, ZG_SCENE,
    ZG_TRACK_BASE, /* 4 tracks x ZT_COUNT */
    ZG_FX_BASE = ZG_TRACK_BASE + 4 * 8,
    ZG_DLY_TIME = ZG_FX_BASE, ZG_DLY_FB, ZG_DLY_TONE, ZG_DLY_LEVEL, ZG_REV_SIZE, ZG_REV_DAMP, ZG_REV_LEVEL,
    ZG_DRIVE, ZG_DJ_FILTER, ZG_PUMP, ZG_PUMP_REL, ZG_FX_RSV,
    ZG_GEN_BASE,
    ZG_GEN_SEED_L = ZG_GEN_BASE, ZG_GEN_SEED_H, ZG_GEN_DENSITY, ZG_GEN_COMPLEX, ZG_GEN_RANGE, ZG_GEN_LENGTH,
    ZG_GEN_VARIATION, ZG_GEN_STYLE,
    ZG_COUNT, /* 60 */
    ZR_GLOBALS = 64
};
enum { ZT_VOLUME, ZT_MUTE, ZT_ARP_ON, ZT_ARP_MODE, ZT_ARP_RATE, ZT_ARP_OCT, ZT_ARP_GATE, ZT_ARP_HOLD, ZT_COUNT };
#define ZG_TRACK(track, field) (ZG_TRACK_BASE + (track) * ZT_COUNT + (field))
enum { ZARP_UP, ZARP_DOWN, ZARP_UPDOWN, ZARP_RANDOM, ZARP_ORDER };
enum { ZGEN_MELODY, ZGEN_BASS, ZGEN_ARP, ZGEN_CHORDS };
enum { ZR_SCALE_COUNT = 12, ZR_CHORD_COUNT = 8 };

/* ---- parameter descriptors: the single source of truth for names/ranges ---- */
enum { ZF_INT, ZF_INT1, ZF_BIPOLAR, ZF_ENUM, ZF_ONOFF, ZF_TEMPO, ZF_SWING, ZF_NOTE, ZF_RATIO, ZF_PAN, ZF_SEMI,
       ZF_OCT, ZF_DETUNE };
typedef struct {
    const char *name;         /* <= 8 characters, upper case */
    uint8_t min, max, def, fmt;
    const char *const *names; /* ZF_ENUM */
} zr_pdesc;
const zr_pdesc *zr_patch_desc(unsigned id);  /* synth patch param, NULL if invalid */
const zr_pdesc *zr_kit_desc(unsigned id);    /* drum kit param */
const zr_pdesc *zr_global_desc(unsigned id);
/* Format a value as text (<= 9 chars + NUL). Returns out. */
char *zr_format(const zr_pdesc *d, unsigned value, char out[12]);
unsigned zr_clamp_param(const zr_pdesc *d, int value);
extern const char *const zr_note_names[12];
extern const char *const zr_scale_names[ZR_SCALE_COUNT];
extern const uint16_t zr_scale_mask[ZR_SCALE_COUNT];
extern const char *const zr_chord_names[ZR_CHORD_COUNT];
extern const char *const zr_drum_names[ZR_DRUMS];
extern const char *const zr_category_names[8];
extern const uint8_t zr_gate_eighths[16];

/* ---- data model ---- */
enum { ZR_PATCH_SYNTH, ZR_PATCH_KIT };
enum { ZCAT_BASS, ZCAT_LEAD, ZCAT_KEYS, ZCAT_PAD, ZCAT_PLUCK, ZCAT_BELL, ZCAT_FX, ZCAT_KIT };
typedef struct {
    uint8_t type, category, flags, rsv;
    char name[ZR_NAME_LEN];
    uint8_t p[ZR_PATCH_PARAMS];
} zr_patch; /* 100 bytes */

/* Step. Synth tracks: note 0 = empty, else MIDI note. Drum track: note = mask
 * of the 8 drums. gate: bits 0..3 length index (zr_gate_eighths), bits 4..5
 * ratchet-1. fx: bits 0..2 probability (0 = always, n = skip n/8 of the time),
 * bits 3..5 chord type. */
typedef struct { uint8_t note, vel, gate, fx; } zr_step;
typedef struct {
    zr_step steps[ZR_STEPS];
    uint8_t length, rsv[3];
} zr_pattern; /* 260 bytes */

typedef struct {
    char name[ZR_NAME_LEN];
    uint8_t g[ZR_GLOBALS];
    zr_patch patch[ZR_TRACKS];
    zr_pattern pat[ZR_SCENES][ZR_TRACKS];
} zr_project;

extern zr_project zr_proj;

/* ---- engine: control -> audio ---- */
enum { ZE_NOTE_ON, ZE_NOTE_OFF, ZE_ALL_OFF, ZE_PLAY, ZE_STOP, ZE_BEND, ZE_SUSTAIN, ZE_PANIC, ZE_CC };
enum { ZQ_CONTROL, ZQ_MIDI }; /* one producer context each */
/* Returns 0, or -1 when the queue is full (the event is dropped). */
int zr_post(unsigned queue, unsigned type, unsigned track, unsigned a, unsigned b);
void zr_engine_init(void);
/* Tell the audio side that patch parameters of a track changed. */
void zr_patch_touch(unsigned track);
/* Bulk edits of shared data: audio renders silence between begin and end. */
void zr_engine_hold(int on);

typedef struct {
    volatile uint8_t playing, recording, voices, scene, scene_next, load, clip, pump;
    volatile uint8_t step[ZR_TRACKS];     /* current step per track */
    volatile uint8_t peak[ZR_TRACKS];     /* 0..127 decaying */
    volatile uint8_t peak_master;
    volatile uint8_t track_voices[ZR_TRACKS];
    volatile uint32_t overloads, dropped, blocks;
} zr_status_t;
extern zr_status_t zr_status;

typedef struct {
    volatile uint8_t record;     /* live record armed */
    volatile uint8_t sel_track;  /* target of MIDI channel 16 and local keys */
    volatile uint8_t scene_req;  /* 0xff none */
    volatile uint8_t rec_cursor; /* step record cursor when stopped */
} zr_live_t;
extern zr_live_t zr_live;

/* MIDI out produced by the audio side, drained by control. 0 = empty. */
unsigned zr_midi_out_pop(uint8_t message[3]);
/* Program change received over MIDI for a track: program+1, or 0. Clears it. */
unsigned zr_take_program(unsigned track);

/* ---- synth kernels (zr_synth.c) ---- */
struct zr_tparams;
struct zr_voice;
void zr_synth_derive(unsigned track);
void zr_drums_derive(void);

/* ---- fx (zr_fx.c) ---- */
void zr_fx_init(void);
/* bus: mix[2*n] dry stereo, dly[n], rev[n] mono sends (Q15-ish int32), duck:
 * buffer that is ducked by the pump (already included in mix when NULL). */
void zr_fx_process(int32_t *mix, const int32_t *duck, const int32_t *dly, const int32_t *rev, unsigned n,
                   int32_t *out_q23);
void zr_fx_pump_trigger(void);
void zr_fx_set_tempo(uint32_t step_len_q8);

/* ---- sequencer helpers / generator (zr_seq.c) ---- */
uint32_t zr_rand(uint32_t *state);
unsigned zr_scale_snap(unsigned note, unsigned root, unsigned scale);
void zr_pattern_clear(zr_pattern *p, unsigned length);
typedef struct {
    uint16_t seed;
    uint8_t root, scale, density, complexity, range, length, variation, style, base_note;
} zr_gen_params;
void zr_gen_from_globals(zr_gen_params *gp, unsigned track);
void zr_generate(zr_pattern *out, const zr_gen_params *gp, int drums);

/* ---- presets & serialization (zr_presets.c) ---- */
unsigned zr_factory_count(int kits);
void zr_factory_patch(zr_patch *out, unsigned index, int kits);
void zr_patch_init(zr_patch *p, int kit);
void zr_project_init(zr_project *p);
void zr_project_demo(zr_project *p);
int zr_patch_valid(const zr_patch *p);
void zr_patch_sanitize(zr_patch *p);
void zr_pattern_sanitize(zr_pattern *p);
void zr_globals_sanitize(uint8_t *g);
uint16_t zr_crc16(const uint8_t *data, uint32_t bytes, uint16_t crc);
uint32_t zr_crc32(const uint8_t *data, uint32_t bytes, uint32_t crc);
/* Blobs: 8-byte header {'Z','R',kind,schema,len_lo,len_hi,crc_lo,crc_hi} + payload. */
enum { ZB_PATCH = 1, ZB_PATTERN, ZB_GLOBALS, ZB_PROJECT };
enum { ZR_BLOB_HEADER = 8 };
uint32_t zr_blob_pack(uint8_t *out, unsigned kind, const void *payload, uint32_t bytes);
/* Returns payload length, or <0: -1 short, -2 magic, -3 kind, -4 schema, -5 length, -6 crc. */
int zr_blob_check(const uint8_t *blob, uint32_t bytes, unsigned kind, uint32_t expect_len);

/* ---- storage (zr_store.c) ---- */
enum { ZS_ABSENT, ZS_UNFORMATTED, ZS_READY, ZS_ERROR };
enum { ZREC_SETTINGS, ZREC_PRESETS, ZREC_PROJECT /* + slot */ };
void zr_store_init(const zr_platform *platform);
unsigned zr_store_state(void);
int zr_store_format(void);                                     /* explicit user action only */
int zr_store_save(unsigned record, const void *data, uint32_t bytes);
int zr_store_load(unsigned record, void *data, uint32_t bytes); /* bytes read or <0 */
int zr_store_exists(unsigned record);
uint32_t zr_store_writes(void);
extern zr_patch zr_user_presets[ZR_USER_PRESETS];
int zr_user_presets_load(void);
int zr_user_presets_save(void);
int zr_project_save(unsigned slot);
int zr_project_load(unsigned slot);

/* ---- editor protocol (zr_sysex.c) ---- */
void zr_sysex_init(const zr_platform *platform);
void zr_sysex_poll(uint32_t now_ms);
void zr_sysex_notify_param(unsigned scope, unsigned track, unsigned id, unsigned value);
void zr_sysex_notify_patch(unsigned track);
unsigned zr_pack7(uint8_t *out, const uint8_t *in, unsigned bytes);
unsigned zr_unpack7(uint8_t *out, const uint8_t *in, unsigned bytes);

/* ---- graphics (zr_gfx.c) ---- */
#define ZR_RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))
void zr_gfx_begin(uint16_t *buf, unsigned y0, unsigned h);
void zr_fill(int x, int y, int w, int h, uint16_t c);
void zr_frame(int x, int y, int w, int h, uint16_t c);
void zr_line(int x0, int y0, int x1, int y1, uint16_t c);
void zr_arc(int cx, int cy, int r, int a0, int a1, int thick, uint16_t c); /* angles 0..4095 */
int zr_text(int x, int y, const char *s, unsigned scale, uint16_t c);      /* returns end x */
int zr_text_width(const char *s, unsigned scale);
void zr_text_center(int cx, int y, const char *s, unsigned scale, uint16_t c);
void zr_text_right(int xr, int y, const char *s, unsigned scale, uint16_t c);
char *zr_utoa(unsigned v, char *out);

/* ---- ui (zr_ui.c) ---- */
void zr_ui_init(const zr_platform *platform);
void zr_ui_control(const zr_controls *c, uint32_t now_ms);
void zr_ui_draw(uint16_t *buf, unsigned y0, unsigned h);
int zr_ui_dirty(void);
void zr_ui_touch(void);
void zr_ui_notice(const char *text);
/* Shared edit entry points (UI and editor protocol use the same paths). */
void zr_set_patch_param(unsigned track, unsigned id, int value, int notify);
void zr_set_global(unsigned id, int value, int notify);
void zr_load_patch(unsigned track, const zr_patch *p);
unsigned zr_ui_page(void);

/* ---- tiny libc replacements (zr_params.c) ---- */
void zr_zero(void *p, uint32_t bytes);
void zr_copy(void *d, const void *s, uint32_t bytes);
int zr_equal(const void *a, const void *b, uint32_t bytes);
unsigned zr_strlen(const char *s);
void zr_name_set(char name[ZR_NAME_LEN], const char *s);

#endif
