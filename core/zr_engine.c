/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA engine: event queues, voice allocation, sequencer clock, arpeggiator,
 * live recording and the block renderer. Everything below zr_post() runs in
 * the audio context. */
#include "zr_eng.h"

zr_project zr_proj;
zr_status_t zr_status;
zr_live_t zr_live;

enum { QSIZE = 64, MOSIZE = 32, OFFS = 8, HELD = 8 };
typedef struct { volatile uint16_t w, r; volatile uint32_t e[QSIZE]; } queue_t;
static queue_t queues[2];
static struct { volatile uint16_t w, r; volatile uint32_t e[MOSIZE]; } midi_out;
static volatile uint32_t patch_rev[ZR_TRACKS];
static volatile uint8_t hold_flag;

typedef struct {
    uint8_t n, phys, latched, pos;
    uint8_t note[HELD], vel[HELD];
    uint32_t t_q8;
} held_t;
typedef struct { uint8_t note; uint16_t left; } off_t;
typedef struct { uint8_t left; uint16_t interval, count, gate; zr_step st; } ratchet_t;

static struct {
    uint32_t rev_seen[ZR_TRACKS];
    int32_t bend_q8[ZR_TRACKS];
    uint8_t sustain[ZR_TRACKS], rec_skip[ZR_TRACKS], rec_step[ZR_TRACKS][HELD], rec_note[ZR_TRACKS][HELD];
    uint32_t rec_start[ZR_TRACKS][HELD];
    held_t held[ZR_TRACKS];
    off_t offs[ZR_TRACKS][OFFS];
    ratchet_t ratchet[ZR_TRACKS];
    uint32_t gstep, t_q8, cur_len, len_even, len_odd, base_len;
    uint32_t rng, age;
    uint8_t playing, tempo_seen, swing_seen, voice_limit, hot;
    uint32_t clk_q8;
} E;

static int32_t bus_mix[2 * ZR_BLOCK_FRAMES], bus_duck[2 * ZR_BLOCK_FRAMES];
static int32_t bus_dly[ZR_BLOCK_FRAMES], bus_rev[ZR_BLOCK_FRAMES];

static const uint8_t chord_iv[ZR_CHORD_COUNT][3] ZR_AUDIO_DATA = {
    {0, 0, 0}, {12, 0, 0}, {7, 0, 0}, {4, 7, 0}, {3, 7, 0}, {5, 7, 0}, {4, 7, 11}, {3, 7, 10}};
static const uint16_t arp_rate_q8[5] ZR_AUDIO_DATA = {512, 256, 128, 341, 171};

/* ---------------- any context ---------------- */
int zr_post(unsigned qi, unsigned type, unsigned track, unsigned a, unsigned b)
{
    queue_t *q = &queues[qi & 1];
    uint16_t w = q->w;
    if ((uint16_t)(w - q->r) >= QSIZE) { ++zr_status.dropped; return -1; }
    q->e[w & (QSIZE - 1)] = (type & 255) | ((track & 255) << 8) | ((a & 255) << 16) | ((b & 255) << 24);
    q->w = (uint16_t)(w + 1);
    return 0;
}
void zr_patch_touch(unsigned track) { if (track < ZR_TRACKS) ++patch_rev[track]; }
void zr_engine_hold(int on) { hold_flag = (uint8_t)(on != 0); }
unsigned zr_midi_out_pop(uint8_t m[3])
{
    uint16_t r = midi_out.r;
    if (r == midi_out.w) return 0;
    uint32_t e = midi_out.e[r & (MOSIZE - 1)];
    midi_out.r = (uint16_t)(r + 1);
    m[0] = (uint8_t)e; m[1] = (uint8_t)(e >> 8); m[2] = (uint8_t)(e >> 16);
    return m[0] >= 0xf8 ? 1 : (m[0] & 0xf0) == 0xc0 || (m[0] & 0xf0) == 0xd0 ? 2 : 3;
}
void zr_engine_init(void)
{
    zr_zero(&E, sizeof E);
    zr_zero(queues, sizeof queues);
    zr_zero(&midi_out, sizeof midi_out);
    zr_zero(zr_voices, sizeof zr_voices);
    zr_zero(zr_drums, sizeof zr_drums);
    zr_zero(&zr_status, sizeof zr_status);
    zr_zero(&zr_live, sizeof zr_live);
    zr_live.scene_req = 0xff;
    E.rng = 0x1234abcd;
    E.voice_limit = ZR_VOICES;
    E.tempo_seen = 0xff;
    for (unsigned t = 0; t < ZR_TRACKS; ++t) { patch_rev[t] = 1; E.rev_seen[t] = 0; }
    hold_flag = 0;
    zr_fx_init();
}
void zr_report_load(uint32_t used, uint32_t period)
{
    if (!period) return;
    uint32_t pct = (used * 100U) / period;
    zr_status.load = (uint8_t)(pct > 255 ? 255 : pct);
    if (pct > 88) {
        ++zr_status.overloads;
        if (++E.hot >= 4 && E.voice_limit > 4) { --E.voice_limit; E.hot = 0; }
    } else if (pct < 55) {
        E.hot = 0;
        if (E.voice_limit < ZR_VOICES && !(zr_status.blocks & 1023)) ++E.voice_limit;
    }
}

/* ---------------- audio context ---------------- */
ZR_INLINE void out_midi(unsigned status, unsigned a, unsigned b)
{
    if (!zr_proj.g[ZG_MIDI_OUT]) return;
    uint16_t w = midi_out.w;
    if ((uint16_t)(w - midi_out.r) >= MOSIZE) return;
    midi_out.e[w & (MOSIZE - 1)] = status | (a << 8) | (b << 16);
    midi_out.w = (uint16_t)(w + 1);
}
ZR_INLINE unsigned track_channel(unsigned track) { return track == ZR_DRUM_TRACK ? 9U : track; }

static const uint8_t gm_drum[ZR_DRUMS] ZR_AUDIO_DATA = {36, 38, 42, 46, 39, 45, 37, 56};
ZR_AUDIO_CODE static unsigned drum_of_note(unsigned note)
{
    for (unsigned d = 0; d < ZR_DRUMS; ++d) if (gm_drum[d] == note) return d;
    return note & 7;
}

ZR_AUDIO_CODE static void voice_release(zr_voice *v)
{
    v->state = 2;
    v->sustained = 0;
    for (unsigned i = 0; i < 4; ++i) v->stage[i] = 2;
    v->fstage = 2;
}

ZR_AUDIO_CODE static void synth_on(unsigned track, unsigned note, unsigned vel)
{
    const zr_tparams *tp = &zr_tp[track];
    zr_voice *v = 0;
    unsigned limit = E.voice_limit;
    int legato = 0;
    if (tp->mode != ZMODE_POLY) {
        for (unsigned i = 0; i < ZR_VOICES; ++i)
            if (zr_voices[i].state && zr_voices[i].track == track) { v = &zr_voices[i]; break; }
        if (v) legato = tp->mode == ZMODE_LEGATO && v->state == 1;
    } else {
        for (unsigned i = 0; i < ZR_VOICES; ++i)
            if (zr_voices[i].state && zr_voices[i].track == track && zr_voices[i].note == note) { v = &zr_voices[i]; break; }
    }
    if (!v)
        for (unsigned i = 0; i < limit; ++i) if (!zr_voices[i].state) { v = &zr_voices[i]; break; }
    if (!v) { /* steal: the oldest released voice, else the oldest voice */
        uint32_t best = 0;
        for (unsigned pass = 0; pass < 2 && !v; ++pass)
            for (unsigned i = 0; i < limit; ++i) {
                zr_voice *c = &zr_voices[i];
                if (pass == 0 && c->state != 2) continue;
                uint32_t age = E.age - c->age;
                if (!v || age > best) { v = c; best = age; }
            }
    }
    if (!v) return;
    if (v->state && v->track != track) v->state = 0; /* stolen across tracks: restart cleanly */
    v->track = (uint8_t)track;
    v->age = ++E.age;
    v->pan_off = (int16_t)((((v - zr_voices) & 1) ? 1 : -1) * (int)(tp->spread / 3));
    zr_voice_start(v, tp, note, vel, legato);
}
ZR_AUDIO_CODE static void synth_off(unsigned track, unsigned note)
{
    for (unsigned i = 0; i < ZR_VOICES; ++i) {
        zr_voice *v = &zr_voices[i];
        if (v->state == 1 && v->track == track && v->note == note) {
            if (E.sustain[track]) v->sustained = 1;
            else voice_release(v);
        }
    }
}
/* Sound a note (sequencer, arp or live). */
ZR_AUDIO_CODE static void note_on(unsigned track, unsigned note, unsigned vel, int echo)
{
    if (track == ZR_DRUM_TRACK) {
        unsigned d = note < ZR_DRUMS ? note : drum_of_note(note);
        zr_drum_trigger(d, vel);
        if (d == ZDR_KICK && zr_proj.g[ZG_PUMP]) zr_fx_pump_trigger();
        if (echo) out_midi(0x99, gm_drum[d], vel);
        return;
    }
    synth_on(track, note & 127, vel);
    if (echo) out_midi(0x90 | track_channel(track), note & 127, vel);
}
ZR_AUDIO_CODE static void note_off(unsigned track, unsigned note, int echo)
{
    if (track == ZR_DRUM_TRACK) {
        if (echo) out_midi(0x89, gm_drum[note < ZR_DRUMS ? note : drum_of_note(note)], 0);
        return;
    }
    synth_off(track, note & 127);
    if (echo) out_midi(0x80 | track_channel(track), note & 127, 0);
}
ZR_AUDIO_CODE static void schedule_off(unsigned track, unsigned note, unsigned chunks)
{
    off_t *o = E.offs[track], *slot = 0;
    if (!chunks) chunks = 1;
    if (chunks > 65535) chunks = 65535;
    for (unsigned i = 0; i < OFFS; ++i) if (o[i].left && o[i].note == note) { slot = &o[i]; break; }
    if (!slot)
        for (unsigned i = 0; i < OFFS; ++i) if (!o[i].left) { slot = &o[i]; break; }
    if (!slot) { /* table full: end the note that would end first */
        slot = &o[0];
        for (unsigned i = 1; i < OFFS; ++i) if (o[i].left < slot->left) slot = &o[i];
        note_off(track, slot->note, 1);
    }
    slot->note = (uint8_t)note;
    slot->left = (uint16_t)chunks;
}
ZR_AUDIO_CODE static void all_off(unsigned track)
{
    for (unsigned i = 0; i < ZR_VOICES; ++i)
        if (zr_voices[i].state == 1 && zr_voices[i].track == track) voice_release(&zr_voices[i]);
    for (unsigned i = 0; i < OFFS; ++i) {
        if (E.offs[track][i].left) out_midi(0x80 | track_channel(track), E.offs[track][i].note, 0);
        E.offs[track][i].left = 0;
    }
    E.ratchet[track].left = 0;
    E.held[track].n = E.held[track].phys = E.held[track].latched = 0;
    E.sustain[track] = 0;
}

/* ---- sequencer ---- */
ZR_AUDIO_CODE static void fire(unsigned track, const zr_step *st, uint32_t gate_chunks)
{
    unsigned vel = st->vel ? st->vel : 100;
    if (track == ZR_DRUM_TRACK) {
        for (unsigned d = 0; d < ZR_DRUMS; ++d) if (st->note & (1U << d)) note_on(track, d, vel, 1);
        return;
    }
    unsigned chord = (st->fx >> 3) & 7;
    for (unsigned k = 0; k < 4; ++k) {
        unsigned n = st->note + (k ? chord_iv[chord][k - 1] : 0);
        if (k && !chord_iv[chord][k - 1]) break;
        if (n > 127) continue;
        note_on(track, n, vel, 1);
        schedule_off(track, n, gate_chunks);
    }
}
ZR_AUDIO_CODE static void trigger_step(unsigned track, unsigned index, const zr_step *st, uint32_t len_q8)
{
    if (!st->note) return;
    if (E.rec_skip[track] == index + 1) { E.rec_skip[track] = 0; return; }
    unsigned prob = st->fx & 7;
    if (prob && (zr_rand(&E.rng) & 7) < prob) return;
    unsigned ratch = ((st->gate >> 4) & 3) + 1;
    uint32_t step_chunks = len_q8 >> 12; /* Q8 samples -> 16-sample chunks */
    uint32_t gate = (step_chunks * zr_gate_eighths[st->gate & 15]) >> 3;
    if (ratch > 1) {
        ratchet_t *r = &E.ratchet[track];
        r->left = (uint8_t)(ratch - 1);
        r->interval = r->count = (uint16_t)(step_chunks / ratch ? step_chunks / ratch : 1);
        r->gate = (uint16_t)((r->interval * 3) >> 2 ? (r->interval * 3) >> 2 : 1);
        r->st = *st;
        gate = r->gate;
    }
    fire(track, st, gate);
}
ZR_AUDIO_CODE static void step_advance(void)
{
    uint8_t *g = zr_proj.g;
    ++E.gstep;
    if (zr_live.scene_req != 0xff && !(E.gstep & 15)) {
        g[ZG_SCENE] = zr_live.scene_req & 7;
        zr_live.scene_req = 0xff;
    }
    unsigned scene = g[ZG_SCENE] & 7;
    zr_status.scene = (uint8_t)scene;
    E.cur_len = (E.gstep & 1) ? E.len_odd : E.len_even;
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        const zr_pattern *p = &zr_proj.pat[scene][t];
        unsigned len = p->length && p->length <= ZR_STEPS ? p->length : 16;
        unsigned s = E.gstep % len;
        zr_status.step[t] = (uint8_t)s;
        trigger_step(t, s, &p->steps[s], E.cur_len);
    }
}
ZR_AUDIO_CODE static void seq_tick(void)
{
    for (unsigned t = 0; t < ZR_TRACKS; ++t) { /* note-offs and ratchets run even when stopped */
        off_t *o = E.offs[t];
        for (unsigned i = 0; i < OFFS; ++i)
            if (o[i].left && !--o[i].left) note_off(t, o[i].note, 1);
        ratchet_t *r = &E.ratchet[t];
        if (r->left && !--r->count) {
            --r->left;
            r->count = r->interval;
            fire(t, &r->st, r->gate);
        }
    }
    if (!E.playing) return;
    E.clk_q8 += (ZR_CHUNK << 8) * 6; /* MIDI clock: 6 per 16th */
    if (E.clk_q8 >= E.base_len) { E.clk_q8 -= E.base_len; out_midi(0xf8, 0, 0); }
    E.t_q8 += ZR_CHUNK << 8;
    while (E.t_q8 >= E.cur_len) {
        E.t_q8 -= E.cur_len;
        step_advance();
    }
}

/* ---- arpeggiator ---- */
ZR_AUDIO_CODE static unsigned held_sorted(const held_t *h, unsigned rank)
{
    /* rank-th smallest held note (n <= 8) */
    for (unsigned i = 0; i < h->n; ++i) {
        unsigned below = 0;
        for (unsigned j = 0; j < h->n; ++j)
            if (h->note[j] < h->note[i] || (h->note[j] == h->note[i] && j < i)) ++below;
        if (below == rank) return i;
    }
    return 0;
}
ZR_AUDIO_CODE static void arp_tick(void)
{
    const uint8_t *g = zr_proj.g;
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        held_t *h = &E.held[t];
        if (!g[ZG_TRACK(t, ZT_ARP_ON)] || !h->n) continue;
        uint32_t period = (E.base_len * arp_rate_q8[g[ZG_TRACK(t, ZT_ARP_RATE)] % 5]) >> 8;
        if (period < (ZR_CHUNK << 8)) period = ZR_CHUNK << 8;
        h->t_q8 += ZR_CHUNK << 8;
        if (h->t_q8 < period) continue;
        h->t_q8 -= period;
        if (h->t_q8 >= period) h->t_q8 = 0;
        unsigned octs = (g[ZG_TRACK(t, ZT_ARP_OCT)] % 3) + 1, total = h->n * octs, k;
        unsigned mode = g[ZG_TRACK(t, ZT_ARP_MODE)];
        if (t == ZR_DRUM_TRACK) { octs = 1; total = h->n; }
        switch (mode) {
        case ZARP_DOWN: k = total - 1 - (h->pos % total); break;
        case ZARP_UPDOWN: {
            unsigned span = total > 1 ? 2 * total - 2 : 1, x = h->pos % span;
            k = x < total ? x : span - x;
            break;
        }
        case ZARP_RANDOM: k = zr_rand(&E.rng) % total; break;
        default: k = h->pos % total; break;
        }
        ++h->pos;
        unsigned idx = mode == ZARP_ORDER ? k % h->n : held_sorted(h, k % h->n);
        unsigned note = h->note[idx] + 12 * (k / h->n);
        if (note > 127) note = 127;
        note_on(t, note, h->vel[idx], 1);
        if (t != ZR_DRUM_TRACK)
            schedule_off(t, note, ((period >> 12) * (g[ZG_TRACK(t, ZT_ARP_GATE)] & 127)) / 127);
    }
}

/* ---- live input ---- */
ZR_AUDIO_CODE static uint32_t rec_pos8(void)
{
    uint32_t frac = E.cur_len ? (E.t_q8 << 3) / E.cur_len : 0;
    return E.gstep * 8 + (frac > 7 ? 7 : frac);
}
ZR_AUDIO_CODE static void record_on(unsigned track, unsigned note, unsigned vel)
{
    zr_pattern *p = &zr_proj.pat[zr_proj.g[ZG_SCENE] & 7][track];
    unsigned len = p->length && p->length <= ZR_STEPS ? p->length : 16, s;
    if (E.playing) {
        uint32_t gs = E.gstep;
        if (E.cur_len && E.t_q8 * 2 >= E.cur_len) { ++gs; E.rec_skip[track] = (uint8_t)(gs % len + 1); }
        s = gs % len;
    } else {
        s = zr_live.rec_cursor % len;
        zr_live.rec_cursor = (uint8_t)((s + 1) % len);
    }
    zr_step *st = &p->steps[s];
    if (track == ZR_DRUM_TRACK) {
        unsigned d = note < ZR_DRUMS ? note : drum_of_note(note);
        st->note |= (uint8_t)(1U << d);
        if (vel > st->vel) st->vel = (uint8_t)vel;
        return;
    }
    st->note = (uint8_t)note;
    st->vel = (uint8_t)vel;
    st->gate = 3;
    st->fx = 0;
    for (unsigned i = 0; i < HELD; ++i)
        if (!E.rec_note[track][i]) {
            E.rec_note[track][i] = (uint8_t)note;
            E.rec_step[track][i] = (uint8_t)s;
            E.rec_start[track][i] = rec_pos8();
            break;
        }
}
ZR_AUDIO_CODE static void record_off(unsigned track, unsigned note)
{
    for (unsigned i = 0; i < HELD; ++i) {
        if (E.rec_note[track][i] != note) continue;
        E.rec_note[track][i] = 0;
        if (!E.playing || track == ZR_DRUM_TRACK) continue;
        zr_step *st = &zr_proj.pat[zr_proj.g[ZG_SCENE] & 7][track].steps[E.rec_step[track][i] % ZR_STEPS];
        if (st->note != note) continue;
        uint32_t dur = rec_pos8() - E.rec_start[track][i];
        unsigned gi = 0;
        while (gi < 15 && zr_gate_eighths[gi] < dur) ++gi;
        st->gate = (uint8_t)((st->gate & 0xf0) | gi);
    }
}
ZR_AUDIO_CODE static void live_on(unsigned track, unsigned note, unsigned vel, int local)
{
    const uint8_t *g = zr_proj.g;
    if (track != ZR_DRUM_TRACK && g[ZG_SCALE_LOCK]) note = zr_scale_snap(note, g[ZG_ROOT], g[ZG_SCALE]);
    held_t *h = &E.held[track];
    int arp = g[ZG_TRACK(track, ZT_ARP_ON)] != 0;
    if (h->latched && !h->phys) { h->n = 0; h->latched = 0; }
    ++h->phys;
    unsigned found = 0;
    for (unsigned i = 0; i < h->n; ++i) if (h->note[i] == note) found = 1;
    if (!found) {
        if (h->n == HELD) { /* drop the oldest */
            for (unsigned i = 1; i < HELD; ++i) { h->note[i - 1] = h->note[i]; h->vel[i - 1] = h->vel[i]; }
            --h->n;
        }
        if (!h->n) { h->pos = 0; h->t_q8 = 0x7fffffffu; }
        h->note[h->n] = (uint8_t)note;
        h->vel[h->n] = (uint8_t)vel;
        ++h->n;
    }
    if (zr_live.record) record_on(track, note, vel);
    if (arp) return;
    note_on(track, note, vel, local);
}
ZR_AUDIO_CODE static void live_off(unsigned track, unsigned note, int local)
{
    const uint8_t *g = zr_proj.g;
    if (track != ZR_DRUM_TRACK && g[ZG_SCALE_LOCK]) note = zr_scale_snap(note, g[ZG_ROOT], g[ZG_SCALE]);
    held_t *h = &E.held[track];
    int arp = g[ZG_TRACK(track, ZT_ARP_ON)] != 0;
    if (h->phys) --h->phys;
    record_off(track, note);
    if (arp && g[ZG_TRACK(track, ZT_ARP_HOLD)]) {
        if (!h->phys) h->latched = 1;
        return;
    }
    for (unsigned i = 0; i < h->n; ++i)
        if (h->note[i] == note) {
            for (unsigned j = i + 1; j < h->n; ++j) { h->note[j - 1] = h->note[j]; h->vel[j - 1] = h->vel[j]; }
            --h->n;
            break;
        }
    if (arp) return;
    if (track != ZR_DRUM_TRACK && zr_tp[track].mode != ZMODE_POLY && h->n) {
        /* mono: fall back to the most recent key still held */
        int sounding = 0;
        for (unsigned i = 0; i < ZR_VOICES; ++i)
            if (zr_voices[i].state == 1 && zr_voices[i].track == track && zr_voices[i].note == note) sounding = 1;
        if (sounding) {
            for (unsigned i = 0; i < ZR_VOICES; ++i)
                if (zr_voices[i].state == 1 && zr_voices[i].track == track)
                    zr_voice_start(&zr_voices[i], &zr_tp[track], h->note[h->n - 1], h->vel[h->n - 1], 1);
            if (local) out_midi(0x80 | track_channel(track), note, 0);
            return;
        }
    }
    note_off(track, note, local);
}

ZR_AUDIO_CODE static void drain(unsigned qi)
{
    queue_t *q = &queues[qi];
    while (q->r != q->w) {
        uint32_t e = q->e[q->r & (QSIZE - 1)];
        q->r = (uint16_t)(q->r + 1);
        unsigned type = e & 255, track = (e >> 8) & 255, a = (e >> 16) & 255, b = e >> 24;
        int local = qi == ZQ_CONTROL;
        if (track >= ZR_TRACKS && type != ZE_PLAY && type != ZE_STOP && type != ZE_PANIC && type != ZE_ALL_OFF) continue;
        switch (type) {
        case ZE_NOTE_ON: if (b) live_on(track, a, b, local); else live_off(track, a, local); break;
        case ZE_NOTE_OFF: live_off(track, a, local); break;
        case ZE_ALL_OFF:
            for (unsigned t = 0; t < ZR_TRACKS; ++t) if (track >= ZR_TRACKS || t == track) all_off(t);
            break;
        case ZE_PLAY:
            if (!E.playing) {
                E.playing = 1;
                E.gstep = 0xffffffffu;
                E.t_q8 = E.cur_len = 0;
                E.clk_q8 = 0;
                out_midi(0xfa, 0, 0);
            }
            break;
        case ZE_STOP:
            if (E.playing) out_midi(0xfc, 0, 0);
            E.playing = 0;
            for (unsigned t = 0; t < ZR_TRACKS; ++t) {
                for (unsigned i = 0; i < OFFS; ++i)
                    if (E.offs[t][i].left) { E.offs[t][i].left = 0; note_off(t, E.offs[t][i].note, 1); }
                E.ratchet[t].left = 0;
                E.rec_skip[t] = 0;
            }
            break;
        case ZE_BEND:
            E.bend_q8[track] = (((int32_t)(a | (b << 7)) - 8192) * (int32_t)zr_tp[track < 3 ? track : 0].bend_range) >> 5;
            break;
        case ZE_SUSTAIN:
            E.sustain[track] = (uint8_t)(a != 0);
            if (!a)
                for (unsigned i = 0; i < ZR_VOICES; ++i)
                    if (zr_voices[i].state == 1 && zr_voices[i].track == track && zr_voices[i].sustained)
                        voice_release(&zr_voices[i]);
            break;
        case ZE_PANIC:
            for (unsigned t = 0; t < ZR_TRACKS; ++t) all_off(t);
            for (unsigned i = 0; i < ZR_VOICES; ++i) zr_voices[i].state = 0;
            for (unsigned i = 0; i < ZR_DRUMS; ++i) zr_drums[i].on = 0;
            break;
        default: break;
        }
    }
}

ZR_INLINE unsigned peak_of(const int32_t *buf, unsigned words)
{
    int32_t m = 0;
    for (unsigned i = 0; i < words; ++i) { int32_t a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > m) m = a; }
    m >>= 8;
    return (unsigned)(m > 127 ? 127 : m);
}

ZR_AUDIO_CODE void zr_audio(int32_t *out, unsigned frames)
{
    if (frames != ZR_BLOCK_FRAMES || !out) return;
    const uint8_t *g = zr_proj.g;
    ++zr_status.blocks;
    if (hold_flag) {
        for (unsigned i = 0; i < ZR_VOICES; ++i) zr_voices[i].state = 0;
        for (unsigned i = 0; i < 2 * ZR_BLOCK_FRAMES; ++i) out[i] = 0;
        return;
    }
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        uint32_t rev = patch_rev[t];
        if (rev == E.rev_seen[t]) continue;
        E.rev_seen[t] = rev;
        if (t == ZR_DRUM_TRACK) zr_drums_derive();
        else zr_synth_derive(t);
    }
    if (g[ZG_TEMPO] != E.tempo_seen || g[ZG_SWING] != E.swing_seen) {
        E.tempo_seen = g[ZG_TEMPO];
        E.swing_seen = g[ZG_SWING];
        unsigned bpm = 40U + (g[ZG_TEMPO] > 200 ? 200 : g[ZG_TEMPO]);
        unsigned swing = 50U + (g[ZG_SWING] > 25 ? 25 : g[ZG_SWING]);
        E.base_len = (ZR_SAMPLE_RATE * 60U * 64U) / bpm; /* samples per 16th, Q8 */
        E.len_even = (uint32_t)(((uint64_t)E.base_len * 2 * swing) / 100);
        E.len_odd = 2 * E.base_len - E.len_even;
        if (E.playing) E.cur_len = (E.gstep & 1) ? E.len_odd : E.len_even;
        zr_fx_set_tempo(E.base_len);
    }
    for (unsigned i = 0; i < 2 * ZR_BLOCK_FRAMES; ++i) bus_mix[i] = bus_duck[i] = 0;
    for (unsigned i = 0; i < ZR_BLOCK_FRAMES; ++i) bus_dly[i] = bus_rev[i] = 0;

    unsigned track_peak[ZR_TRACKS] = {0, 0, 0, 0}, voices = 0, tv[ZR_TRACKS] = {0, 0, 0, 0};
    for (unsigned c = 0; c < ZR_BLOCK_FRAMES / ZR_CHUNK; ++c) {
        drain(ZQ_CONTROL);
        drain(ZQ_MIDI);
        seq_tick();
        arp_tick();
        int32_t *dl = bus_dly + c * ZR_CHUNK, *rv = bus_rev + c * ZR_CHUNK;
        for (unsigned t = 0; t < ZR_TRACKS; ++t) {
            int32_t tb[2 * ZR_CHUNK], td[ZR_CHUNK], tr[ZR_CHUNK];
            unsigned any = 0;
            for (unsigned i = 0; i < 2 * ZR_CHUNK; ++i) tb[i] = 0;
            for (unsigned i = 0; i < ZR_CHUNK; ++i) td[i] = tr[i] = 0;
            if (t == ZR_DRUM_TRACK) any = zr_drums_render(tb, td, tr);
            else {
                zr_lfo_free[t] += zr_tp[t].lfo_inc;
                for (unsigned i = 0; i < ZR_VOICES; ++i) {
                    zr_voice *v = &zr_voices[i];
                    if (!v->state || v->track != t) continue;
                    ++any;
                    zr_voice_render(v, &zr_tp[t], E.bend_q8[t], tb, td, tr);
                }
            }
            if (!any) continue;
            if (c == 0) { tv[t] = any; if (t != ZR_DRUM_TRACK) voices += any; }
            const int32_t vol = g[ZG_TRACK(t, ZT_MUTE)] ? 0 : zr_vol[g[ZG_TRACK(t, ZT_VOLUME)] & 127];
            int32_t *dst = (t == ZR_DRUM_TRACK ? bus_mix : bus_duck) + c * 2 * ZR_CHUNK;
            for (unsigned i = 0; i < 2 * ZR_CHUNK; ++i) {
                int32_t x = tb[i];
                if (x > (1 << 20)) x = 1 << 20; else if (x < -(1 << 20)) x = -(1 << 20);
                x = (int32_t)(((int64_t)x * vol) >> 15);
                tb[i] = x;
                dst[i] += x;
            }
            for (unsigned i = 0; i < ZR_CHUNK; ++i) {
                dl[i] += (int32_t)(((int64_t)td[i] * vol) >> 15);
                rv[i] += (int32_t)(((int64_t)tr[i] * vol) >> 15);
            }
            unsigned pk = peak_of(tb, 2 * ZR_CHUNK);
            if (pk > track_peak[t]) track_peak[t] = pk;
        }
    }
    zr_fx_process(bus_mix, bus_duck, bus_dly, bus_rev, ZR_BLOCK_FRAMES, out);
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        unsigned old = zr_status.peak[t];
        zr_status.peak[t] = (uint8_t)(track_peak[t] > old ? track_peak[t] : old > 2 ? old - 3 : 0);
        zr_status.track_voices[t] = (uint8_t)tv[t];
    }
    zr_status.voices = (uint8_t)voices;
    zr_status.playing = E.playing;
    zr_status.recording = zr_live.record;
}

/* ---- MIDI input (routing context): notes become events, controllers edit
 * shared bytes directly ---- */
static volatile uint8_t pending_program[ZR_TRACKS];
unsigned zr_take_program(unsigned track)
{
    unsigned p = pending_program[track];
    pending_program[track] = 0;
    return p;
}
void zr_midi_in(const uint8_t *m, unsigned n)
{
    if (!m || !n) return;
    unsigned status = m[0];
    if (status >= 0xf0) return; /* clock/transport from outside: not supported yet */
    unsigned ch = status & 15, track;
    if (ch < ZR_TRACKS) track = ch;
    else if (ch == 9) track = ZR_DRUM_TRACK;
    else if (ch == 15) track = zr_live.sel_track % ZR_TRACKS;
    else return;
    unsigned a = n > 1 ? m[1] & 127 : 0, b = n > 2 ? m[2] & 127 : 0;
    switch (status & 0xf0) {
    case 0x90: if (n >= 3) zr_post(ZQ_MIDI, b ? ZE_NOTE_ON : ZE_NOTE_OFF, track, a, b); break;
    case 0x80: if (n >= 3) zr_post(ZQ_MIDI, ZE_NOTE_OFF, track, a, 0); break;
    case 0xe0: if (n >= 3) zr_post(ZQ_MIDI, ZE_BEND, track, a, b); break;
    case 0xc0: if (n >= 2) pending_program[track] = (uint8_t)(a + 1); break;
    case 0xb0: {
        if (n < 3) break;
        uint8_t *p = zr_proj.patch[track].p;
        int synth = track != ZR_DRUM_TRACK;
        if (a == 64) zr_post(ZQ_MIDI, ZE_SUSTAIN, track, b >= 64, 0);
        else if (a == 120 || a == 123) zr_post(ZQ_MIDI, ZE_ALL_OFF, track, 0, 0);
        else if (a == 7) zr_proj.g[ZG_TRACK(track, ZT_VOLUME)] = (uint8_t)b;
        else if (!synth) break;
        else if (a == 1) p[ZP_M1_VAL] = (uint8_t)b;
        else if (a >= 16 && a <= 19) p[ZP_M1_VAL + (a - 16)] = (uint8_t)b;
        else if (a == 74) p[ZP_CUTOFF] = (uint8_t)b;
        else if (a == 71) p[ZP_RESO] = (uint8_t)b;
        else if (a == 10) p[ZP_PAN] = (uint8_t)(b ? b : 1);
        else break;
        zr_patch_touch(track);
        break;
    }
    default: break;
    }
}
