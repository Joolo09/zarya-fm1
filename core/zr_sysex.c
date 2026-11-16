/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA editor protocol (docs/PROTOCOL.md). Volatile edits only: nothing in
 * this file writes Flash, and there is no firmware-update command here — the
 * FM-1 SDK handles the standard update protocol itself, before this code.
 *
 * Frame: F0 7D 'Z' 'R' <cmd> <seq> <payload, 7-bit packed> F7, at most 128
 * bytes (the SDK's limit for one outgoing message). */
#include "zr_dsp.h"

enum { CMD_HELLO = 1, CMD_GET_BLOB, CMD_PUT_BLOB, CMD_SET_PARAM, CMD_TRANSPORT, CMD_NOTE, CMD_GET_STATUS,
       CMD_GENERATE, CMD_SELECT, CMD_LIVE,
       RSP_HELLO = 0x41, RSP_BLOB, RSP_PUT_ACK, RSP_STATUS = 0x47, RSP_GENERATED, EVT_PARAM = 0x4a, EVT_PATCH,
       RSP_ERROR = 0x4f };
enum { ERR_FRAME = 1, ERR_COMMAND, ERR_ARGS, ERR_BUSY, ERR_BLOB, ERR_DATA };
enum { CAP_PARAM = 1, CAP_BLOB_GET = 2, CAP_BLOB_PUT = 4, CAP_TRANSPORT = 8, CAP_GENERATE = 16,
       CAP_PERSIST = 32 /* not offered */, CAP_FLASH = 64 /* never offered */, CAP_LIVE = 128 };
enum { FRAME_MAX = 128, PAYLOAD_MAX = 104, CHUNK = 96, BLOB_MAX = ZR_BLOB_HEADER + 260, RING = 8 };

typedef struct { uint8_t cmd, seq, len, data[PAYLOAD_MAX]; } msg_t;
static const zr_platform *plat;
static msg_t ring[RING];
static uint8_t ring_w, ring_r, connected;
static struct { uint8_t active, kind, a, b, seq; uint16_t total, off; uint8_t buf[BLOB_MAX]; } tx;
static struct { uint8_t active, kind, a, b; uint16_t total, got; uint32_t last_ms; uint8_t buf[BLOB_MAX]; } rx;
static uint32_t now;

unsigned zr_pack7(uint8_t *out, const uint8_t *in, unsigned bytes)
{
    unsigned o = 0;
    for (unsigned i = 0; i < bytes; i += 7) {
        unsigned n = bytes - i < 7 ? bytes - i : 7, msb = 0;
        for (unsigned k = 0; k < n; ++k) msb |= (unsigned)(in[i + k] >> 7) << k;
        out[o++] = (uint8_t)msb;
        for (unsigned k = 0; k < n; ++k) out[o++] = in[i + k] & 0x7f;
    }
    return o;
}
/* Returns the decoded length. A group of one byte (MSBs without data) is ignored. */
unsigned zr_unpack7(uint8_t *out, const uint8_t *in, unsigned bytes)
{
    unsigned o = 0;
    for (unsigned i = 0; i < bytes;) {
        unsigned msb = in[i++], n = bytes - i < 7 ? bytes - i : 7;
        for (unsigned k = 0; k < n; ++k) out[o++] = (uint8_t)(in[i + k] | (((msb >> k) & 1U) << 7));
        i += n;
    }
    return o;
}

static msg_t *push(unsigned cmd, unsigned seq)
{
    if ((uint8_t)(ring_w - ring_r) >= RING) ++ring_r; /* drop the oldest */
    msg_t *m = &ring[ring_w++ % RING];
    m->cmd = (uint8_t)cmd; m->seq = (uint8_t)(seq & 0x7f); m->len = 0;
    return m;
}
static void error(unsigned seq, unsigned cmd, unsigned code, unsigned detail)
{
    msg_t *m = push(RSP_ERROR, seq);
    m->data[0] = (uint8_t)cmd; m->data[1] = (uint8_t)code; m->data[2] = (uint8_t)detail; m->len = 3;
}
static int send(unsigned cmd, unsigned seq, const uint8_t *payload, unsigned len)
{
    uint8_t f[FRAME_MAX];
    if (!plat || !plat->midi_send || len > PAYLOAD_MAX) return -1;
    f[0] = 0xf0; f[1] = 0x7d; f[2] = 'Z'; f[3] = 'R'; f[4] = (uint8_t)cmd; f[5] = (uint8_t)(seq & 0x7f);
    unsigned n = 6 + zr_pack7(f + 6, payload, len);
    f[n++] = 0xf7;
    return plat->midi_send(f, n);
}

void zr_sysex_init(const zr_platform *p)
{
    plat = p;
    ring_w = ring_r = connected = 0;
    tx.active = rx.active = 0;
}

void zr_sysex_notify_param(unsigned scope, unsigned track, unsigned id, unsigned value)
{
    if (!connected) return;
    /* coalesce with a queued change of the same parameter */
    for (uint8_t i = ring_r; i != ring_w; ++i) {
        msg_t *q = &ring[i % RING];
        if (q->cmd == EVT_PARAM && q->data[0] == scope && q->data[1] == track && q->data[2] == id) {
            q->data[3] = (uint8_t)value;
            return;
        }
    }
    msg_t *m = push(EVT_PARAM, 0);
    m->data[0] = (uint8_t)scope; m->data[1] = (uint8_t)track; m->data[2] = (uint8_t)id; m->data[3] = (uint8_t)value;
    m->len = 4;
}
void zr_sysex_notify_patch(unsigned track)
{
    if (!connected) return;
    msg_t *m = push(EVT_PATCH, 0);
    m->data[0] = (uint8_t)track; m->len = 1;
}

static int blob_source(unsigned kind, unsigned a, unsigned b, const void **data, uint32_t *bytes)
{
    switch (kind) {
    case ZB_PATCH: if (a >= ZR_TRACKS) return -1; *data = &zr_proj.patch[a]; *bytes = sizeof(zr_patch); return 0;
    case ZB_PATTERN:
        if (a >= ZR_SCENES || b >= ZR_TRACKS) return -1;
        *data = &zr_proj.pat[a][b]; *bytes = sizeof(zr_pattern); return 0;
    case ZB_GLOBALS: *data = zr_proj.g; *bytes = ZR_GLOBALS; return 0;
    default: return -1;
    }
}
static unsigned blob_apply(void)
{
    const void *dst; uint32_t bytes;
    if (blob_source(rx.kind, rx.a, rx.b, &dst, &bytes) != 0) return ERR_ARGS;
    int r = zr_blob_check(rx.buf, rx.total, rx.kind, bytes);
    if (r < 0) return ERR_BLOB;
    const uint8_t *payload = rx.buf + ZR_BLOB_HEADER;
    if (rx.kind == ZB_PATCH) {
        zr_patch p;
        zr_copy(&p, payload, sizeof p);
        p.type = rx.a == ZR_DRUM_TRACK ? ZR_PATCH_KIT : ZR_PATCH_SYNTH;
        if (!zr_patch_valid(&p)) return ERR_DATA;
        zr_load_patch(rx.a, &p);
    } else if (rx.kind == ZB_PATTERN) {
        zr_pattern p;
        zr_copy(&p, payload, sizeof p);
        zr_pattern_sanitize(&p);
        zr_copy(&zr_proj.pat[rx.a][rx.b], &p, sizeof p);
    } else {
        uint8_t g[ZR_GLOBALS];
        zr_copy(g, payload, ZR_GLOBALS);
        zr_globals_sanitize(g);
        g[ZG_SCENE] = zr_proj.g[ZG_SCENE]; /* the scene changes on the bar, via SELECT */
        zr_copy(zr_proj.g, g, ZR_GLOBALS);
    }
    zr_ui_touch();
    return 0;
}

void zr_sysex_in(const uint8_t *f, unsigned n)
{
    uint8_t d[PAYLOAD_MAX + 8];
    if (!f || n < 7 || n > FRAME_MAX || f[0] != 0xf0 || f[n - 1] != 0xf7) return;
    if (f[1] != 0x7d || f[2] != 'Z' || f[3] != 'R') return; /* not ours: ignore silently */
    for (unsigned i = 4; i < n - 1; ++i) if (f[i] & 0x80) return;
    unsigned cmd = f[4], seq = f[5];
    if (n - 7 > 119) { error(seq, cmd, ERR_FRAME, 0); return; }
    unsigned len = zr_unpack7(d, f + 6, n - 7);
    switch (cmd) {
    case CMD_HELLO: {
        connected = 1;
        msg_t *m = push(RSP_HELLO, seq);
        const uint8_t hello[] = {ZR_PROTOCOL_VERSION, ZR_VERSION_MAJOR, ZR_VERSION_MINOR, ZR_VERSION_PATCH,
            ZR_SCHEMA_VERSION, ZR_TRACKS, ZR_VOICES, ZR_STEPS, ZR_SCENES,
            CAP_PARAM | CAP_BLOB_GET | CAP_BLOB_PUT | CAP_TRANSPORT | CAP_GENERATE | CAP_LIVE, 0,
            (uint8_t)zr_store_state(), ZR_PATCH_PARAMS, ZR_GLOBALS,
#ifdef ZR_TARGET_FM1
            1,
#else
            0, /* simulator build */
#endif
        };
        zr_copy(m->data, hello, sizeof hello);
        m->len = sizeof hello;
        break;
    }
    case CMD_GET_BLOB: {
        const void *src; uint32_t bytes;
        if (len < 3 || blob_source(d[0], d[1], d[2], &src, &bytes) != 0) { error(seq, cmd, ERR_ARGS, 0); break; }
        if (tx.active) { error(seq, cmd, ERR_BUSY, 0); break; }
        tx.kind = d[0]; tx.a = d[1]; tx.b = d[2]; tx.seq = (uint8_t)seq;
        tx.total = (uint16_t)zr_blob_pack(tx.buf, d[0], src, bytes);
        tx.off = 0;
        tx.active = 1;
        break;
    }
    case CMD_PUT_BLOB: {
        if (len < 8) { error(seq, cmd, ERR_ARGS, 0); break; }
        unsigned off = d[3] | (d[4] << 8), total = d[5] | (d[6] << 8), count = len - 7;
        if (total > BLOB_MAX || off + count > total || count > CHUNK) { error(seq, cmd, ERR_ARGS, 1); rx.active = 0; break; }
        if (!off) { rx.active = 1; rx.kind = d[0]; rx.a = d[1]; rx.b = d[2]; rx.total = (uint16_t)total; rx.got = 0; }
        if (!rx.active || rx.kind != d[0] || rx.a != d[1] || rx.b != d[2] || rx.total != total || rx.got != off) {
            error(seq, cmd, ERR_ARGS, 2);
            rx.active = 0;
            break;
        }
        zr_copy(rx.buf + off, d + 7, count);
        rx.got = (uint16_t)(off + count);
        rx.last_ms = now;
        unsigned status = 0, done = rx.got == rx.total;
        if (done) { status = blob_apply(); rx.active = 0; }
        msg_t *m = push(RSP_PUT_ACK, seq);
        m->data[0] = d[0]; m->data[1] = d[1]; m->data[2] = d[2]; m->data[3] = (uint8_t)status;
        m->data[4] = (uint8_t)rx.got; m->data[5] = (uint8_t)(rx.got >> 8); m->data[6] = (uint8_t)done;
        m->len = 7;
        break;
    }
    case CMD_SET_PARAM:
        if (len < 4 || d[1] >= ZR_TRACKS) { error(seq, cmd, ERR_ARGS, 0); break; }
        if (d[0] == 0) zr_set_patch_param(d[1], d[2], d[3], 0);
        else if (d[0] == 1) zr_set_global(d[2], d[3], 0);
        else error(seq, cmd, ERR_ARGS, 1);
        break;
    case CMD_TRANSPORT:
        if (len < 1) { error(seq, cmd, ERR_ARGS, 0); break; }
        zr_post(ZQ_CONTROL, d[0] ? ZE_PLAY : ZE_STOP, 0xff, 0, 0);
        break;
    case CMD_NOTE:
        if (len < 3 || d[0] >= ZR_TRACKS || d[1] > 127 || d[2] > 127) { error(seq, cmd, ERR_ARGS, 0); break; }
        zr_post(ZQ_CONTROL, d[2] ? ZE_NOTE_ON : ZE_NOTE_OFF, d[0], d[1], d[2]);
        break;
    case CMD_GET_STATUS: {
        msg_t *m = push(RSP_STATUS, seq);
        m->data[0] = zr_status.playing; m->data[1] = zr_proj.g[ZG_SCENE];
        for (unsigned t = 0; t < ZR_TRACKS; ++t) m->data[2 + t] = zr_status.step[t];
        m->data[6] = zr_status.voices; m->data[7] = zr_status.load; m->data[8] = zr_live.sel_track;
        m->data[9] = (uint8_t)zr_ui_page(); m->data[10] = (uint8_t)zr_store_state(); m->data[11] = zr_live.record;
        m->data[12] = zr_status.peak_master;
        m->data[13] = zr_live.degree % 7; m->data[14] = zr_status.pure_root; m->data[15] = zr_status.duet_active;
        m->data[16] = zr_status.capture_seq; m->data[17] = zr_status.capture_count;
        m->len = 18;
        break;
    }
    case CMD_GENERATE: {
        if (len < 1 || d[0] >= ZR_TRACKS) { error(seq, cmd, ERR_ARGS, 0); break; }
        zr_gen_params gp;
        zr_gen_from_globals(&gp, d[0]);
        static zr_pattern tmp;
        zr_generate(&tmp, &gp, d[0] == ZR_DRUM_TRACK);
        zr_copy(&zr_proj.pat[zr_proj.g[ZG_SCENE] & 7][d[0]], &tmp, sizeof tmp);
        zr_ui_touch();
        msg_t *m = push(RSP_GENERATED, seq);
        m->data[0] = d[0]; m->data[1] = zr_proj.g[ZG_SCENE] & 7; m->len = 2;
        break;
    }
    case CMD_SELECT:
        if (len < 2 || d[0] >= ZR_TRACKS || d[1] > ZR_SCENES) { error(seq, cmd, ERR_ARGS, 0); break; }
        zr_live.sel_track = d[0];
        if (d[1] < ZR_SCENES) {
            if (zr_status.playing) zr_live.scene_req = d[1];
            else zr_proj.g[ZG_SCENE] = d[1];
        }
        zr_ui_touch();
        break;
    case CMD_LIVE: /* op 0: chord degree 0..6; op 1: capture the last phrase of a track */
        if (len < 2 || d[0] > 1 || (d[0] == 0 && d[1] > 6) || (d[0] == 1 && d[1] >= ZR_TRACKS)) { error(seq, cmd, ERR_ARGS, 0); break; }
        if (d[0] == 0) zr_live.degree = d[1];
        else zr_post(ZQ_CONTROL, ZE_CAPTURE, d[1], 0, 0);
        zr_ui_touch();
        break;
    default: error(seq, cmd, ERR_COMMAND, 0); break;
    }
}

void zr_sysex_poll(uint32_t now_ms)
{
    now = now_ms;
    if (rx.active && now - rx.last_ms > 2000U) rx.active = 0;
    for (unsigned budget = 0; budget < 2; ++budget) {
        if (ring_r != ring_w) {
            msg_t *m = &ring[ring_r % RING];
            if (send(m->cmd, m->seq, m->data, m->len) != 0) return;
            ++ring_r;
        } else if (tx.active) {
            uint8_t d[PAYLOAD_MAX];
            unsigned count = tx.total - tx.off < CHUNK ? tx.total - tx.off : CHUNK;
            d[0] = tx.kind; d[1] = tx.a; d[2] = tx.b;
            d[3] = (uint8_t)tx.off; d[4] = (uint8_t)(tx.off >> 8);
            d[5] = (uint8_t)tx.total; d[6] = (uint8_t)(tx.total >> 8);
            zr_copy(d + 7, tx.buf + tx.off, count);
            if (send(RSP_BLOB, tx.seq, d, 7 + count) != 0) return;
            tx.off = (uint16_t)(tx.off + count);
            if (tx.off >= tx.total) tx.active = 0;
        } else return;
    }
}
