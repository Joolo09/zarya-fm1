/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Audio-side state shared by zr_engine.c and zr_synth.c. */
#ifndef ZR_ENG_H
#define ZR_ENG_H
#include "zr_dsp.h"

#define ZR_ENV_ONE (1 << 24)

typedef struct zr_tparams {
    uint8_t algo, mode, flt_type, lfo_shape, lfo_dest, lfo_sync, bend_range, spread;
    int32_t km[4][4];   /* km[target][source]: all-ones when source modulates target */
    int32_t kc[4];      /* all-ones when the operator is a carrier */
    uint8_t wave[4], vel_sens[4], ksc[4], carrier[4];
    int16_t level_idx[4];
    uint32_t ratio_q12[4];
    int16_t detune[4];
    uint32_t ka[4], kd[4], kr[4];
    int32_t sus[4];
    int32_t fb;
    int32_t transpose_q8;
    uint32_t glide_k;
    uint16_t level_q15, norm_q15;
    int32_t cutoff_q8;
    int32_t damp_q14;
    int16_t flt_env, flt_key, flt_vel;
    uint32_t fka, fkd, fkr;
    int32_t fsus;
    uint32_t lfo_inc, lfo_fade_k;
    uint16_t lfo_depth;
    uint16_t pan; /* 0..127 */
    uint16_t dly_send, rev_send;
} zr_tparams;

typedef struct zr_voice {
    uint8_t state; /* 0 free, 1 held, 2 released */
    uint8_t track, note, vel, sustained, fstage, fresh, rsv;
    uint32_t age;
    uint32_t phase[4];
    int32_t env[4];
    uint8_t stage[4];
    int32_t gain[4];
    uint16_t peak[4];
    int32_t fb1, fb2;
    int32_t pitch_q8, target_q8;
    int32_t fenv, lp, bp;
    int32_t amp_l, amp_r;
    uint32_t lfo_phase, noise;
    int32_t lfo_fade, lfo_sh;
    int16_t pan_off;
    int16_t tune, tune_to; /* PURE: adaptive just-intonation offset, 1/256 semitone */
} zr_voice;

typedef struct {
    uint8_t on, vel, burst, rsv;
    uint32_t ph1, ph2, ph3, t;
    int32_t amp, amp2, penv, lp, bp, lp2, gain;
} zr_drum;

typedef struct {
    uint32_t inc[ZR_DRUMS], sweep[ZR_DRUMS];
    uint32_t kamp[ZR_DRUMS], kamp2[ZR_DRUMS], kpitch[ZR_DRUMS];
    uint16_t level[ZR_DRUMS], pan_l[ZR_DRUMS], pan_r[ZR_DRUMS];
    uint8_t tone[ZR_DRUMS];
    uint16_t dly_send, rev_send;
} zr_kparams;

extern zr_tparams zr_tp[3];
extern zr_kparams zr_kp;
extern zr_voice zr_voices[ZR_VOICES];
extern zr_drum zr_drums[ZR_DRUMS];
extern uint32_t zr_lfo_free[3];

/* Start a voice (envelopes, per-note levels). legato: keep phases/envelopes. */
void zr_voice_start(zr_voice *v, const zr_tparams *tp, unsigned note, unsigned vel, int legato);
/* Render ZR_CHUNK samples of one voice, adding to the buses. Returns 0 when
 * the voice has finished. */
int zr_voice_render(zr_voice *v, const zr_tparams *tp, int32_t bend_q8, int32_t *bus, int32_t *dly, int32_t *rev);
void zr_drum_trigger(unsigned drum, unsigned vel);
/* Render ZR_CHUNK samples of all drums. Returns the number of active drums. */
unsigned zr_drums_render(int32_t *bus, int32_t *dly, int32_t *rev);
uint32_t zr_pitch_inc(int32_t pitch_q8);

#endif
