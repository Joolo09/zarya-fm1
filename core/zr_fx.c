/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA effects: tempo-synced dual-tap delay, small plate reverb (both run
 * at half rate to save RAM), kick-driven pump, DJ filter, drive and the
 * output limiter. Called once per 128-frame block from the audio context. */
#include "zr_eng.h"

#define DLY_MASK (ZR_DELAY_LEN - 1)
enum { AP1 = 225, AP2 = 341, C1 = 1103, C2 = 1277, C3 = 1361, C4 = 1499 };

static struct {
    int16_t dly[ZR_DELAY_LEN];
    int16_t ap1[AP1], ap2[AP2], c1[C1], c2[C2], c3[C3], c4[C4];
    uint32_t dw, i_ap1, i_ap2, i_c[4];
    int32_t cf[4];                 /* comb damping state */
    int32_t dly_lp, dly_prev_l, dly_prev_r, rev_prev_l, rev_prev_r;
    uint32_t dly_t, dly_target, step_len_q8;
    int32_t pump;                  /* Q24 duck amount */
    int32_t dj_f, lp[2], bp[2];
    int32_t peak;
} fx;

static const uint16_t dly_mult_q8[8] ZR_AUDIO_DATA = {128, 256, 341, 384, 512, 683, 768, 1024};

ZR_INLINE int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }

void zr_fx_init(void)
{
    zr_zero(&fx, sizeof fx);
    fx.dly_t = fx.dly_target = 2756;
    fx.dj_f = 16000;
}
ZR_AUDIO_CODE void zr_fx_set_tempo(uint32_t step_len_q8) { fx.step_len_q8 = step_len_q8; }
ZR_AUDIO_CODE void zr_fx_pump_trigger(void) { fx.pump = ZR_ENV_ONE; }

ZR_AUDIO_CODE void zr_fx_process(int32_t *mix, const int32_t *duck, const int32_t *dly, const int32_t *rev,
                                 unsigned n, int32_t *out)
{
    const uint8_t *g = zr_proj.g;
    /* ---- block-rate parameters ---- */
    uint32_t t = (uint32_t)(((uint64_t)fx.step_len_q8 * dly_mult_q8[g[ZG_DLY_TIME] & 7]) >> 17);
    while (t > ZR_DELAY_LEN - 4) t >>= 1;
    if (t < 32) t = 32;
    fx.dly_target = t;
    const int32_t dfb = (int32_t)g[ZG_DLY_FB] * 258 > 31000 ? 31000 : (int32_t)g[ZG_DLY_FB] * 258;
    const int32_t dtone = 2000 + (int32_t)g[ZG_DLY_TONE] * 240;
    const int32_t dlvl = zr_vol[g[ZG_DLY_LEVEL] & 127];
    const int32_t rg = 19660 + (int32_t)(g[ZG_REV_SIZE] & 127) * 88;
    const int32_t rdamp = 32767 - (int32_t)(g[ZG_REV_DAMP] & 127) * 200;
    const int32_t rlvl = zr_vol[g[ZG_REV_LEVEL] & 127];
    const int32_t pump_depth = (int32_t)(g[ZG_PUMP] & 127) * 250; /* Q15, max 0.97 */
    const uint32_t pump_k = zr_env_rate[62 + ((g[ZG_PUMP_REL] & 127) * 30) / 127];
    const int32_t pump0 = (int32_t)(((int64_t)(fx.pump >> 9) * pump_depth) >> 15);
    for (unsigned i = 0; i < n / ZR_CHUNK; ++i) fx.pump -= (int32_t)(((int64_t)fx.pump * pump_k) >> 24) + 1;
    if (fx.pump < 0) fx.pump = 0;
    const int32_t pump1 = (int32_t)(((int64_t)(fx.pump >> 9) * pump_depth) >> 15);
    zr_status.pump = (uint8_t)(pump1 >> 8);
    int32_t pg = (32767 - pump0) << 8;
    const int32_t dpg = ((pump0 - pump1) * 256) / (int32_t)n;
    const int32_t drive = g[ZG_DRIVE] & 127;
    const int32_t pre_q4 = 16 + drive;
    const int32_t post_q15 = (64 * 32768) / (64 + (drive >> 1));
    int dj = (int)g[ZG_DJ_FILTER] - 64;
    int32_t dj_target = 0;
    unsigned dj_mode = 0;
    if (dj < -4) { dj_mode = 1; dj_target = zr_cutoff[20 + ((dj + 63) * 107) / 59]; }
    else if (dj > 4) { dj_mode = 2; dj_target = zr_cutoff[((dj - 5) * 100) / 58]; }
    if (dj_mode) fx.dj_f += (dj_target - fx.dj_f) >> 2;
    else { fx.lp[0] = fx.lp[1] = fx.bp[0] = fx.bp[1] = 0; fx.dj_f = dj_target ? dj_target : fx.dj_f; }
    const int32_t djf = fx.dj_f;
    int32_t peak = 0;

    for (unsigned s = 0; s < n; s += 2) {
        /* ---- half-rate section: delay ---- */
        int32_t din = (dly[s] + dly[s + 1]) >> 1;
        if (fx.dly_t < fx.dly_target) ++fx.dly_t;
        else if (fx.dly_t > fx.dly_target) --fx.dly_t;
        uint32_t w = fx.dw;
        int32_t tap_l = fx.dly[(w - fx.dly_t) & DLY_MASK];
        int32_t tap_r = fx.dly[(w - ((fx.dly_t * 3) >> 2)) & DLY_MASK];
        fx.dly_lp += ((tap_l - fx.dly_lp) * dtone) >> 15;
        fx.dly[w & DLY_MASK] = (int16_t)sat16(din + ((fx.dly_lp * dfb) >> 15));
        fx.dw = w + 1;
        /* ---- half-rate section: reverb ---- */
        int32_t rin = (rev[s] + rev[s + 1]) >> 2;
        int32_t bo = fx.ap1[fx.i_ap1], x = bo - (rin >> 1);
        fx.ap1[fx.i_ap1] = (int16_t)sat16(rin + (bo >> 1));
        if (++fx.i_ap1 >= AP1) fx.i_ap1 = 0;
        bo = fx.ap2[fx.i_ap2];
        int32_t y = bo - (x >> 1);
        fx.ap2[fx.i_ap2] = (int16_t)sat16(x + (bo >> 1));
        if (++fx.i_ap2 >= AP2) fx.i_ap2 = 0;
        int32_t cv[4];
        {
            int16_t *const cb[4] = {fx.c1, fx.c2, fx.c3, fx.c4};
            static const uint16_t cl[4] ZR_AUDIO_DATA = {C1, C2, C3, C4};
            for (unsigned c = 0; c < 4; ++c) {
                int32_t o = cb[c][fx.i_c[c]];
                fx.cf[c] += ((o - fx.cf[c]) * rdamp) >> 15;
                cb[c][fx.i_c[c]] = (int16_t)sat16(y + ((fx.cf[c] * rg) >> 15));
                if (++fx.i_c[c] >= cl[c]) fx.i_c[c] = 0;
                cv[c] = o;
            }
        }
        const int32_t rl = (cv[0] - cv[1] + cv[2] - cv[3]) >> 1, rr = (cv[0] + cv[1] - cv[2] - cv[3]) >> 1;

        /* ---- full-rate output: two samples per half-rate step ---- */
        for (unsigned k = 0; k < 2; ++k) {
            const unsigned i = s + k;
            const int32_t wl = k ? tap_l : (tap_l + fx.dly_prev_l) >> 1;
            const int32_t wr = k ? tap_r : (tap_r + fx.dly_prev_r) >> 1;
            const int32_t vl = k ? rl : (rl + fx.rev_prev_l) >> 1;
            const int32_t vr = k ? rr : (rr + fx.rev_prev_r) >> 1;
            int32_t l = mix[2 * i] + ((wl * dlvl) >> 15) + ((vl * rlvl) >> 15);
            int32_t r = mix[2 * i + 1] + ((wr * dlvl) >> 15) + ((vr * rlvl) >> 15);
            if (duck) {
                const int32_t gq = pg >> 8;
                l += (int32_t)(((int64_t)duck[2 * i] * gq) >> 15);
                r += (int32_t)(((int64_t)duck[2 * i + 1] * gq) >> 15);
                pg += dpg;
            }
            if (dj_mode) {
                int32_t in[2] = {l, r}, o[2];
                for (unsigned c = 0; c < 2; ++c) {
                    int32_t hp = 0;
                    for (unsigned q = 0; q < 2; ++q) {
                        fx.lp[c] += (int32_t)(((int64_t)fx.bp[c] * djf) >> 14);
                        hp = in[c] - fx.lp[c] - fx.bp[c];
                        fx.bp[c] += (int32_t)(((int64_t)hp * djf) >> 14);
                    }
                    if (fx.lp[c] > (1 << 22)) fx.lp[c] = 1 << 22; else if (fx.lp[c] < -(1 << 22)) fx.lp[c] = -(1 << 22);
                    if (fx.bp[c] > (1 << 22)) fx.bp[c] = 1 << 22; else if (fx.bp[c] < -(1 << 22)) fx.bp[c] = -(1 << 22);
                    o[c] = dj_mode == 1 ? fx.lp[c] : hp;
                }
                l = o[0]; r = o[1];
            }
            int32_t ch[2] = {l, r};
            for (unsigned c = 0; c < 2; ++c) {
                int32_t v = ch[c];
                if (v > (1 << 22)) v = 1 << 22; else if (v < -(1 << 22)) v = -(1 << 22);
                v = (v * pre_q4) >> 4;
                int32_t a = v < 0 ? -v : v, yv;
                /* transparent to half scale, then a parabolic knee to full scale */
                if (a <= 32768) yv = a;
                else if (a >= 98304) yv = 65535;
                else { int32_t u = (a - 32768) >> 1; yv = 32768 + 2 * u - ((u * u) >> 15); }
                if (yv > 65535) yv = 65535;
                if (yv > peak) peak = yv;
                yv = (yv * post_q15) >> 15;
                out[2 * i + c] = (v < 0 ? -yv : yv) * 128;
            }
        }
        fx.dly_prev_l = tap_l; fx.dly_prev_r = tap_r;
        fx.rev_prev_l = rl; fx.rev_prev_r = rr;
    }
    if (peak >= 65000) zr_status.clip = 40;
    else if (zr_status.clip) --zr_status.clip;
    {
        int32_t m = peak >> 9; /* 0..127 */
        if (m > 127) m = 127;
        int32_t old = zr_status.peak_master;
        zr_status.peak_master = (uint8_t)(m > old ? m : old > 2 ? old - 3 : 0);
    }
}
