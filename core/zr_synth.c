/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA sound kernels: the 4-operator phase-modulation voice with shaped
 * operator waves, a resonant state-variable filter and LFO; and the
 * synthesized drum kit. Integer-only; runs in the audio context. */
#include "zr_eng.h"

zr_tparams zr_tp[3];
zr_kparams zr_kp;
zr_voice zr_voices[ZR_VOICES];
zr_drum zr_drums[ZR_DRUMS];
uint32_t zr_lfo_free[3];

static const uint8_t algo_mods[8][4] ZR_AUDIO_DATA = {
    {0x2, 0x4, 0x8, 0}, {0x2, 0xc, 0, 0}, {0xa, 0x4, 0, 0}, {0x6, 0, 0x8, 0},
    {0x2, 0, 0x8, 0},   {0x8, 0x8, 0x8, 0}, {0, 0, 0x8, 0}, {0, 0, 0, 0}};
static const uint8_t algo_car[8] ZR_AUDIO_DATA = {0x1, 0x1, 0x1, 0x1, 0x5, 0x7, 0x7, 0xf};
static const uint16_t car_norm[4] ZR_AUDIO_DATA = {32767, 24576, 20317, 18022};

ZR_INLINE int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
ZR_INLINE int32_t mul24(int32_t a, uint32_t k) { return (int32_t)(((int64_t)a * (int64_t)k) >> 24); }

ZR_AUDIO_CODE uint32_t zr_pitch_inc(int32_t pitch_q8)
{
    if (pitch_q8 < 0) pitch_q8 = 0;
    if (pitch_q8 > (127 << 8) + 255) pitch_q8 = (127 << 8) + 255;
    return (uint32_t)(((uint64_t)zr_note_inc[pitch_q8 >> 8] * zr_frac[pitch_q8 & 255]) >> 16);
}

/* ---- derive audio-rate parameters from the patch bytes ---- */
ZR_AUDIO_CODE void zr_synth_derive(unsigned track)
{
    const uint8_t *p = zr_proj.patch[track].p;
    zr_tparams *t = &zr_tp[track];
    int mod[ZMD_COUNT];
    for (unsigned i = 0; i < ZMD_COUNT; ++i) mod[i] = 0;
    for (unsigned m = 0; m < 4; ++m) {
        unsigned dest = p[ZP_M1_DEST + 2 * m];
        if (dest && dest < ZMD_COUNT)
            mod[dest] += ((int)p[ZP_M1_VAL + m] * ((int)p[ZP_M1_AMT + 2 * m] - 64)) / 63;
    }
    t->algo = p[ZP_ALGO] & 7;
    t->mode = p[ZP_MODE];
    t->bend_range = p[ZP_BEND];
    t->spread = p[ZP_SPREAD];
    unsigned carriers = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *o = p + ZP_OP_BASE + i * ZO_COUNT;
        unsigned car = (algo_car[t->algo] >> i) & 1U;
        t->carrier[i] = (uint8_t)car;
        t->kc[i] = car ? -1 : 0;
        carriers += car;
        for (unsigned s = 0; s < 4; ++s) t->km[i][s] = ((algo_mods[t->algo][i] >> s) & 1U) ? -1 : 0;
        t->wave[i] = o[ZO_WAVE] & 7;
        t->vel_sens[i] = o[ZO_VEL];
        t->ksc[i] = o[ZO_KSC];
        t->level_idx[i] = (int16_t)(o[ZO_LEVEL] + (car || !o[ZO_LEVEL] ? 0 : mod[ZMD_FM] / 2));
        t->ratio_q12[i] = (zr_ratio[o[ZO_RATIO] > 16 ? 16 : o[ZO_RATIO]] * (128U + o[ZO_FINE])) >> 7;
        t->detune[i] = (int16_t)(((int)o[ZO_DETUNE] - 7) * 3);
        t->ka[i] = zr_env_rate[clampi(o[ZO_ATK] + mod[ZMD_ATTACK], 0, 127)];
        t->kd[i] = zr_env_rate[clampi(o[ZO_DEC] + mod[ZMD_DECAY], 0, 127)];
        t->kr[i] = zr_env_rate[clampi(o[ZO_REL] + mod[ZMD_RELEASE], 0, 127)];
        t->sus[i] = (int32_t)zr_vol[o[ZO_SUS] & 127] << 9;
    }
    t->fb = clampi(p[ZP_FEEDBACK] + mod[ZMD_FEEDBACK], 0, 127);
    t->transpose_q8 = (((int)p[ZP_TRANSPOSE] - 64) * 256) + (mod[ZMD_PITCH] * 3072) / 127;
    t->glide_k = p[ZP_GLIDE] ? zr_env_rate[22 + (p[ZP_GLIDE] * 55) / 127] : 0;
    t->level_q15 = zr_vol[p[ZP_LEVEL] & 127];
    t->norm_q15 = car_norm[carriers ? carriers - 1 : 0];
    t->flt_type = p[ZP_FLT_TYPE] & 3;
    t->cutoff_q8 = (p[ZP_CUTOFF] + mod[ZMD_CUTOFF]) * 256;
    t->damp_q14 = 23170 - clampi(p[ZP_RESO] + mod[ZMD_RESO], 0, 127) * 176;
    t->flt_env = (int16_t)(clampi(((int)p[ZP_FLT_ENV] - 64) * 2 + mod[ZMD_FLT_ENV], -126, 126));
    t->flt_key = p[ZP_FLT_KEY];
    t->flt_vel = p[ZP_FLT_VEL];
    t->fka = zr_env_rate[p[ZP_FA] & 127];
    t->fkd = zr_env_rate[p[ZP_FD] & 127];
    t->fkr = zr_env_rate[p[ZP_FR] & 127];
    t->fsus = (int32_t)zr_vol[p[ZP_FS] & 127] << 9;
    t->lfo_shape = p[ZP_LFO_SHAPE];
    t->lfo_dest = p[ZP_LFO_DEST];
    t->lfo_sync = p[ZP_LFO_SYNC];
    t->lfo_inc = zr_lfo_rate[clampi(p[ZP_LFO_RATE] + mod[ZMD_LFO_RATE], 0, 127)];
    t->lfo_depth = (uint16_t)clampi(p[ZP_LFO_DEPTH] + mod[ZMD_LFO_DEPTH], 0, 127);
    t->lfo_fade_k = p[ZP_LFO_FADE] ? zr_env_rate[30 + (p[ZP_LFO_FADE] * 60) / 127] : 0;
    t->pan = (uint16_t)clampi(p[ZP_PAN], 1, 127);
    t->dly_send = zr_vol[clampi(p[ZP_DLY_SEND] + mod[ZMD_DLY_SEND], 0, 127)];
    t->rev_send = zr_vol[clampi(p[ZP_REV_SEND] + mod[ZMD_REV_SEND], 0, 127)];
}

/* ---- operator waves ---- */
ZR_INLINE int32_t blep(uint32_t pos, uint32_t inc)
{
    /* pos < inc: just after the edge. Returns the correction in Q15 (-32768..0). */
    int32_t t = (int32_t)(pos / (inc >> 15));
    if (t > 32767) t = 32767;
    return (2 * t - ((t * t) >> 15)) - 32768;
}
ZR_INLINE int32_t wave(unsigned w, uint32_t ph, uint32_t inc, uint32_t *noise)
{
    if (!w) return zr_sin[ph >> 20];
    switch (w) {
    case ZW_TRI: {
        int32_t a = (int32_t)(ph + 0x40000000u);
        if (a < 0) a = -a - 1;
        a = (a >> 15) - 32768;
        return a > 32767 ? 32767 : a;
    }
    case ZW_SAW: {
        int32_t s = 32767 - (int32_t)(ph >> 16);
        if (inc >= 0x10000u) {
            if (ph < inc) s += blep(ph, inc);
            else if (ph > 0u - inc) s -= blep(0u - ph, inc);
        }
        return s;
    }
    case ZW_SQUARE: {
        int32_t s = ph < 0x80000000u ? 32767 : -32767;
        if (inc >= 0x10000u) {
            uint32_t h = ph - 0x80000000u;
            if (ph < inc) s += blep(ph, inc);
            else if (ph > 0u - inc) s -= blep(0u - ph, inc);
            else if (h < inc) s -= blep(h, inc);
            else if (h > 0u - inc) s += blep(0u - h, inc);
        }
        return s;
    }
    case ZW_HALF: return (ph < 0x80000000u ? zr_sin[ph >> 20] : 0) - 10430;
    case ZW_DOUBLE: return ph < 0x80000000u ? zr_sin[(ph >> 19) & 4095] : 0;
    case ZW_FOLD: return zr_sin[((zr_sin[ph >> 20] * 3) >> 5) & 4095];
    default: {
        uint32_t n = *noise;
        n ^= n << 13; n ^= n >> 17; n ^= n << 5;
        *noise = n;
        return (int16_t)n;
    }
    }
}

ZR_INLINE void env_tick(int32_t *env, uint8_t *stage, uint32_t ka, uint32_t kd, uint32_t kr, int32_t sus)
{
    int32_t e = *env;
    if (*stage == 0) {
        e += mul24(ZR_ENV_ONE + (ZR_ENV_ONE >> 2) - e, ka) + 1;
        if (e >= ZR_ENV_ONE) { e = ZR_ENV_ONE; *stage = 1; }
    } else if (*stage == 1) {
        int32_t d = mul24(sus - e, kd);
        if (!d && e != sus) d = e > sus ? -1 : 1;
        e += d;
    } else {
        e -= mul24(e, kr) + 1;
        if (e < (ZR_ENV_ONE >> 13)) e = 0;
    }
    *env = e;
}

ZR_AUDIO_CODE void zr_voice_start(zr_voice *v, const zr_tparams *tp, unsigned note, unsigned vel, int legato)
{
    int fresh = v->state == 0;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->target_q8 = (int32_t)note << 8;
    if (fresh || !tp->glide_k) v->pitch_q8 = v->target_q8;
    if (fresh) {
        for (unsigned i = 0; i < 4; ++i) { v->phase[i] = 0; v->env[i] = 0; v->gain[i] = 0; }
        v->fb1 = v->fb2 = 0;
        v->lp = v->bp = 0;
        v->fenv = 0;
        v->amp_l = v->amp_r = 0;
        v->noise = 0x9e3779b9u ^ (note * 2654435761u);
        v->fresh = 1;
    }
    if (!legato || fresh) {
        for (unsigned i = 0; i < 4; ++i) v->stage[i] = 0;
        v->fstage = 0;
        v->lfo_phase = tp->lfo_sync ? 0 : zr_lfo_free[v->track];
        v->lfo_fade = tp->lfo_fade_k ? 0 : ZR_ENV_ONE;
    }
    v->sustained = 0;
    v->state = 1;
}

ZR_AUDIO_CODE int zr_voice_render(zr_voice *v, const zr_tparams *tp, int32_t bend_q8, int32_t *bus, int32_t *dly,
                                  int32_t *rev)
{
    int32_t buf[ZR_CHUNK];
    int32_t lfo = 0;
    unsigned dest = tp->lfo_depth ? tp->lfo_dest : 0;
    if (dest) {
        uint32_t ph = v->lfo_phase;
        int32_t raw;
        switch (tp->lfo_shape) {
        case ZLFO_SINE: raw = zr_sin[ph >> 20]; break;
        case ZLFO_SAW: raw = 32767 - (int32_t)(ph >> 16); break;
        case ZLFO_SQUARE: raw = ph < 0x80000000u ? 32767 : -32767; break;
        case ZLFO_SH:
            if (ph < tp->lfo_inc || !v->lfo_sh) {
                uint32_t n = v->noise;
                n ^= n << 13; n ^= n >> 17; n ^= n << 5;
                v->noise = n;
                v->lfo_sh = (int16_t)n | 1;
            }
            raw = v->lfo_sh;
            break;
        default: {
            int32_t a = (int32_t)(ph + 0x40000000u);
            if (a < 0) a = -a - 1;
            raw = (a >> 15) - 32768;
            if (raw > 32767) raw = 32767;
            break;
        }
        }
        v->lfo_phase = ph + tp->lfo_inc;
        if (v->lfo_fade < ZR_ENV_ONE) {
            v->lfo_fade += mul24(ZR_ENV_ONE - v->lfo_fade, tp->lfo_fade_k) + 1;
            if (v->lfo_fade > ZR_ENV_ONE) v->lfo_fade = ZR_ENV_ONE;
        }
        lfo = ((raw * (int32_t)tp->lfo_depth) / 127 * (v->lfo_fade >> 9)) >> 15;
    }

    /* pitch */
    if (v->pitch_q8 != v->target_q8) {
        int32_t d = v->target_q8 - v->pitch_q8;
        if (!tp->glide_k || (d < 8 && d > -8)) v->pitch_q8 = v->target_q8;
        else v->pitch_q8 += mul24(d, tp->glide_k) + (d > 0 ? 1 : -1);
    }
    int32_t pitch = v->pitch_q8 + tp->transpose_q8 + bend_q8;
    if (dest == ZLD_PITCH) pitch += (lfo * (int32_t)tp->lfo_depth) >> 11;
    uint32_t base = zr_pitch_inc(pitch);

    uint32_t inc[4];
    int32_t g1[4];
    int alive = 0;
    int32_t velamp = zr_vol[v->vel & 127];
    for (unsigned i = 0; i < 4; ++i) {
        uint64_t f = ((uint64_t)base * tp->ratio_q12[i]) >> 12;
        f += (int64_t)((int32_t)(f >> 12)) * tp->detune[i];
        env_tick(&v->env[i], &v->stage[i], tp->ka[i], tp->kd[i], tp->kr[i], tp->sus[i]);
        int idx = tp->level_idx[i];
        if (!tp->carrier[i] && dest == ZLD_FM && idx > 0) idx += lfo >> 10;
        if (v->note > 60) idx -= (tp->ksc[i] * (v->note - 60)) >> 6;
        int32_t lv = zr_level[clampi(idx, 0, 127)];
        int32_t vs = tp->vel_sens[i] * 258;
        int32_t peak = (lv * (32767 - vs + ((vs * velamp) >> 15))) >> 15;
        g1[i] = ((v->env[i] >> 9) * peak) >> 15;
        if (f >= 0x7fffffffu) { f = 0; g1[i] = 0; } /* above Nyquist: silence */
        inc[i] = (uint32_t)f;
        if (tp->carrier[i] && v->env[i] > 0) alive = 1;
    }

    {
        int32_t gq0 = v->gain[0] * 256, gq1 = v->gain[1] * 256, gq2 = v->gain[2] * 256, gq3 = v->gain[3] * 256;
        const int32_t dg0 = (g1[0] - v->gain[0]) * 16, dg1 = (g1[1] - v->gain[1]) * 16;
        const int32_t dg2 = (g1[2] - v->gain[2]) * 16, dg3 = (g1[3] - v->gain[3]) * 16;
        uint32_t p0 = v->phase[0], p1 = v->phase[1], p2 = v->phase[2], p3 = v->phase[3];
        const uint32_t i0 = inc[0], i1 = inc[1], i2 = inc[2], i3 = inc[3];
        const unsigned w0 = tp->wave[0], w1 = tp->wave[1], w2 = tp->wave[2], w3 = tp->wave[3];
        const int32_t k01 = tp->km[0][1], k02 = tp->km[0][2], k03 = tp->km[0][3];
        const int32_t k12 = tp->km[1][2], k13 = tp->km[1][3], k23 = tp->km[2][3];
        const int32_t c0 = tp->kc[0], c1 = tp->kc[1], c2 = tp->kc[2], c3 = tp->kc[3];
        const int32_t fb = tp->fb;
        int32_t fb1 = v->fb1, fb2 = v->fb2;
        uint32_t noise = v->noise;
        for (unsigned s = 0; s < ZR_CHUNK; ++s) {
            int32_t o0, o1, o2, o3;
            p3 += i3;
            o3 = (wave(w3, p3 + ((uint32_t)(((fb1 + fb2) >> 1) * fb) << 10), i3, &noise) * (gq3 >> 8)) >> 15;
            gq3 += dg3;
            fb2 = fb1; fb1 = o3;
            p2 += i2;
            o2 = (wave(w2, p2 + ((uint32_t)(o3 & k23) << 18), i2, &noise) * (gq2 >> 8)) >> 15;
            gq2 += dg2;
            p1 += i1;
            o1 = (wave(w1, p1 + ((uint32_t)((o3 & k13) + (o2 & k12)) << 18), i1, &noise) * (gq1 >> 8)) >> 15;
            gq1 += dg1;
            p0 += i0;
            o0 = (wave(w0, p0 + ((uint32_t)((o3 & k03) + (o2 & k02) + (o1 & k01)) << 18), i0, &noise) *
                  (gq0 >> 8)) >> 15;
            gq0 += dg0;
            buf[s] = ((((o0 & c0) + (o1 & c1) + (o2 & c2) + (o3 & c3)) >> 1) * (int32_t)tp->norm_q15) >> 14;
        }
        v->phase[0] = p0; v->phase[1] = p1; v->phase[2] = p2; v->phase[3] = p3;
        v->fb1 = fb1; v->fb2 = fb2;
        v->noise = noise;
        for (unsigned i = 0; i < 4; ++i) v->gain[i] = g1[i];
    }

    if (tp->flt_type) {
        env_tick(&v->fenv, &v->fstage, tp->fka, tp->fkd, tp->fkr, tp->fsus);
        int32_t c = tp->cutoff_q8 + (((v->fenv >> 9) * tp->flt_env) >> 7) +
                    ((((int32_t)v->note - 60) * 282 * tp->flt_key) >> 7) + (int32_t)v->vel * tp->flt_vel * 2;
        if (dest == ZLD_FILTER) c += lfo >> 1;
        c = clampi(c, 0, 127 << 8);
        unsigned ci = (unsigned)c >> 8;
        int32_t f = zr_cutoff[ci];
        if (ci < 127) f += (((int32_t)zr_cutoff[ci + 1] - f) * (c & 255)) >> 8;
        const int32_t damp = tp->damp_q14;
        int32_t lp = v->lp, bp = v->bp;
        const unsigned type = tp->flt_type;
        for (unsigned s = 0; s < ZR_CHUNK; ++s) {
            int32_t in = buf[s], hp = 0;
            for (unsigned k = 0; k < 2; ++k) {
                lp += (int32_t)(((int64_t)bp * f) >> 14);
                hp = in - lp - (int32_t)(((int64_t)bp * damp) >> 14);
                bp += (int32_t)(((int64_t)hp * f) >> 14);
            }
            if (lp > (1 << 20)) lp = 1 << 20; else if (lp < -(1 << 20)) lp = -(1 << 20);
            if (bp > (1 << 20)) bp = 1 << 20; else if (bp < -(1 << 20)) bp = -(1 << 20);
            buf[s] = type == ZFLT_LP ? lp : type == ZFLT_BP ? bp : hp;
        }
        v->lp = lp; v->bp = bp;
    }

    {
        int32_t amp = tp->level_q15;
        if (dest == ZLD_AMP) amp = (amp * (32767 - ((((32767 * (int32_t)tp->lfo_depth) / 127) - lfo) >> 1))) >> 15;
        int pan = (int)tp->pan + v->pan_off;
        if (dest == ZLD_PAN) pan += lfo >> 9;
        unsigned pi = (unsigned)(clampi(pan, 1, 127) * 1024) / 127;
        if (pi > 1023) pi = 1023;
        int32_t tl = (amp * zr_sin[pi + 1024]) >> 15, tr = (amp * zr_sin[pi]) >> 15;
        if (v->fresh) { v->amp_l = tl; v->amp_r = tr; v->fresh = 0; }
        int32_t gl = v->amp_l * 256, gr = v->amp_r * 256;
        const int32_t dl = (tl - v->amp_l) * 16, dr = (tr - v->amp_r) * 16;
        const int32_t ds = tp->dly_send, rs = tp->rev_send;
        v->amp_l = tl; v->amp_r = tr;
        for (unsigned s = 0; s < ZR_CHUNK; ++s) {
            int32_t x = buf[s];
            if (x > 65535) x = 65535; else if (x < -65535) x = -65535;
            int32_t l = (x * (gl >> 8)) >> 15, r = (x * (gr >> 8)) >> 15;
            gl += dl; gr += dr;
            bus[2 * s] += l;
            bus[2 * s + 1] += r;
            int32_t m = (l + r) >> 1;
            dly[s] += (m * ds) >> 15;
            rev[s] += (m * rs) >> 15;
        }
    }
    if (v->state == 2 && !alive) { v->state = 0; return 0; }
    return 1;
}

/* ================= drums ================= */
static const uint8_t drum_pan[ZR_DRUMS] = {64, 64, 80, 90, 52, 42, 78, 38};

ZR_INLINE uint32_t hz_inc(uint32_t hz_x16) { return (uint32_t)(((uint64_t)hz_x16 * 97391u) >> 4); }

ZR_AUDIO_CODE void zr_drums_derive(void)
{
    const uint8_t *p = zr_proj.patch[ZR_DRUM_TRACK].p;
    zr_kparams *k = &zr_kp;
    static const uint8_t dec_lo[ZR_DRUMS] = {55, 50, 34, 62, 52, 55, 28, 55};
    static const uint8_t dec_hi[ZR_DRUMS] = {88, 76, 58, 90, 76, 82, 44, 84};
    int32_t kit = zr_vol[p[ZK_LEVEL] & 127];
    for (unsigned d = 0; d < ZR_DRUMS; ++d) {
        unsigned tune = p[ZK_DRUM(d, ZD_TUNE)] & 127, decay = p[ZK_DRUM(d, ZD_DECAY)] & 127;
        unsigned tone = p[ZK_DRUM(d, ZD_TONE)] & 127;
        k->tone[d] = (uint8_t)tone;
        k->kamp[d] = zr_env_rate[dec_lo[d] + ((dec_hi[d] - dec_lo[d]) * decay) / 127];
        k->kamp2[d] = zr_env_rate[48];
        k->kpitch[d] = zr_env_rate[36];
        k->level[d] = (uint16_t)(((int32_t)zr_vol[p[ZK_DRUM(d, ZD_LEVEL)] & 127] * kit) >> 15);
        int pan = 64 + (((int)drum_pan[d] - 64) * (int)(p[ZK_PAN_WIDTH] & 127)) / 127;
        unsigned pi = (unsigned)(pan * 1024) / 127;
        if (pi > 1023) pi = 1023;
        /* +3 dB so that a centred drum keeps unity gain */
        k->pan_l[d] = (uint16_t)((zr_sin[pi + 1024] * 46340) >> 15);
        k->pan_r[d] = (uint16_t)((zr_sin[pi] * 46340) >> 15);
        uint32_t hz16;
        switch (d) {
        case ZDR_KICK:
            hz16 = 16 * 30 + (tune * 16 * 60) / 127;
            k->inc[d] = hz_inc(hz16);
            k->sweep[d] = (k->inc[d] >> 4) * (tone + 24);
            break;
        case ZDR_TOM:
            hz16 = 16 * 70 + (tune * 16 * 180) / 127;
            k->inc[d] = hz_inc(hz16);
            k->sweep[d] = (k->inc[d] >> 6) * (tone + 16);
            k->kpitch[d] = zr_env_rate[44];
            break;
        case ZDR_SNARE:
            hz16 = 16 * 150 + (tune * 16 * 100) / 127;
            k->inc[d] = hz_inc(hz16);
            k->sweep[d] = k->inc[d] >> 1;
            k->kpitch[d] = zr_env_rate[40];
            break;
        case ZDR_CHAT:
        case ZDR_OHAT:
            hz16 = 16 * 2000 + tune * 16 * 20;
            k->inc[d] = hz_inc(hz16);
            k->sweep[d] = 0;
            break;
        case ZDR_RIM:
            hz16 = 16 * 1100 + tune * 16 * 8;
            k->inc[d] = hz_inc(hz16);
            k->sweep[d] = 0;
            break;
        case ZDR_BELL:
            hz16 = 16 * 380 + tune * 16 * 3;
            k->inc[d] = hz_inc(hz16);
            k->sweep[d] = 0;
            break;
        default: /* clap: band-pass centre */
            k->inc[d] = 2000 + tune * 16; /* SVF f in Q14 */
            k->sweep[d] = 0;
            break;
        }
    }
    k->dly_send = zr_vol[p[ZK_DLY_SEND] & 127];
    k->rev_send = zr_vol[p[ZK_REV_SEND] & 127];
}

ZR_AUDIO_CODE void zr_drum_trigger(unsigned drum, unsigned vel)
{
    if (drum >= ZR_DRUMS || !vel) return;
    zr_drum *d = &zr_drums[drum];
    if (drum == ZDR_CHAT) zr_drums[ZDR_OHAT].amp >>= 4; /* closed hat chokes the open hat */
    d->on = 1;
    d->vel = (uint8_t)vel;
    d->ph1 = d->ph2 = d->ph3 = 0;
    d->t = 0;
    d->amp = d->amp2 = d->penv = ZR_ENV_ONE;
    d->burst = 0;
    if (!d->lp2) d->lp2 = 0x2545f491; /* noise seed */
}

ZR_INLINE int32_t noise16(int32_t *state)
{
    uint32_t n = (uint32_t)*state;
    n ^= n << 13; n ^= n >> 17; n ^= n << 5;
    *state = (int32_t)n;
    return (int16_t)n;
}
ZR_INLINE int32_t softclip(int32_t x)
{
    if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
    return (x * (49152 - ((x * x) >> 16))) >> 15;
}

ZR_AUDIO_CODE unsigned zr_drums_render(int32_t *bus, int32_t *dly, int32_t *rev)
{
    const zr_kparams *k = &zr_kp;
    unsigned active = 0;
    for (unsigned n = 0; n < ZR_DRUMS; ++n) {
        zr_drum *d = &zr_drums[n];
        if (!d->on) continue;
        ++active;
        int32_t buf[ZR_CHUNK];
        const int32_t tone = k->tone[n];
        uint32_t inc = k->inc[n] + (uint32_t)(((uint64_t)k->sweep[n] * (uint32_t)(d->penv >> 9)) >> 15);
        d->penv -= mul24(d->penv, k->kpitch[n]) + 1;
        if (d->penv < 0) d->penv = 0;
        int32_t a0 = d->amp >> 9, b0 = d->amp2 >> 9;
        if (n == ZDR_CLAP && d->t < 1323) d->amp = ZR_ENV_ONE; /* hold during the bursts */
        else d->amp -= mul24(d->amp, k->kamp[n]) + 1;
        d->amp2 -= mul24(d->amp2, k->kamp2[n]) + 1;
        if (d->amp2 < 0) d->amp2 = 0;
        if (d->amp < (ZR_ENV_ONE >> 13)) { d->amp = 0; d->on = 0; }
        const int32_t a1 = d->amp >> 9, b1 = d->amp2 >> 9;
        int32_t a = a0 * 256, b = b0 * 256;
        const int32_t da = (a1 - a0) * 16, db = (b1 - b0) * 16;
        uint32_t p1 = d->ph1, p2 = d->ph2, p3 = d->ph3, t = d->t;
        int32_t lp = d->lp, bp = d->bp, ns = d->lp2;
        for (unsigned s = 0; s < ZR_CHUNK; ++s, ++t) {
            int32_t x, av = a >> 8, bv = b >> 8;
            a += da; b += db;
            switch (n) {
            case ZDR_KICK:
            case ZDR_TOM:
                p1 += inc;
                x = softclip((zr_sin[p1 >> 20] * 3) >> 1);
                if (t < 96) x += (noise16(&ns) * (int32_t)(96 - t) * tone) >> 15;
                x = (x * av) >> 15;
                break;
            case ZDR_SNARE: {
                p1 += inc; p2 += inc + (inc >> 1) + (inc >> 2);
                int32_t body = ((zr_sin[p1 >> 20] + zr_sin[p2 >> 20]) * bv) >> 16;
                int32_t nz = noise16(&ns);
                lp += (nz - lp) >> 2;
                nz = ((nz - lp) * av) >> 15;
                x = (body * (160 - tone) + nz * (tone + 64)) >> 7;
                break;
            }
            case ZDR_CHAT:
            case ZDR_OHAT: {
                p1 += inc; p2 += inc + (inc >> 1) - (inc >> 4); p3 += (inc << 1) - (inc >> 3);
                int32_t m = ((p1 ^ p2 ^ p3) & 0x80000000u) ? -20000 : 20000;
                int32_t nz = noise16(&ns);
                int32_t y = (m * tone + nz * (127 - tone)) >> 7;
                lp += (y - lp) >> 1;
                y -= lp;
                bp += (y - bp) >> 1;
                x = ((y - (bp >> 1)) * av) >> 15;
                break;
            }
            case ZDR_CLAP: {
                int32_t nz = noise16(&ns), f = (int32_t)k->inc[n];
                lp += (bp * f) >> 14;
                bp += ((nz - lp - (bp >> 1)) * f) >> 14;
                int32_t e = av;
                if (t < 1323) { /* three 10 ms bursts before the tail */
                    uint32_t tb = t;
                    while (tb >= 441) tb -= 441;
                    e = 32767 - (int32_t)(tb * 74);
                }
                if (bp > 120000) bp = 120000; else if (bp < -120000) bp = -120000;
                x = ((bp >> 1) * e) >> 13;
                break;
            }
            case ZDR_RIM: {
                p1 += inc; p2 += (inc >> 2) + (inc >> 4);
                x = (zr_sin[p1 >> 20] + (zr_sin[p2 >> 20] >> 1)) >> 1;
                if (t < 40) x += noise16(&ns) >> 2;
                x = (x * av) >> 15;
                break;
            }
            default: { /* bell: two detuned squares through a band-pass pair */
                p1 += inc; p2 += inc + (inc >> 1) - (inc >> 5);
                int32_t y = ((p1 & 0x80000000u) ? -14000 : 14000) + ((p2 & 0x80000000u) ? -12000 : 12000);
                lp += (y - lp) >> (tone < 40 ? 2 : 1);
                bp += (lp - bp) >> 5;
                x = ((lp - bp) * av) >> 15;
                break;
            }
            }
            buf[s] = x;
        }
        d->ph1 = p1; d->ph2 = p2; d->ph3 = p3; d->t = t;
        d->lp = lp; d->bp = bp; d->lp2 = ns;
        const int32_t g = ((int32_t)zr_vol[d->vel & 127] * k->level[n]) >> 15;
        const int32_t gl = (g * k->pan_l[n]) >> 15, gr = (g * k->pan_r[n]) >> 15;
        const int32_t ds = k->dly_send, rs = n == ZDR_KICK ? k->rev_send >> 2 : k->rev_send;
        for (unsigned s = 0; s < ZR_CHUNK; ++s) {
            int32_t x = buf[s];
            if (x > 45000) x = 45000; else if (x < -45000) x = -45000;
            int32_t m = (x * g) >> 15;
            bus[2 * s] += (x * gl) >> 15;
            bus[2 * s + 1] += (x * gr) >> 15;
            dly[s] += (m * ds) >> 15;
            rev[s] += (m * rs) >> 15;
        }
    }
    return active;
}
