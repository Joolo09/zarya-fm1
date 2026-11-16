/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Factory sounds, project defaults, validation and blob serialization. */
#include "zr_dsp.h"

/* ---- factory bank: each preset is the INIT voice plus a list of overrides ---- */
#define G(id, v) (id), (v)
#define O(op, field, v) (uint8_t)(ZP_OP_BASE + ((op) - 1) * ZO_COUNT + (field)), (v)
#define END 255
enum { R = ZO_RATIO, F = ZO_FINE, DT = ZO_DETUNE, L = ZO_LEVEL, W = ZO_WAVE, A = ZO_ATK, D = ZO_DEC, S = ZO_SUS,
       RL = ZO_REL, V = ZO_VEL, K = ZO_KSC };

static const uint8_t p_sub[] = {G(ZP_MODE, ZMODE_MONO), G(ZP_GLIDE, 25), G(ZP_TRANSPOSE, 52), G(ZP_LEVEL, 112),
    O(1, D, 80), O(1, S, 115), O(1, RL, 35), O(1, V, 40), O(2, L, 80), O(2, D, 55), O(2, S, 60), O(2, RL, 35), END};
static const uint8_t p_rubber[] = {G(ZP_MODE, ZMODE_MONO), G(ZP_TRANSPOSE, 52), O(1, D, 72), O(1, S, 95), O(1, RL, 30),
    O(2, L, 108), O(2, D, 48), O(2, S, 55), O(2, RL, 30), O(2, V, 90), END};
static const uint8_t p_acid[] = {G(ZP_MODE, ZMODE_LEGATO), G(ZP_GLIDE, 35), G(ZP_TRANSPOSE, 52), O(1, W, ZW_SAW),
    O(1, RL, 30), G(ZP_FLT_TYPE, ZFLT_LP), G(ZP_CUTOFF, 48), G(ZP_RESO, 100), G(ZP_FLT_ENV, 112), G(ZP_FD, 52),
    G(ZP_FS, 0), G(ZP_FLT_VEL, 40), G(ZP_M1_DEST, ZMD_RESO), G(ZP_M1_AMT, 40), END};
static const uint8_t p_reese[] = {G(ZP_ALGO, 4), G(ZP_MODE, ZMODE_MONO), G(ZP_TRANSPOSE, 52), G(ZP_SPREAD, 90),
    O(1, W, ZW_SAW), O(1, DT, 13), O(3, W, ZW_SAW), O(3, DT, 1), O(3, L, 127), G(ZP_FLT_TYPE, ZFLT_LP),
    G(ZP_CUTOFF, 62), G(ZP_RESO, 35), G(ZP_LFO_RATE, 45), G(ZP_LFO_DEST, ZLD_FILTER), G(ZP_LFO_DEPTH, 40), END};
static const uint8_t p_wobble[] = {G(ZP_MODE, ZMODE_MONO), G(ZP_TRANSPOSE, 52), O(1, W, ZW_SQUARE), O(2, L, 90),
    G(ZP_FLT_TYPE, ZFLT_LP), G(ZP_CUTOFF, 40), G(ZP_RESO, 80), G(ZP_LFO_RATE, 88), G(ZP_LFO_SHAPE, ZLFO_SINE),
    G(ZP_LFO_DEST, ZLD_FILTER), G(ZP_LFO_DEPTH, 100), G(ZP_LFO_SYNC, 1), G(ZP_M1_DEST, ZMD_LFO_RATE), END};
static const uint8_t p_slap[] = {G(ZP_ALGO, 1), G(ZP_TRANSPOSE, 52), O(1, D, 62), O(1, S, 70), O(1, RL, 30),
    O(2, L, 112), O(2, D, 42), O(2, S, 0), O(2, V, 100), O(3, R, 3), O(3, L, 100), O(3, D, 32), O(3, S, 0),
    O(3, V, 110), END};

static const uint8_t p_neon[] = {G(ZP_MODE, ZMODE_LEGATO), G(ZP_GLIDE, 20), O(1, W, ZW_SQUARE), O(1, RL, 45),
    O(2, R, 2), O(2, L, 85), G(ZP_FLT_TYPE, ZFLT_LP), G(ZP_CUTOFF, 92), G(ZP_RESO, 45), G(ZP_FLT_KEY, 60),
    G(ZP_LFO_RATE, 98), G(ZP_LFO_DEST, ZLD_PITCH), G(ZP_LFO_DEPTH, 14), G(ZP_LFO_FADE, 70), G(ZP_DLY_SEND, 55),
    G(ZP_REV_SEND, 25), END};
static const uint8_t p_cry[] = {G(ZP_MODE, ZMODE_MONO), G(ZP_GLIDE, 15), O(1, W, ZW_FOLD), O(2, R, 3), O(2, L, 100),
    O(2, D, 75), O(2, S, 90), G(ZP_LFO_RATE, 80), G(ZP_LFO_DEST, ZLD_FM), G(ZP_LFO_DEPTH, 50), G(ZP_LFO_FADE, 40),
    G(ZP_DLY_SEND, 40), END};
static const uint8_t p_brass[] = {G(ZP_ALGO, 2), G(ZP_FEEDBACK, 70), O(1, A, 38), O(1, D, 80), O(1, S, 105),
    O(1, RL, 45), O(2, L, 90), O(2, A, 40), O(2, S, 80), O(4, L, 104), O(4, A, 45), O(4, D, 75), O(4, S, 85),
    O(4, V, 100), G(ZP_REV_SEND, 30), END};
static const uint8_t p_whistle[] = {G(ZP_ALGO, 6), O(1, A, 45), O(1, RL, 50), O(2, W, ZW_NOISE), O(2, L, 92),
    O(2, A, 50), O(2, S, 70), G(ZP_FLT_TYPE, ZFLT_BP), G(ZP_CUTOFF, 74), G(ZP_RESO, 75), G(ZP_FLT_KEY, 127),
    G(ZP_LFO_RATE, 96), G(ZP_LFO_DEST, ZLD_PITCH), G(ZP_LFO_DEPTH, 12), G(ZP_LFO_FADE, 80), G(ZP_REV_SEND, 45), END};
static const uint8_t p_chip[] = {O(1, W, ZW_SQUARE), O(1, D, 60), O(1, S, 100), O(1, RL, 30), G(ZP_LFO_RATE, 100),
    G(ZP_LFO_DEST, ZLD_PITCH), G(ZP_LFO_DEPTH, 18), G(ZP_LFO_FADE, 50), G(ZP_LEVEL, 90), END};

static const uint8_t p_epiano[] = {G(ZP_ALGO, 4), G(ZP_SPREAD, 40), O(1, D, 86), O(1, S, 0), O(1, RL, 48), O(1, V, 80),
    O(2, L, 97), O(2, D, 62), O(2, S, 0), O(2, V, 110), O(3, DT, 9), O(3, L, 118), O(3, D, 80), O(3, S, 0),
    O(3, RL, 48), O(4, R, 14), O(4, L, 88), O(4, D, 42), O(4, S, 0), O(4, V, 120), O(4, K, 40),
    G(ZP_REV_SEND, 25), END};
static const uint8_t p_glass[] = {G(ZP_ALGO, 5), O(4, R, 3), O(4, F, 21), O(4, L, 100), O(4, D, 55), O(4, S, 20),
    O(1, D, 80), O(1, S, 0), O(1, RL, 55), O(2, R, 2), O(2, L, 110), O(2, D, 70), O(2, S, 0), O(2, RL, 55),
    O(3, R, 4), O(3, L, 95), O(3, D, 60), O(3, S, 0), G(ZP_REV_SEND, 40), G(ZP_DLY_SEND, 25), END};
static const uint8_t p_organ[] = {G(ZP_ALGO, 7), O(1, R, 0), O(1, L, 118), O(1, RL, 25), O(1, V, 0), O(2, L, 127),
    O(2, RL, 25), O(2, V, 0), O(3, R, 2), O(3, L, 112), O(3, RL, 25), O(3, V, 0), O(4, R, 3), O(4, L, 100),
    O(4, RL, 25), O(4, V, 0), G(ZP_LFO_RATE, 92), G(ZP_LFO_SHAPE, ZLFO_SINE), G(ZP_LFO_DEST, ZLD_PAN),
    G(ZP_LFO_DEPTH, 40), G(ZP_REV_SEND, 20), END};
static const uint8_t p_clav[] = {O(1, W, ZW_SQUARE), O(1, D, 58), O(1, S, 0), O(1, RL, 28), O(2, R, 3), O(2, L, 104),
    O(2, D, 40), O(2, S, 0), O(2, V, 110), G(ZP_FLT_TYPE, ZFLT_BP), G(ZP_CUTOFF, 85), G(ZP_RESO, 30),
    G(ZP_FLT_KEY, 80), END};
static const uint8_t p_marimba[] = {O(1, D, 55), O(1, S, 0), O(1, RL, 45), O(2, R, 4), O(2, L, 98), O(2, D, 28),
    O(2, S, 0), O(2, V, 100), G(ZP_REV_SEND, 30), END};

static const uint8_t p_aurora[] = {G(ZP_ALGO, 4), G(ZP_SPREAD, 110), O(1, A, 78), O(1, D, 90), O(1, S, 110),
    O(1, RL, 82), O(2, L, 96), O(2, A, 88), O(2, S, 90), O(2, RL, 82), O(3, DT, 12), O(3, L, 120), O(3, A, 80),
    O(3, S, 110), O(3, RL, 82), O(4, R, 2), O(4, L, 92), O(4, A, 92), O(4, RL, 82), G(ZP_LFO_RATE, 35),
    G(ZP_LFO_DEST, ZLD_FM), G(ZP_LFO_DEPTH, 45), G(ZP_REV_SEND, 85), G(ZP_DLY_SEND, 30), END};
static const uint8_t p_strings[] = {G(ZP_ALGO, 4), G(ZP_SPREAD, 120), O(1, W, ZW_SAW), O(1, DT, 11), O(1, A, 62),
    O(1, RL, 72), O(3, W, ZW_SAW), O(3, DT, 3), O(3, L, 127), O(3, A, 66), O(3, RL, 72), G(ZP_FLT_TYPE, ZFLT_LP),
    G(ZP_CUTOFF, 80), G(ZP_RESO, 15), G(ZP_FLT_KEY, 50), G(ZP_LFO_RATE, 94), G(ZP_LFO_DEST, ZLD_PITCH),
    G(ZP_LFO_DEPTH, 8), G(ZP_LFO_FADE, 60), G(ZP_REV_SEND, 60), END};
static const uint8_t p_choir[] = {G(ZP_ALGO, 4), G(ZP_SPREAD, 90), O(1, W, ZW_HALF), O(1, A, 58), O(1, RL, 68),
    O(2, R, 2), O(2, L, 70), O(3, W, ZW_HALF), O(3, DT, 11), O(3, L, 122), O(3, A, 60), O(3, RL, 68),
    G(ZP_FLT_TYPE, ZFLT_BP), G(ZP_CUTOFF, 82), G(ZP_RESO, 78), G(ZP_FLT_KEY, 30), G(ZP_LFO_RATE, 95),
    G(ZP_LFO_DEST, ZLD_PITCH), G(ZP_LFO_DEPTH, 10), G(ZP_LFO_FADE, 75), G(ZP_REV_SEND, 75), END};
static const uint8_t p_drone[] = {G(ZP_ALGO, 1), G(ZP_TRANSPOSE, 52), O(1, W, ZW_FOLD), O(1, A, 70), O(1, RL, 88),
    O(2, R, 0), O(2, L, 100), O(2, A, 80), O(2, RL, 88), O(3, F, 64), O(3, L, 95), O(3, A, 85),
    G(ZP_FLT_TYPE, ZFLT_LP), G(ZP_CUTOFF, 58), G(ZP_RESO, 50), G(ZP_LFO_RATE, 70), G(ZP_LFO_SHAPE, ZLFO_SH),
    G(ZP_LFO_DEST, ZLD_FILTER), G(ZP_LFO_DEPTH, 45), G(ZP_REV_SEND, 90), END};
static const uint8_t p_shimmer[] = {G(ZP_ALGO, 5), G(ZP_SPREAD, 100), O(4, R, 7), O(4, L, 88), O(4, A, 92),
    O(4, D, 100), O(4, S, 70), O(4, RL, 85), O(1, A, 55), O(1, RL, 85), O(2, R, 2), O(2, L, 112), O(2, A, 70),
    O(2, RL, 85), O(3, R, 4), O(3, L, 96), O(3, A, 85), O(3, RL, 85), G(ZP_REV_SEND, 90), G(ZP_DLY_SEND, 50), END};

static const uint8_t p_koto[] = {O(1, D, 62), O(1, S, 0), O(1, RL, 50), O(2, R, 2), O(2, L, 108), O(2, D, 34),
    O(2, S, 0), O(2, V, 110), O(3, R, 5), O(3, L, 90), O(3, D, 24), O(3, S, 0), O(3, V, 120),
    G(ZP_REV_SEND, 35), END};
static const uint8_t p_pizz[] = {O(1, W, ZW_TRI), O(1, D, 48), O(1, S, 0), O(1, RL, 35), O(2, L, 85), O(2, D, 30),
    O(2, S, 0), G(ZP_FLT_TYPE, ZFLT_LP), G(ZP_CUTOFF, 62), G(ZP_FLT_ENV, 104), G(ZP_FD, 36), G(ZP_RESO, 20),
    G(ZP_REV_SEND, 30), END};
static const uint8_t p_harp[] = {G(ZP_ALGO, 4), G(ZP_SPREAD, 80), O(1, D, 78), O(1, S, 0), O(1, RL, 66), O(2, R, 2),
    O(2, L, 96), O(2, D, 46), O(2, S, 0), O(2, V, 100), O(3, W, ZW_TRI), O(3, DT, 10), O(3, L, 115), O(3, D, 80),
    O(3, S, 0), O(3, RL, 66), G(ZP_REV_SEND, 55), END};
static const uint8_t p_seqpluck[] = {O(1, W, ZW_SAW), O(1, D, 55), O(1, S, 0), O(1, RL, 40), G(ZP_FLT_TYPE, ZFLT_LP),
    G(ZP_CUTOFF, 46), G(ZP_RESO, 62), G(ZP_FLT_ENV, 112), G(ZP_FD, 44), G(ZP_FS, 0), G(ZP_FLT_VEL, 30),
    G(ZP_DLY_SEND, 70), G(ZP_REV_SEND, 30), END};

static const uint8_t p_tubular[] = {G(ZP_ALGO, 4), O(1, D, 96), O(1, S, 0), O(1, RL, 88), O(2, R, 3), O(2, F, 21),
    O(2, L, 104), O(2, D, 90), O(2, S, 0), O(3, R, 2), O(3, L, 105), O(3, D, 85), O(3, S, 0), O(3, RL, 88),
    O(4, R, 7), O(4, F, 2), O(4, L, 96), O(4, D, 70), O(4, S, 0), G(ZP_REV_SEND, 60), END};
static const uint8_t p_crystal[] = {G(ZP_ALGO, 5), G(ZP_TRANSPOSE, 76), G(ZP_SPREAD, 70), O(4, R, 9), O(4, L, 98),
    O(4, D, 62), O(4, S, 0), O(4, V, 100), O(1, D, 84), O(1, S, 0), O(1, RL, 78), O(2, R, 3), O(2, L, 105),
    O(2, D, 76), O(2, S, 0), O(2, RL, 78), O(3, R, 5), O(3, F, 10), O(3, L, 92), O(3, D, 66), O(3, S, 0),
    O(3, RL, 70), G(ZP_REV_SEND, 65), G(ZP_DLY_SEND, 40), END};
static const uint8_t p_musicbox[] = {G(ZP_TRANSPOSE, 76), O(1, D, 68), O(1, S, 0), O(1, RL, 60), O(2, R, 7),
    O(2, L, 90), O(2, D, 36), O(2, S, 0), O(2, V, 110), G(ZP_REV_SEND, 45), END};
static const uint8_t p_gamelan[] = {G(ZP_ALGO, 2), O(1, D, 80), O(1, S, 0), O(1, RL, 75), O(2, R, 2), O(2, F, 52),
    O(2, L, 100), O(2, D, 70), O(2, S, 0), O(3, F, 30), O(3, L, 80), O(4, R, 5), O(4, F, 20), O(4, L, 92),
    O(4, D, 50), O(4, S, 0), G(ZP_REV_SEND, 50), END};

static const uint8_t p_laser[] = {O(1, D, 50), O(1, S, 0), O(1, RL, 40), O(2, R, 9), O(2, L, 118), O(2, D, 34),
    O(2, S, 0), G(ZP_LFO_RATE, 110), G(ZP_LFO_SHAPE, ZLFO_SAW), G(ZP_LFO_DEST, ZLD_PITCH), G(ZP_LFO_DEPTH, 110),
    G(ZP_LFO_SYNC, 1), G(ZP_DLY_SEND, 60), END};
static const uint8_t p_sweep[] = {O(1, W, ZW_NOISE), O(1, A, 70), O(1, RL, 75), G(ZP_FLT_TYPE, ZFLT_BP),
    G(ZP_CUTOFF, 70), G(ZP_RESO, 85), G(ZP_LFO_RATE, 30), G(ZP_LFO_DEST, ZLD_FILTER), G(ZP_LFO_DEPTH, 110),
    G(ZP_REV_SEND, 60), END};
static const uint8_t p_robot[] = {G(ZP_MODE, ZMODE_MONO), O(1, W, ZW_FOLD), O(2, R, 2), O(2, L, 104),
    G(ZP_LFO_RATE, 100), G(ZP_LFO_SHAPE, ZLFO_SH), G(ZP_LFO_DEST, ZLD_FM), G(ZP_LFO_DEPTH, 100),
    G(ZP_FLT_TYPE, ZFLT_BP), G(ZP_CUTOFF, 78), G(ZP_RESO, 60), G(ZP_DLY_SEND, 35), END};
static const uint8_t p_blip[] = {O(1, W, ZW_SQUARE), O(1, D, 40), O(1, S, 0), O(1, RL, 25), O(2, W, ZW_SQUARE),
    O(2, R, 2), O(2, L, 96), O(2, D, 25), O(2, S, 0), G(ZP_DLY_SEND, 30), G(ZP_LEVEL, 90), END};
static const uint8_t p_init[] = {END};

typedef struct { const char *name; uint8_t category; const uint8_t *data; } factory_t;
static const factory_t factory[] = {
    {"SUB ZERO", ZCAT_BASS, p_sub}, {"RUBBER", ZCAT_BASS, p_rubber}, {"ACID LINE", ZCAT_BASS, p_acid},
    {"REESE", ZCAT_BASS, p_reese}, {"WOBBLE", ZCAT_BASS, p_wobble}, {"FM SLAP", ZCAT_BASS, p_slap},
    {"NEON LEAD", ZCAT_LEAD, p_neon}, {"SYNC CRY", ZCAT_LEAD, p_cry}, {"BRASSY", ZCAT_LEAD, p_brass},
    {"WHISTLE", ZCAT_LEAD, p_whistle}, {"CHIP HERO", ZCAT_LEAD, p_chip},
    {"E-PIANO", ZCAT_KEYS, p_epiano}, {"GLASS KEYS", ZCAT_KEYS, p_glass}, {"ORGAN", ZCAT_KEYS, p_organ},
    {"CLAV", ZCAT_KEYS, p_clav}, {"MARIMBA", ZCAT_KEYS, p_marimba},
    {"AURORA PAD", ZCAT_PAD, p_aurora}, {"WARM STRINGS", ZCAT_PAD, p_strings}, {"CHOIR", ZCAT_PAD, p_choir},
    {"DARK DRONE", ZCAT_PAD, p_drone}, {"SHIMMER", ZCAT_PAD, p_shimmer},
    {"KOTO", ZCAT_PLUCK, p_koto}, {"PIZZ", ZCAT_PLUCK, p_pizz}, {"HARP", ZCAT_PLUCK, p_harp},
    {"SEQ PLUCK", ZCAT_PLUCK, p_seqpluck},
    {"TUBULAR", ZCAT_BELL, p_tubular}, {"CRYSTAL", ZCAT_BELL, p_crystal}, {"MUSIC BOX", ZCAT_BELL, p_musicbox},
    {"GAMELAN", ZCAT_BELL, p_gamelan},
    {"LASER ZAP", ZCAT_FX, p_laser}, {"NOISE SWEEP", ZCAT_FX, p_sweep}, {"ROBOT TALK", ZCAT_FX, p_robot},
    {"CHIP BLIP", ZCAT_FX, p_blip}, {"INIT VOICE", ZCAT_KEYS, p_init},
};
enum { FACTORY_COUNT = sizeof factory / sizeof factory[0] };

#define KD(drum, field, v) (uint8_t)ZK_DRUM(drum, field), (v)
static const uint8_t k_zarya[] = {END};
static const uint8_t k_tight[] = {KD(ZDR_KICK, ZD_TUNE, 72), KD(ZDR_KICK, ZD_DECAY, 40), KD(ZDR_KICK, ZD_TONE, 84),
    KD(ZDR_SNARE, ZD_DECAY, 42), KD(ZDR_SNARE, ZD_TUNE, 78), KD(ZDR_CHAT, ZD_DECAY, 30), KD(ZDR_OHAT, ZD_DECAY, 40),
    KD(ZDR_CLAP, ZD_DECAY, 40), KD(ZDR_TOM, ZD_DECAY, 45), ZK_REV_SEND, 10, END};
static const uint8_t k_deep[] = {KD(ZDR_KICK, ZD_TUNE, 28), KD(ZDR_KICK, ZD_DECAY, 104), KD(ZDR_KICK, ZD_TONE, 40),
    KD(ZDR_SNARE, ZD_TUNE, 40), KD(ZDR_SNARE, ZD_DECAY, 80), KD(ZDR_CHAT, ZD_TONE, 30), KD(ZDR_OHAT, ZD_TONE, 30),
    KD(ZDR_TOM, ZD_TUNE, 30), KD(ZDR_TOM, ZD_DECAY, 90), ZK_REV_SEND, 40, END};
static const uint8_t k_lofi[] = {KD(ZDR_KICK, ZD_TUNE, 50), KD(ZDR_KICK, ZD_TONE, 20), KD(ZDR_KICK, ZD_DECAY, 55),
    KD(ZDR_SNARE, ZD_TONE, 100), KD(ZDR_SNARE, ZD_TUNE, 30), KD(ZDR_CHAT, ZD_TUNE, 20), KD(ZDR_CHAT, ZD_TONE, 110),
    KD(ZDR_OHAT, ZD_TUNE, 20), KD(ZDR_OHAT, ZD_TONE, 110), KD(ZDR_BELL, ZD_TUNE, 100), KD(ZDR_RIM, ZD_TUNE, 30),
    ZK_REV_SEND, 30, ZK_DLY_SEND, 15, END};
static const factory_t kits[] = {
    {"ZARYA KIT", ZCAT_KIT, k_zarya}, {"TIGHT KIT", ZCAT_KIT, k_tight}, {"DEEP KIT", ZCAT_KIT, k_deep},
    {"LOFI KIT", ZCAT_KIT, k_lofi},
};
enum { KIT_COUNT = sizeof kits / sizeof kits[0] };

unsigned zr_factory_count(int kit) { return kit ? KIT_COUNT : FACTORY_COUNT; }

void zr_patch_init(zr_patch *p, int kit)
{
    zr_zero(p, sizeof *p);
    p->type = kit ? ZR_PATCH_KIT : ZR_PATCH_SYNTH;
    p->category = kit ? ZCAT_KIT : ZCAT_KEYS;
    zr_name_set(p->name, kit ? "INIT KIT" : "INIT VOICE");
    if (kit) {
        for (unsigned i = 0; i < ZK_COUNT; ++i) p->p[i] = zr_kit_desc(i)->def;
        p->p[ZK_DRUM(ZDR_KICK, ZD_LEVEL)] = 120;
        p->p[ZK_DRUM(ZDR_CHAT, ZD_LEVEL)] = 84;
        p->p[ZK_DRUM(ZDR_OHAT, ZD_LEVEL)] = 80;
        p->p[ZK_DRUM(ZDR_OHAT, ZD_DECAY)] = 50;
        p->p[ZK_DRUM(ZDR_CHAT, ZD_DECAY)] = 45;
        return;
    }
    for (unsigned i = 0; i < ZR_PATCH_PARAMS; ++i) p->p[i] = zr_patch_desc(i)->def;
    p->p[ZP_OP(0, ZO_LEVEL)] = 127;
}
void zr_factory_patch(zr_patch *out, unsigned index, int kit)
{
    const factory_t *f = kit ? &kits[index % KIT_COUNT] : &factory[index % FACTORY_COUNT];
    zr_patch_init(out, kit);
    out->category = f->category;
    zr_name_set(out->name, f->name);
    for (const uint8_t *d = f->data; d[0] != END; d += 2)
        if (d[0] < ZR_PATCH_PARAMS) out->p[d[0]] = d[1];
    zr_patch_sanitize(out);
}

/* ---- sound breeding ---- */
void zr_patch_breed(zr_patch *out, const zr_patch *a, const zr_patch *mate, unsigned amount, uint32_t seed)
{
    static const uint8_t groups[7][2] = {{ZP_ALGO, 2}, {ZP_FLT_TYPE, 10}, {ZP_LFO_RATE, 6}, {ZP_OP_BASE, ZO_COUNT},
        {ZP_OP_BASE + ZO_COUNT, ZO_COUNT}, {ZP_OP_BASE + 2 * ZO_COUNT, ZO_COUNT}, {ZP_OP_BASE + 3 * ZO_COUNT, ZO_COUNT}};
    static const uint8_t carriers[8] = {1, 1, 1, 1, 5, 7, 7, 15};
    static const char hex[] = "0123456789ABCDEF";
    uint32_t rng = seed * 2654435761u + 0x9e3779b9u;
    (void)zr_rand(&rng);
    *out = *a;
    if (a->type != ZR_PATCH_SYNTH) return;
    if (amount > 127) amount = 127;
    if (mate && mate->type == ZR_PATCH_SYNTH) /* crossover: whole gene groups come from one parent */
        for (unsigned g = 0; g < 7; ++g)
            if (zr_rand(&rng) & 1) zr_copy(out->p + groups[g][0], mate->p + groups[g][0], groups[g][1]);
    for (unsigned i = 0; i < ZR_PATCH_PARAMS; ++i) {
        const zr_pdesc *d = zr_patch_desc(i);
        uint32_t r = zr_rand(&rng);
        if (i == ZP_LEVEL || i == ZP_TRANSPOSE || i == ZP_MODE || i == ZP_BEND || i == ZP_PAN || i == ZP_GLIDE ||
            i == ZP_RSV || (i >= ZP_M1_VAL && i <= ZP_M4_VAL)) continue;
        if ((r & 255) >= amount) continue;
        int field = i >= ZP_OP_BASE ? (int)((i - ZP_OP_BASE) % ZO_COUNT) : -1, v = out->p[i];
        if (d->fmt == ZF_ENUM || d->fmt == ZF_ONOFF || i == ZP_ALGO || field == ZO_RATIO || field == ZO_WAVE) {
            if (((r >> 8) & 3) == 0) v = (int)(d->min + (r >> 10) % (unsigned)(d->max - d->min + 1)); /* rare jump */
        } else {
            int span = ((d->max - d->min) * (int)amount) / 381 + 1;
            v += (int)((r >> 8) % (unsigned)(2 * span + 1)) - span;
        }
        out->p[i] = (uint8_t)zr_clamp_param(d, v);
    }
    /* keep every child playable: carriers audible, no click-only or sub-audio results */
    unsigned car = carriers[out->p[ZP_ALGO] & 7];
    for (unsigned op = 0; op < 4; ++op) {
        uint8_t *o = out->p + ZP_OP_BASE + op * ZO_COUNT;
        if (!((car >> op) & 1)) continue;
        if (o[ZO_LEVEL] < 104) o[ZO_LEVEL] = (uint8_t)(104 + zr_rand(&rng) % 24);
        if (o[ZO_DEC] < 34 && o[ZO_SUS] < 40) o[ZO_DEC] = 46;
        if (o[ZO_ATK] > 100) o[ZO_ATK] = 100;
        if (o[ZO_WAVE] == ZW_NOISE && op == 0) o[ZO_WAVE] = ZW_SINE;
        if (o[ZO_RATIO] > 8) o[ZO_RATIO] = 1;
    }
    if (out->p[ZP_FLT_TYPE] == ZFLT_LP && out->p[ZP_CUTOFF] < 30 && out->p[ZP_FLT_ENV] <= 70) out->p[ZP_CUTOFF] = 44;
    if (out->p[ZP_FLT_TYPE] == ZFLT_HP && out->p[ZP_CUTOFF] > 44) out->p[ZP_CUTOFF] = 44;
    if (out->p[ZP_FLT_TYPE] == ZFLT_BP && out->p[ZP_CUTOFF] > 100) out->p[ZP_CUTOFF] = 100;
    if (out->p[ZP_FLT_TYPE] == ZFLT_BP && out->p[ZP_CUTOFF] < 45) out->p[ZP_CUTOFF] = 45;
    zr_patch_sanitize(out);
    uint16_t id = zr_crc16(out->p, ZR_PATCH_PARAMS, 0xffff);
    zr_name_set(out->name, "EVO-0000");
    for (unsigned k = 0; k < 4; ++k) out->name[4 + k] = hex[(id >> (12 - 4 * k)) & 15];
}

int zr_patch_valid(const zr_patch *p)
{
    if (p->type > ZR_PATCH_KIT) return 0;
    for (unsigned i = 0; i < ZR_NAME_LEN; ++i) if (p->name[i] < 32 || p->name[i] > 126) return 0;
    for (unsigned i = 0; i < ZR_PATCH_PARAMS; ++i) {
        const zr_pdesc *d = p->type == ZR_PATCH_KIT ? zr_kit_desc(i) : zr_patch_desc(i);
        if (d && (p->p[i] < d->min || p->p[i] > d->max)) return 0;
    }
    return 1;
}
void zr_patch_sanitize(zr_patch *p)
{
    if (p->type > ZR_PATCH_KIT) p->type = ZR_PATCH_SYNTH;
    p->category &= 7;
    for (unsigned i = 0; i < ZR_NAME_LEN; ++i) if (p->name[i] < 32 || p->name[i] > 126) p->name[i] = ' ';
    for (unsigned i = 0; i < ZR_PATCH_PARAMS; ++i) {
        const zr_pdesc *d = p->type == ZR_PATCH_KIT ? zr_kit_desc(i) : zr_patch_desc(i);
        p->p[i] = d ? (uint8_t)zr_clamp_param(d, p->p[i]) : 0;
    }
}
void zr_pattern_sanitize(zr_pattern *p)
{
    if (!p->length || p->length > ZR_STEPS) p->length = 16;
    for (unsigned i = 0; i < ZR_STEPS; ++i) {
        p->steps[i].vel &= 127;
        p->steps[i].gate &= 0x3f;
        p->steps[i].fx &= 0x3f;
    }
}
void zr_globals_sanitize(uint8_t *g)
{
    for (unsigned i = 0; i < ZR_GLOBALS; ++i) {
        const zr_pdesc *d = zr_global_desc(i);
        g[i] = d ? (uint8_t)zr_clamp_param(d, g[i]) : 0;
    }
}

void zr_project_init(zr_project *p)
{
    static const uint8_t first[3] = {1, 11, 24}; /* RUBBER, E-PIANO, SEQ PLUCK */
    zr_zero(p, sizeof *p);
    zr_name_set(p->name, "NEW PROJECT");
    for (unsigned i = 0; i < ZG_COUNT; ++i) p->g[i] = zr_global_desc(i)->def;
    for (unsigned t = 0; t < 3; ++t) zr_factory_patch(&p->patch[t], first[t], 0);
    zr_factory_patch(&p->patch[ZR_DRUM_TRACK], 0, 1);
    for (unsigned s = 0; s < ZR_SCENES; ++s)
        for (unsigned t = 0; t < ZR_TRACKS; ++t) zr_pattern_clear(&p->pat[s][t], 16);
}
/* The demo: four scenes generated from one seed, so the tracks agree. */
void zr_project_demo(zr_project *p)
{
    static const uint8_t styles[3] = {ZGEN_BASS, ZGEN_CHORDS, ZGEN_ARP};
    static const uint8_t bases[3] = {36, 48, 60};
    static const uint8_t density[4] = {60, 80, 100, 50}, complexity[4] = {30, 50, 70, 100};
    zr_project_init(p);
    zr_name_set(p->name, "ZARYA DEMO");
    p->g[ZG_TEMPO] = 78; /* 118 BPM */
    p->g[ZG_SWING] = 4;
    p->g[ZG_ROOT] = 9;
    p->g[ZG_SCALE] = 1;
    p->g[ZG_PUMP] = 50;
    for (unsigned s = 0; s < 4; ++s)
        for (unsigned t = 0; t < ZR_TRACKS; ++t) {
            zr_gen_params gp;
            gp.seed = (uint16_t)(1986 + s / 2);
            gp.root = 9; gp.scale = 1;
            gp.density = density[s]; gp.complexity = complexity[s];
            gp.range = 2; gp.length = 32; gp.variation = (uint8_t)(40 + 20 * s);
            gp.style = t < 3 ? styles[t] : 0;
            gp.base_note = (uint8_t)((t < 3 ? bases[t] : 36) + 9);
            if (s == 3 && t == 2) gp.style = ZGEN_MELODY;
            zr_generate(&p->pat[s][t], &gp, t == ZR_DRUM_TRACK);
        }
}

/* ---- checksums and blobs ---- */
uint16_t zr_crc16(const uint8_t *data, uint32_t bytes, uint16_t crc)
{
    while (bytes--) {
        crc ^= (uint16_t)(*data++ << 8);
        for (unsigned i = 0; i < 8; ++i) crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
uint32_t zr_crc32(const uint8_t *data, uint32_t bytes, uint32_t crc)
{
    crc = ~crc;
    while (bytes--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
uint32_t zr_blob_pack(uint8_t *out, unsigned kind, const void *payload, uint32_t bytes)
{
    uint16_t crc = zr_crc16((const uint8_t *)payload, bytes, 0xffff);
    out[0] = 'Z'; out[1] = 'R'; out[2] = (uint8_t)kind; out[3] = ZR_SCHEMA_VERSION;
    out[4] = (uint8_t)bytes; out[5] = (uint8_t)(bytes >> 8);
    out[6] = (uint8_t)crc; out[7] = (uint8_t)(crc >> 8);
    zr_copy(out + ZR_BLOB_HEADER, payload, bytes);
    return bytes + ZR_BLOB_HEADER;
}
int zr_blob_check(const uint8_t *blob, uint32_t bytes, unsigned kind, uint32_t expect_len)
{
    if (bytes < ZR_BLOB_HEADER) return -1;
    if (blob[0] != 'Z' || blob[1] != 'R') return -2;
    if (blob[2] != kind) return -3;
    if (blob[3] != ZR_SCHEMA_VERSION) return -4;
    uint32_t len = blob[4] | ((uint32_t)blob[5] << 8);
    if (len != bytes - ZR_BLOB_HEADER || (expect_len && len != expect_len)) return -5;
    uint16_t crc = zr_crc16(blob + ZR_BLOB_HEADER, len, 0xffff);
    if (blob[6] != (crc & 255) || blob[7] != (crc >> 8)) return -6;
    return (int)len;
}
