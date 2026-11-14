/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA on the M-VAVE FM-1: adapter between the portable core and the
 * official FM-1 SDK (m-vave/fm1_open, binary libfm1_engine.a).
 *
 * STATUS: DRAFT — NEVER COMPILED FOR pi32v2, NEVER RUN ON HARDWARE.
 * It is written against sdk/include/fm1.h as read at fm1_open commit
 * 4c7496daac04a95ff10d1afdcc8f9ead2dbabc41 and replaces only app/screen.c;
 * see device/README.md for what is verified and what is not. Do not flash an
 * image built from this file until docs/SAFETY_STATUS.md says otherwise.
 *
 * Design rule: the SDK keeps everything it owns — boot, USB, Bluetooth, the
 * standard firmware-update protocol (handled before any application hook) and
 * its own engine state. ZARYA only adds its audio to the block and draws the
 * screen. It never touches the bootloader, never calls fm1_device_recovery
 * and never writes Flash outside the SDK's bounded user-partition API. */
#include "fm1.h"
#include "fm1_device.h"
#include "../core/zr.h"

/* ---- platform services for the core ---- */
static uint32_t dev_flash_size(void) { return fm1_flash_size(); }
static int dev_flash_read(uint32_t o, void *d, uint32_t n) { return fm1_flash_read(o, d, n); }
static int dev_flash_write(uint32_t o, const void *d, uint32_t n) { return fm1_flash_write(o, d, n); }
static int dev_flash_erase(uint32_t o) { return fm1_flash_erase(o); }
static int dev_midi_send(const uint8_t *m, uint32_t n) { return n <= 128 ? fm1_midi_send(m, n) : -1; }
static void dev_led(unsigned i, int on) { fm1_led_set(i, on); }
static const zr_platform dev_platform = {dev_flash_size, dev_flash_read, dev_flash_write, dev_flash_erase,
                                         dev_midi_send, dev_led};

/* ---- screen: one 240x16 band per control pass, sent by DMA ---- */
enum { BAND_ROWS = 16, BANDS = FM1_SCREEN_HEIGHT / BAND_ROWS };
static uint16_t band[FM1_SCREEN_DMA_PIXELS] __attribute__((aligned(4)));
static uint16_t band_crc[BANDS];
static uint8_t next_band = BANDS, started;

static uint16_t crc_band(void)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < FM1_SCREEN_DMA_PIXELS; ++i) h = (h ^ band[i]) * 16777619u;
    return (uint16_t)(h ^ (h >> 16)) | 1U;
}
static void screen_pump(void)
{
    if (fm1_screen_pending() > 0) return; /* DMA still owns the buffer */
    if (next_band >= BANDS) {
        if (!zr_screen_dirty()) return;
        next_band = 0;
    }
    for (unsigned tries = 0; tries < BANDS && next_band < BANDS; ++tries) {
        unsigned y = next_band * BAND_ROWS;
        zr_draw(band, y, BAND_ROWS);
        uint16_t crc = crc_band();
        ++next_band;
        if (crc == band_crc[next_band - 1]) continue; /* unchanged: no transfer */
        if (fm1_screen_submit(0, y, FM1_SCREEN_WIDTH, BAND_ROWS, band, FM1_SCREEN_DMA_PIXELS) == 0) {
            band_crc[next_band - 1] = crc;
            return;
        }
        --next_band; /* busy or error: try again on the next pass */
        return;
    }
}

/* ---- hooks called by the SDK's control task (replace app/screen.c) ---- */
void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    zr_init(&dev_platform);
    started = 1;
}
void groove_screen_control(void)
{
    struct fm1_controls c;
    zr_controls z;
    if (!started) return;
    fm1_controls_read(&c);
    z.keys = c.keys;
    z.buttons = c.buttons;
    for (unsigned i = 0; i < ZR_ENCODERS; ++i) z.encoders[i] = c.encoders[i];
    z.knob = c.knob;
    z.battery = c.battery;
    zr_control(&z, fm1_millis());
    screen_pump();
}

/* ---- MIDI: complete short messages go straight to the ZARYA engine ---- */
void fm1_app_midi_received(unsigned source, const uint8_t *message, size_t bytes)
{
    (void)source;
    if (started && message && bytes && bytes <= 3) zr_midi_in(message, (unsigned)bytes);
}

/* ---- editor SysEx (the SDK has already consumed update messages) ---- */
void zr_device_sysex(const uint8_t *message, size_t bytes)
{
    if (started && message && bytes <= 128) zr_sysex_in(message, (unsigned)bytes);
}

/* ---- audio: called from app/board/audio.c after fm1_engine_render ---- */
FM1_AUDIO_CODE void zr_device_audio(int32_t *block)
{
    if (started) zr_audio(block, FM1_BLOCK_FRAMES);
}
