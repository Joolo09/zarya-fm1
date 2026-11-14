/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* WebAssembly shell around the ZARYA core: the firmware simulator used by the
 * web editor (DEMO / NO HARDWARE) and by the web tests. */
#include "../core/zr.h"

void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, unsigned long n)
{
    unsigned char *a = d; const unsigned char *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else while (n--) a[n] = b[n];
    return d;
}

enum { FLASH = 536576, OUT = 8192 };
static unsigned char flash[FLASH], outbuf[OUT], inbuf[256];
static unsigned outlen;
static int32_t audio[2 * ZR_BLOCK_FRAMES];
static uint16_t fb[ZR_SCREEN_W * ZR_SCREEN_H];
static zr_controls controls;
static unsigned leds_lo, leds_hi;

static uint32_t f_size(void) { return FLASH; }
static int f_read(uint32_t o, void *d, uint32_t n) { if (o + n > FLASH) return -1; memcpy(d, flash + o, n); return 0; }
static int f_write(uint32_t o, const void *d, uint32_t n)
{
    if (o + n > FLASH) return -1;
    for (uint32_t i = 0; i < n; ++i) flash[o + i] &= ((const unsigned char *)d)[i];
    return 0;
}
static int f_erase(uint32_t o) { if (o % 4096 || o >= FLASH) return -1; memset(flash + o, 0xff, 4096); return 0; }
static int m_send(const uint8_t *m, uint32_t n)
{
    if (n > 255 || outlen + n + 1 > OUT) return -2;
    outbuf[outlen++] = (unsigned char)n;
    memcpy(outbuf + outlen, m, n);
    outlen += n;
    return 0;
}
static void led(unsigned i, int on)
{
    if (i < 32) leds_lo = on ? leds_lo | (1u << i) : leds_lo & ~(1u << i);
    else if (i < 64) leds_hi = on ? leds_hi | (1u << (i - 32)) : leds_hi & ~(1u << (i - 32));
}
static const zr_platform plat = {f_size, f_read, f_write, f_erase, m_send, led};

#define EXPORT __attribute__((visibility("default")))
EXPORT void zw_init(int blank_flash) { if (blank_flash) memset(flash, 0xff, FLASH); outlen = 0; zr_init(&plat); }
EXPORT int32_t *zw_audio(void) { zr_audio(audio, ZR_BLOCK_FRAMES); return audio; }
EXPORT zr_controls *zw_controls(void) { return &controls; }
EXPORT void zw_tick(uint32_t now_ms) { zr_control(&controls, now_ms); for (unsigned i = 0; i < ZR_ENCODERS; ++i) controls.encoders[i] = 0; }
EXPORT uint16_t *zw_fb(void) { return fb; }
EXPORT int zw_draw(int force) { if (!zr_screen_dirty() && !force) return 0; zr_draw(fb, 0, ZR_SCREEN_H); return 1; }
EXPORT unsigned char *zw_inbuf(void) { return inbuf; }
EXPORT void zw_sysex(unsigned n) { if (n <= sizeof inbuf) zr_sysex_in(inbuf, n); }
EXPORT void zw_midi(unsigned status, unsigned a, unsigned b) { uint8_t m[3] = {(uint8_t)status, (uint8_t)a, (uint8_t)b}; zr_midi_in(m, 3); }
EXPORT unsigned char *zw_out(void) { return outbuf; }
EXPORT unsigned zw_out_len(void) { return outlen; }
EXPORT void zw_out_clear(void) { outlen = 0; }
EXPORT unsigned char *zw_flash(void) { return flash; }
EXPORT unsigned zw_flash_size(void) { return FLASH; }
EXPORT unsigned zw_leds(int hi) { return hi ? leds_hi : leds_lo; }
EXPORT void zw_load(uint32_t used, uint32_t period) { zr_report_load(used, period); }
EXPORT unsigned char *zw_blob_pack_ptr(void) { return outbuf; }
