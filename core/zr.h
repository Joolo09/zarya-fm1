/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA — original firmware core for the M-VAVE FM-1.
 * Public interface of the portable core. The same code is compiled for the
 * FM-1 (device/), for host tests (host/) and to WebAssembly (wasm/).
 * Integer-only, freestanding C99: no libc, no floating point, no heap. */
#ifndef ZR_H
#define ZR_H
#include <stddef.h>
#include <stdint.h>

#define ZR_VERSION_MAJOR 0
#define ZR_VERSION_MINOR 2
#define ZR_VERSION_PATCH 0
#define ZR_SCHEMA_VERSION 2 /* patch / pattern / project serialization */
#define ZR_PROTOCOL_VERSION 1 /* editor SysEx protocol */

#define ZR_SAMPLE_RATE 44100
#define ZR_BLOCK_FRAMES 128
#define ZR_SCREEN_W 240
#define ZR_SCREEN_H 240

enum {
    ZR_TRACKS = 4,       /* 0..2 synth, 3 drums */
    ZR_DRUM_TRACK = 3,
    ZR_VOICES = 8,       /* shared by the three synth tracks */
    ZR_DRUMS = 8,
    ZR_STEPS = 64,
    ZR_SCENES = 8,
    ZR_NAME_LEN = 12,
    ZR_PATCH_PARAMS = 84,
    ZR_USER_PRESETS = 32,
    ZR_PROJECT_SLOTS = 8
};

/* ---- panel (matches the FM-1 SDK numbering) ---- */
enum { ZR_BTN_OCT_DOWN, ZR_BTN_OCT_UP, ZR_BTN_FX, ZR_BTN_SCL, ZR_BTN_ENV, ZR_BTN_LFO, ZR_BTN_EDIT,
       ZR_BTN_GLO, ZR_BTN_HOME, ZR_BTN_SAVE, ZR_BTN_ARP, ZR_BTN_SEQ, ZR_BTN_PLAY, ZR_BTN_REC, ZR_BUTTONS };
enum { ZR_ENC_SELECT, ZR_ENC_ALGO, ZR_ENC_K1, ZR_ENC_K2, ZR_ENC_K3, ZR_ENC_K4, ZR_ENC_PRESETS, ZR_ENCODERS };
enum { ZR_KEYS = 27, ZR_KEY_BASE_NOTE = 53 /* F3 */ };

typedef struct {
    uint32_t keys;                    /* bits 0..26: F3..G5 */
    uint32_t buttons;                 /* bits 0..13 */
    int16_t encoders[ZR_ENCODERS];    /* detents since the last read */
    uint16_t knob, battery;           /* 0..1023 */
} zr_controls;

/* ---- platform services (all optional; NULL = not available) ---- */
typedef struct {
    uint32_t (*flash_size)(void);
    int (*flash_read)(uint32_t offset, void *data, uint32_t bytes);
    int (*flash_write)(uint32_t offset, const void *data, uint32_t bytes);
    int (*flash_erase)(uint32_t offset);                 /* one 4096-byte sector */
    int (*midi_send)(const uint8_t *message, uint32_t bytes); /* 0 ok, <0 retry later */
    void (*led)(unsigned index, int on);                 /* 0..13 buttons, 14..40 keys */
} zr_platform;

/* ---- lifecycle ---- */
void zr_init(const zr_platform *platform);
/* Control context, about every 5..20 ms. */
void zr_control(const zr_controls *controls, uint32_t now_ms);
/* Audio context. out: frames*2 interleaved signed Q23. frames must be 128. */
void zr_audio(int32_t *out, unsigned frames);
/* MIDI input, complete short message (status + data). Any single context. */
void zr_midi_in(const uint8_t *message, unsigned bytes);
/* Complete SysEx frame including F0/F7. Control context. */
void zr_sysex_in(const uint8_t *frame, unsigned bytes);
/* Render rows [y0, y0+h) of the screen into buf (w=240, RGB565 native). */
void zr_draw(uint16_t *buf, unsigned y0, unsigned h);
/* Nonzero when the screen content may have changed since the last call. */
int zr_screen_dirty(void);
/* Audio load reporting from the platform (ticks used / ticks per block). */
void zr_report_load(uint32_t used, uint32_t period);

#endif
