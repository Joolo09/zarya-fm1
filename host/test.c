/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host test suite for the ZARYA core. Build and run: make test */
#include "../core/zr_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- fake platform ---- */
enum { FLASH = 536576 };
static uint8_t flash[FLASH];
static int flash_budget = -1; /* operations left before "power loss"; -1 = unlimited */
static unsigned flash_ops;
static int fuse(void) { ++flash_ops; if (flash_budget < 0) return 0; if (!flash_budget) return 1; --flash_budget; return 0; }
static uint32_t f_size(void) { return FLASH; }
static int f_read(uint32_t o, void *d, uint32_t n) { if (o + n > FLASH) return -1; memcpy(d, flash + o, n); return 0; }
static int f_write(uint32_t o, const void *d, uint32_t n)
{
    if (o + n > FLASH) return -1;
    const uint8_t *s = d;
    for (uint32_t i = 0; i < n; ++i) {
        if (!(i & 255) && fuse()) return -2; /* torn write: stops part-way */
        if ((flash[o + i] & s[i]) != s[i]) return -4;
        flash[o + i] &= s[i];
    }
    return 0;
}
static int f_erase(uint32_t o) { if (o % 4096 || o >= FLASH) return -1; if (fuse()) { memset(flash + o, 0x5a, 1000); return -2; } memset(flash + o, 0xff, 4096); return 0; }
static uint8_t sent[64][128]; static unsigned sent_len[64], sent_n; static int midi_busy;
static int m_send(const uint8_t *m, uint32_t n) { if (midi_busy) return -2; if (sent_n < 64 && n <= 128) { memcpy(sent[sent_n], m, n); sent_len[sent_n++] = n; } return 0; }
static const zr_platform plat = {f_size, f_read, f_write, f_erase, m_send, 0};

static int32_t out[256];
static uint32_t hash;
static double energy; static int32_t peak;
static void render(unsigned blocks)
{
    for (unsigned b = 0; b < blocks; ++b) {
        zr_audio(out, 128);
        for (unsigned i = 0; i < 256; ++i) {
            hash = (hash ^ (uint32_t)out[i]) * 16777619u;
            energy += (double)out[i] * out[i];
            if (abs(out[i]) > peak) peak = abs(out[i]);
        }
    }
}
static void reset_meter(void) { hash = 2166136261u; energy = 0; peak = 0; }
static double db(double x) { return 20 * log10(x / 8388608.0 + 1e-12); }

static void test_presets(void)
{
    printf("presets: every factory sound renders, is audible, never clips, and ends\n");
    for (unsigned i = 0; i < zr_factory_count(0); ++i) {
        zr_patch p;
        zr_init(&plat);
        zr_project_init(&zr_proj);
        zr_factory_patch(&p, i, 0);
        CHECK(zr_patch_valid(&p), "preset %u invalid", i);
        zr_load_patch(0, &p);
        zr_proj.g[ZG_REV_LEVEL] = 0; zr_proj.g[ZG_DLY_LEVEL] = 0;
        reset_meter();
        static const uint8_t chord[3] = {48, 60, 67};
        for (unsigned k = 0; k < 3; ++k) zr_post(ZQ_CONTROL, ZE_NOTE_ON, 0, chord[k], 110);
        render(345); /* 1 s */
        double rms = sqrt(energy / (345 * 256.0));
        CHECK(db(rms) > -45, "%.12s too quiet: %.1f dB", p.name, db(rms));
        CHECK(peak < 8388000, "%.12s clips", p.name);
        for (unsigned k = 0; k < 3; ++k) zr_post(ZQ_CONTROL, ZE_NOTE_OFF, 0, chord[k], 0);
        render(345 * 12);
        reset_meter(); render(20);
        CHECK(peak < 2000 && zr_status.voices == 0, "%.12s does not end (peak %d voices %u)", p.name, peak, zr_status.voices);
    }
    for (unsigned i = 0; i < zr_factory_count(1); ++i) {
        zr_patch p; zr_init(&plat); zr_project_init(&zr_proj); zr_factory_patch(&p, i, 1); zr_load_patch(3, &p);
        for (unsigned d = 0; d < 8; ++d) {
            reset_meter(); zr_post(ZQ_CONTROL, ZE_NOTE_ON, 3, d, 120); render(60);
            CHECK(db(peak) > -30 && peak < 8388000, "kit %u drum %u peak %.1f dB", i, d, db(peak));
        }
    }
}
static void test_determinism(void)
{
    printf("determinism: the demo song renders bit-identically twice\n");
    uint32_t h[2];
    for (unsigned r = 0; r < 2; ++r) {
        zr_init(&plat); reset_meter();
        zr_post(ZQ_CONTROL, ZE_PLAY, 0xff, 0, 0);
        render(345 * 6);
        h[r] = hash;
    }
    CHECK(h[0] == h[1], "hash %08x != %08x", h[0], h[1]);
    double rms = sqrt(energy / (345 * 6 * 256.0));
    printf("  demo: peak %.1f dBFS, rms %.1f dBFS, hash %08x\n", db(peak), db(rms), h[0]);
    CHECK(peak < 8388000 && db(rms) > -30, "demo level");
    zr_post(ZQ_CONTROL, ZE_STOP, 0xff, 0, 0); zr_post(ZQ_CONTROL, ZE_PANIC, 0xff, 0, 0); render(4);
    reset_meter(); render(700); reset_meter(); render(10);
    CHECK(zr_status.voices == 0, "stuck voices after stop: %u", zr_status.voices);
}
static void test_stress(void)
{
    printf("stress: random notes, parameters and extreme filter settings stay bounded\n");
    zr_init(&plat);
    uint32_t rng = 12345;
    zr_post(ZQ_CONTROL, ZE_PLAY, 0xff, 0, 0);
    reset_meter();
    for (unsigned b = 0; b < 6000; ++b) {
        if (!(b % 3)) zr_post(ZQ_MIDI, (zr_rand(&rng) & 1) ? ZE_NOTE_ON : ZE_NOTE_OFF, zr_rand(&rng) % 4, zr_rand(&rng) % 128, zr_rand(&rng) % 128);
        if (!(b % 7)) { unsigned t = zr_rand(&rng) % 4, id = zr_rand(&rng) % ZR_PATCH_PARAMS; zr_set_patch_param(t, id, (int)(zr_rand(&rng) % 300) - 80, 0); }
        if (!(b % 11)) zr_set_global(zr_rand(&rng) % ZG_COUNT, (int)(zr_rand(&rng) % 300) - 40, 0);
        if (!(b % 500)) { zr_patch p; zr_factory_patch(&p, zr_rand(&rng), 0); zr_load_patch(zr_rand(&rng) % 3, &p); }
        render(1);
        for (unsigned i = 0; i < 256; ++i) if (out[i] > 8388607 || out[i] < -8388608) { CHECK(0, "sample out of Q23"); return; }
    }
    for (unsigned t = 0; t < 4; ++t) CHECK(zr_patch_valid(&zr_proj.patch[t]), "patch %u invalid after stress", t);
    CHECK(checks, "ran");
}
static void test_generator(void)
{
    printf("generator: deterministic, in scale, in range\n");
    for (unsigned style = 0; style < 5; ++style)
        for (unsigned seed = 0; seed < 200; ++seed) {
            zr_gen_params gp = {(uint16_t)(seed * 37), seed % 12, seed % ZR_SCALE_COUNT, (uint8_t)(seed * 7 % 128), (uint8_t)(seed * 13 % 128),
                                (uint8_t)(1 + seed % 3), (uint8_t)(16 * (1 + seed % 4)), (uint8_t)(seed * 3 % 128), (uint8_t)(style & 3), 0};
            gp.base_note = (uint8_t)(48 + gp.root);
            zr_pattern a, b;
            zr_generate(&a, &gp, style == 4); zr_generate(&b, &gp, style == 4);
            CHECK(!memcmp(&a, &b, sizeof a), "not deterministic style %u seed %u", style, seed);
            CHECK(a.length == gp.length, "length");
            unsigned notes = 0;
            for (unsigned i = 0; i < 64; ++i) {
                if (!a.steps[i].note) continue;
                ++notes;
                CHECK(i < a.length, "note beyond length");
                if (style < 4) {
                    unsigned n = a.steps[i].note;
                    CHECK(zr_scale_snap(n, gp.root, gp.scale) == n, "note %u out of scale (style %u seed %u)", n, style, seed);
                    CHECK(n >= gp.base_note && n <= gp.base_note + 12u * gp.range + 12u, "note %u out of range", n);
                }
            }
            CHECK(notes > 0, "empty pattern style %u seed %u", style, seed);
        }
    zr_gen_params g1 = {1, 0, 0, 80, 50, 1, 16, 40, 0, 60}, g2 = g1; g2.seed = 2;
    zr_pattern a, b; zr_generate(&a, &g1, 0); zr_generate(&b, &g2, 0);
    CHECK(memcmp(&a, &b, sizeof a), "different seeds gave the same melody");
}
static void test_storage(void)
{
    printf("storage: never writes unformatted flash; survives power loss at every step\n");
    memset(flash, 0x42, FLASH); /* foreign data */
    static uint8_t before[FLASH];
    memcpy(before, flash, FLASH);
    zr_init(&plat);
    CHECK(zr_store_state() == ZS_UNFORMATTED, "foreign flash must be UNFORMATTED");
    CHECK(zr_project_save(0) < 0 && zr_user_presets_save() < 0, "save must be refused");
    CHECK(!memcmp(before, flash, FLASH), "flash modified without format");
    CHECK(zr_store_format() == 0 && zr_store_state() == ZS_READY, "format");
    zr_name_set(zr_proj.name, "VERSION ONE");
    CHECK(zr_project_save(2) == 0, "save v1");
    static zr_project v1, v2;
    v1 = zr_proj;
    zr_name_set(zr_proj.name, "VERSION TWO"); zr_proj.g[ZG_TEMPO] = 150; v2 = zr_proj;
    static uint8_t snap[FLASH];
    memcpy(snap, flash, FLASH);
    unsigned total_ops; flash_ops = 0; CHECK(zr_project_save(2) == 0, "save v2"); total_ops = flash_ops;
    unsigned old = 0, new = 0;
    for (unsigned cut = 0; cut < total_ops; ++cut) {
        memcpy(flash, snap, FLASH);
        zr_init(&plat); zr_proj = v2;
        flash_budget = (int)cut;
        int r = zr_project_save(2);
        flash_budget = -1;
        CHECK(r != 0, "save reported success despite power loss at op %u", cut);
        zr_init(&plat);
        CHECK(zr_store_state() == ZS_READY, "store lost at op %u", cut);
        CHECK(zr_project_load(2) == 0, "no valid copy after power loss at op %u", cut);
        if (!memcmp(&zr_proj, &v1, sizeof v1)) ++old; else if (!memcmp(&zr_proj, &v2, sizeof v2)) ++new; else CHECK(0, "corrupt project at op %u", cut);
    }
    printf("  %u cut points: %u kept the old version, %u had the new one\n", total_ops, old, new);
    CHECK(old == total_ops, "an interrupted save must keep the previous version");
    /* many saves alternate copies and stay readable */
    for (unsigned i = 0; i < 9; ++i) { zr_proj.g[ZG_TEMPO] = (uint8_t)i; CHECK(zr_project_save(2) == 0, "resave"); }
    zr_init(&plat); CHECK(zr_project_load(2) == 0 && zr_proj.g[ZG_TEMPO] == 8, "latest version wins");
    CHECK(zr_project_load(5) == -3, "empty slot");
    /* corrupt one byte of the newest copy: the older one is used */
    for (uint32_t o = 5 * 4096; o < FLASH; o += 4096) if (flash[o] == 0x43 && flash[o + 4] == 4) flash[o + 300] ^= 1;
    zr_init(&plat); int r = zr_project_load(2);
    CHECK(r == -3 || r == 0, "corruption handled (%d)", r);
    zr_factory_patch(&zr_user_presets[3], 7, 0); CHECK(zr_user_presets_save() == 0, "preset save");
    zr_init(&plat); CHECK(zr_user_presets[3].type == 0 && !memcmp(zr_user_presets[3].name, "SYNC CRY", 8), "preset reload");
    CHECK(zr_user_presets[4].type == 0xff, "empty preset slot");
}
static unsigned frame(uint8_t *f, unsigned cmd, unsigned seq, const uint8_t *payload, unsigned n)
{
    f[0] = 0xf0; f[1] = 0x7d; f[2] = 'Z'; f[3] = 'R'; f[4] = (uint8_t)cmd; f[5] = (uint8_t)seq;
    unsigned len = 6 + zr_pack7(f + 6, payload, n);
    f[len++] = 0xf7;
    return len;
}
static unsigned reply(unsigned i, uint8_t *d) { return zr_unpack7(d, sent[i] + 6, sent_len[i] - 7); }
static void pump(void) { for (unsigned i = 0; i < 20; ++i) zr_control(0, i * 10); }
static void test_protocol(void)
{
    printf("protocol: pack7, hello, blob get/put, malformed frames\n");
    uint8_t f[160], d[160], raw[120], back[140];
    for (unsigned n = 0; n < 100; ++n) {
        for (unsigned i = 0; i < n; ++i) raw[i] = (uint8_t)(i * 37 + n);
        unsigned pl = zr_pack7(f, raw, n), ul = zr_unpack7(back, f, pl);
        for (unsigned i = 0; i < pl; ++i) CHECK(f[i] < 128, "pack7 high bit");
        CHECK(ul == n && !memcmp(raw, back, n), "pack7 roundtrip %u", n);
    }
    memset(flash, 0xff, FLASH); zr_init(&plat); sent_n = 0;
    zr_sysex_in(f, frame(f, 1, 5, 0, 0)); pump();
    CHECK(sent_n == 1u && sent[0][4] == 0x41 && sent[0][5] == 5, "hello reply");
    reply(0, d);
    CHECK(d[0] == ZR_PROTOCOL_VERSION && d[4] == ZR_SCHEMA_VERSION && !(d[9] & 0x60), "hello content; no persist/flash capability");
    CHECK(sent_len[0] <= 128, "frame size");
    /* GET every blob kind and reassemble */
    static const uint8_t reqs[3][3] = {{ZB_PATCH, 1, 0}, {ZB_PATTERN, 0, 2}, {ZB_GLOBALS, 0, 0}};
    static uint8_t blob[3][300]; unsigned blen[3];
    for (unsigned q = 0; q < 3; ++q) {
        sent_n = 0; zr_sysex_in(f, frame(f, 2, 9, reqs[q], 3)); pump(); pump();
        unsigned got = 0, total = 0;
        for (unsigned i = 0; i < sent_n; ++i) {
            unsigned n = reply(i, d);
            CHECK(sent[i][4] == 0x42 && sent_len[i] <= 128, "blob frame");
            total = d[5] | (d[6] << 8);
            CHECK((unsigned)(d[3] | (d[4] << 8)) == got, "blob offset");
            memcpy(blob[q] + got, d + 7, n - 7); got += n - 7;
        }
        blen[q] = got;
        CHECK(got == total && zr_blob_check(blob[q], got, reqs[q][0], 0) > 0, "blob %u complete (%u/%u)", q, got, total);
    }
    /* PUT the patch of track 1 onto track 0, chunked */
    sent_n = 0;
    for (unsigned off = 0; off < blen[0];) {
        unsigned n = blen[0] - off < 96 ? blen[0] - off : 96;
        uint8_t pl[110] = {ZB_PATCH, 0, 0, (uint8_t)off, (uint8_t)(off >> 8), (uint8_t)blen[0], (uint8_t)(blen[0] >> 8)};
        memcpy(pl + 7, blob[0] + off, n);
        zr_sysex_in(f, frame(f, 3, 1, pl, 7 + n)); off += n;
    }
    pump();
    reply(sent_n - 1, d);
    CHECK(sent[sent_n - 1][4] == 0x43 && d[3] == 0 && d[6] == 1, "put ack ok");
    CHECK(!memcmp(zr_proj.patch[0].p, zr_proj.patch[1].p, ZR_PATCH_PARAMS), "patch applied");
    /* corrupted blob is rejected and changes nothing */
    zr_patch keep = zr_proj.patch[2]; sent_n = 0;
    blob[0][40] ^= 0x10;
    for (unsigned off = 0; off < blen[0];) {
        unsigned n = blen[0] - off < 96 ? blen[0] - off : 96;
        uint8_t pl[110] = {ZB_PATCH, 2, 0, (uint8_t)off, (uint8_t)(off >> 8), (uint8_t)blen[0], (uint8_t)(blen[0] >> 8)};
        memcpy(pl + 7, blob[0] + off, n);
        zr_sysex_in(f, frame(f, 3, 1, pl, 7 + n)); off += n;
    }
    pump(); reply(sent_n - 1, d);
    CHECK(d[3] != 0 && !memcmp(&keep, &zr_proj.patch[2], sizeof keep), "corrupt blob rejected");
    /* SET_PARAM clamps; unknown and malformed frames are safe */
    uint8_t sp[4] = {0, 0, ZP_ALGO, 127}; zr_sysex_in(f, frame(f, 4, 0, sp, 4));
    CHECK(zr_proj.patch[0].p[ZP_ALGO] == 7, "set_param clamps");
    uint32_t rng = 99; zr_project before = zr_proj;
    for (unsigned i = 0; i < 20000; ++i) {
        unsigned n = zr_rand(&rng) % 150;
        for (unsigned k = 0; k < n; ++k) f[k] = (uint8_t)zr_rand(&rng);
        if (i & 1) { f[0] = 0xf0; f[1] = 0x7d; f[2] = 'Z'; f[3] = 'R'; if (n > 6) f[n - 1] = 0xf7; f[4] = (uint8_t)(10 + zr_rand(&rng) % 100); }
        zr_sysex_in(f, n);
        if (!(i % 50)) pump();
    }
    CHECK(!memcmp(&before, &zr_proj, sizeof before), "fuzzed frames with unknown commands changed the project");
    static uint8_t ff[FLASH]; memset(ff, 0xff, FLASH);
    CHECK(!memcmp(ff, flash, FLASH), "the protocol wrote flash");
    midi_busy = 1; sent_n = 0; zr_sysex_in(f, frame(f, 1, 1, 0, 0)); pump(); CHECK(sent_n == 0, "busy port");
    midi_busy = 0; pump(); CHECK(sent_n >= 1, "reply sent after busy");
}
static void test_ui(void)
{
    printf("ui: every page draws inside its band; panel gestures work\n");
    static uint16_t fb[240 * 240 + 480]; static uint16_t band[240 * 16 + 480];
    zr_init(&plat);
    zr_controls c; memset(&c, 0, sizeof c);
    uint32_t now = 0;
    static const uint8_t btn[10] = {ZR_BTN_HOME, ZR_BTN_EDIT, ZR_BTN_ENV, ZR_BTN_LFO, ZR_BTN_FX, ZR_BTN_SCL, ZR_BTN_GLO, ZR_BTN_ARP, ZR_BTN_SEQ, ZR_BTN_SAVE};
    if (system("mkdir -p build/screens")) return;
    for (unsigned trk = 0; trk < 4; trk += 3)
    for (unsigned p = 0; p < 10; ++p) {
        c.buttons = 1U << btn[p]; zr_control(&c, now += 10); c.buttons = 0; zr_control(&c, now += 10);
        for (unsigned sub = 0; sub < 7; ++sub) {
            for (unsigned i = 0; i < 240 * 240 + 480; ++i) fb[i] = 0xdead;
            zr_draw(fb + 240, 0, 240);
            for (unsigned i = 0; i < 240; ++i) CHECK(fb[i] == 0xdead && fb[240 + 57600 + i] == 0xdead, "draw overflow");
            for (unsigned y = 0; y < 240; y += 16) { /* band rendering equals full-frame rendering */
                for (unsigned i = 0; i < 240 * 16 + 480; ++i) band[i] = 0xdead;
                zr_draw(band + 240, y, 16);
                CHECK(!memcmp(band + 240, fb + 240 + y * 240, 240 * 16 * 2) && band[0] == 0xdead && band[240 + 3840] == 0xdead, "band mismatch page %u y %u", p, y);
            }
            if (sub == 0) {
                char name[64]; sprintf(name, "build/screens/t%u_page%u.ppm", trk + 1, p);
                FILE *f = fopen(name, "wb"); fprintf(f, "P6 240 240 255\n");
                for (unsigned i = 0; i < 57600; ++i) { uint16_t v = fb[240 + i]; fputc((v >> 8) & 0xf8, f); fputc((v >> 3) & 0xfc, f); fputc((v << 3) & 0xf8, f); }
                fclose(f);
            }
            c.encoders[ZR_ENC_ALGO] = 1; c.encoders[ZR_ENC_K1] = 2; c.encoders[ZR_ENC_K4] = -1; zr_control(&c, now += 10);
            memset(c.encoders, 0, sizeof c.encoders);
        }
        if (p == 9 && trk == 0) { c.encoders[ZR_ENC_SELECT] = 3; zr_control(&c, now += 10); c.encoders[ZR_ENC_SELECT] = 0; }
    }
    /* keys play notes, PLAY starts the sequencer, PRESETS browses */
    zr_init(&plat); memset(&c, 0, sizeof c);
    c.keys = 1U << 7; zr_control(&c, now += 10); render(4); CHECK(zr_status.voices == 1, "key -> voice");
    c.keys = 0; zr_control(&c, now += 10); render(4000); CHECK(zr_status.voices == 0, "key release");
    c.buttons = 1U << ZR_BTN_PLAY; zr_control(&c, now += 10); c.buttons = 0; render(4); CHECK(zr_status.playing, "play");
    char before[12]; memcpy(before, zr_proj.patch[0].name, 12);
    c.encoders[ZR_ENC_PRESETS] = 1; zr_control(&c, now += 10); c.encoders[ZR_ENC_PRESETS] = 0;
    CHECK(memcmp(before, zr_proj.patch[0].name, 12), "preset browse");
    /* generate + undo on the SCALE page */
    c.buttons = 1U << ZR_BTN_SCL; zr_control(&c, now += 10); c.buttons = 0; zr_control(&c, now += 10);
    zr_pattern pat = zr_proj.pat[0][0];
    zr_proj.g[ZG_GEN_SEED_L] = 99;
    c.buttons = 1U << ZR_BTN_SAVE; zr_control(&c, now += 10); c.buttons = 0; zr_control(&c, now += 10);
    CHECK(memcmp(&pat, &zr_proj.pat[0][0], sizeof pat), "generate changed the pattern");
    c.buttons = 1U << ZR_BTN_REC; zr_control(&c, now += 10); c.buttons = 0; zr_control(&c, now += 10);
    CHECK(!memcmp(&pat, &zr_proj.pat[0][0], sizeof pat), "undo restored the pattern");
}
static void test_record(void)
{
    printf("sequencer: live record writes quantized steps; swing and ratchets are stable\n");
    zr_init(&plat); zr_project_init(&zr_proj);
    for (unsigned t = 0; t < 4; ++t) zr_patch_touch(t);
    zr_live.record = 1;
    zr_post(ZQ_CONTROL, ZE_PLAY, 0xff, 0, 0); render(2);
    render(86); /* ~2 steps at 120 BPM (43 blocks per 16th) */
    zr_post(ZQ_CONTROL, ZE_NOTE_ON, 0, 64, 90); render(20); zr_post(ZQ_CONTROL, ZE_NOTE_OFF, 0, 64, 0); render(4);
    unsigned found = 99;
    for (unsigned i = 0; i < 16; ++i) if (zr_proj.pat[0][0].steps[i].note == 64) found = i;
    CHECK(found == 2, "recorded step %u (expected 2)", found);
    zr_proj.g[ZG_SWING] = 25; zr_proj.pat[0][0].steps[5].note = 60; zr_proj.pat[0][0].steps[5].vel = 100; zr_proj.pat[0][0].steps[5].gate = 0x33;
    reset_meter(); render(345 * 4); CHECK(peak < 8388000, "level");
    zr_post(ZQ_CONTROL, ZE_STOP, 0xff, 0, 0); render(345 * 4); CHECK(zr_status.voices == 0, "voices end after stop");
}

static void dump_schema(const char *path)
{
    FILE *f = fopen(path, "w");
    char t[12];
    #define DESC(fn, count, label) fprintf(f, "  \"%s\": [\n", label); for (unsigned i = 0; i < (count); ++i) { const zr_pdesc *d = fn(i); \
        fprintf(f, "    {\"id\": %u, \"name\": \"%s\", \"min\": %u, \"max\": %u, \"def\": %u, \"fmt\": %u, \"labels\": [", i, d->name, d->min, d->max, d->def, d->fmt); \
        for (unsigned v = d->min; v <= d->max; ++v) fprintf(f, "%s\"%s\"", v > d->min ? ", " : "", zr_format(d, v, t)); \
        fprintf(f, "]}%s\n", i + 1 < (count) ? "," : ""); } fprintf(f, "  ],\n");
    fprintf(f, "{\n  \"schema\": %d, \"protocol\": %d, \"version\": \"%d.%d.%d\",\n", ZR_SCHEMA_VERSION, ZR_PROTOCOL_VERSION, ZR_VERSION_MAJOR, ZR_VERSION_MINOR, ZR_VERSION_PATCH);
    fprintf(f, "  \"tracks\": %d, \"steps\": %d, \"scenes\": %d, \"patchParams\": %d, \"globals\": %d, \"opBase\": %d, \"opFields\": %d, \"trackBase\": %d, \"trackFields\": %d,\n",
            ZR_TRACKS, ZR_STEPS, ZR_SCENES, ZR_PATCH_PARAMS, ZR_GLOBALS, ZP_OP_BASE, ZO_COUNT, ZG_TRACK_BASE, ZT_COUNT);
    DESC(zr_patch_desc, ZR_PATCH_PARAMS, "patch") DESC(zr_kit_desc, ZK_COUNT, "kit") DESC(zr_global_desc, ZG_COUNT, "global")
    fprintf(f, "  \"scales\": ["); for (unsigned i = 0; i < ZR_SCALE_COUNT; ++i) fprintf(f, "%s{\"name\": \"%s\", \"mask\": %u}", i ? ", " : "", zr_scale_names[i], zr_scale_mask[i]);
    fprintf(f, "],\n  \"chords\": ["); for (unsigned i = 0; i < 8; ++i) fprintf(f, "%s\"%s\"", i ? ", " : "", zr_chord_names[i]);
    fprintf(f, "],\n  \"drums\": ["); for (unsigned i = 0; i < 8; ++i) fprintf(f, "%s\"%s\"", i ? ", " : "", zr_drum_names[i]);
    fprintf(f, "],\n  \"categories\": ["); for (unsigned i = 0; i < 8; ++i) fprintf(f, "%s\"%s\"", i ? ", " : "", zr_category_names[i]);
    fprintf(f, "],\n  \"gateEighths\": ["); for (unsigned i = 0; i < 16; ++i) fprintf(f, "%s%u", i ? ", " : "", zr_gate_eighths[i]);
    fprintf(f, "]\n}\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--schema")) { dump_schema(argv[2]); return 0; }
    memset(flash, 0xff, FLASH);
    test_presets(); test_determinism(); test_stress(); test_generator(); test_storage(); test_protocol(); test_ui(); test_record();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
