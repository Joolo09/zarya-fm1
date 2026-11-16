/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Pattern helpers and the deterministic melody / rhythm generator.
 * Same seed + same parameters = same pattern, on every platform. One seed
 * yields one chord progression, shared by the BASS, CHORDS, ARP and MELODY
 * styles, so tracks generated from the same seed fit together. */
#include "zr_dsp.h"

ZR_AUDIO_CODE uint32_t zr_rand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    if (!x) x = 0x6d2b79f5u;
    *state = x;
    return x >> 8;
}
ZR_AUDIO_CODE unsigned zr_scale_snap(unsigned note, unsigned root, unsigned scale)
{
    unsigned mask = zr_scale_mask[scale < ZR_SCALE_COUNT ? scale : ZR_SCALE_COUNT - 1];
    for (unsigned k = 0; k < 12 && k <= note; ++k)
        if (mask & (1U << ((note - k + 120 - (root % 12)) % 12))) return note - k;
    return note;
}
ZR_AUDIO_CODE unsigned zr_scale_shift(unsigned note, unsigned root, unsigned scale, int k)
{
    unsigned mask = zr_scale_mask[scale < ZR_SCALE_COUNT ? scale : ZR_SCALE_COUNT - 1];
    int n = (int)zr_scale_snap(note, root, scale);
    for (; k > 0 && n < 127; --k) do ++n; while (n < 127 && !(mask & (1U << ((unsigned)(n + 120 - (int)(root % 12)) % 12))));
    for (; k < 0 && n > 0; ++k) do --n; while (n > 0 && !(mask & (1U << ((unsigned)(n + 120 - (int)(root % 12)) % 12))));
    return (unsigned)n;
}
void zr_pattern_clear(zr_pattern *p, unsigned length)
{
    zr_zero(p, sizeof *p);
    p->length = (uint8_t)(length && length <= ZR_STEPS ? length : 16);
}

void zr_gen_from_globals(zr_gen_params *gp, unsigned track)
{
    const uint8_t *g = zr_proj.g;
    static const uint8_t base[4] = {60, 36, 60, 48};
    gp->seed = (uint16_t)(g[ZG_GEN_SEED_H] * 128U + g[ZG_GEN_SEED_L]);
    gp->root = g[ZG_ROOT] % 12;
    gp->scale = g[ZG_SCALE] < ZR_SCALE_COUNT ? g[ZG_SCALE] : 0;
    gp->density = g[ZG_GEN_DENSITY];
    gp->complexity = g[ZG_GEN_COMPLEX];
    gp->range = (uint8_t)(g[ZG_GEN_RANGE] % 3 + 1);
    gp->length = (uint8_t)(16 * (g[ZG_GEN_LENGTH] % 4 + 1));
    gp->variation = g[ZG_GEN_VARIATION];
    gp->style = g[ZG_GEN_STYLE] & 3;
    gp->base_note = (uint8_t)(base[gp->style] + gp->root);
    (void)track;
}

/* ---- scale helpers ---- */
typedef struct { uint8_t count, per_octave; uint8_t note[40]; } ladder_t;
static void build_ladder(ladder_t *l, const zr_gen_params *gp, unsigned octaves)
{
    unsigned mask = zr_scale_mask[gp->scale];
    l->count = l->per_octave = 0;
    for (unsigned o = 0; o <= octaves; ++o)
        for (unsigned pc = 0; pc < 12; ++pc) {
            if (!(mask & (1U << pc))) continue;
            unsigned n = gp->base_note + 12 * o + pc;
            if (o == octaves && pc) break; /* top octave: root only */
            if (n > 120 || l->count >= 40) break;
            l->note[l->count++] = (uint8_t)n;
            if (!o) ++l->per_octave;
        }
    if (!l->per_octave) l->per_octave = 1;
}
static const uint8_t progressions[6][4] = {
    {0, 4, 5, 3}, {0, 5, 3, 4}, {0, 3, 4, 4}, {5, 3, 0, 4}, {0, 0, 3, 4}, {0, 2, 3, 4}};
/* Chord quality of a ladder degree: 3 MAJ, 4 MIN, 5 SUS4 (see zr_chord_names). */
static unsigned degree_chord(const zr_gen_params *gp, unsigned semis_from_root)
{
    unsigned mask = zr_scale_mask[gp->scale];
    if (mask & (1U << ((semis_from_root + 4) % 12))) return 3;
    if (mask & (1U << ((semis_from_root + 3) % 12))) return 4;
    if (mask & (1U << ((semis_from_root + 5) % 12))) return 5;
    return 2;
}
static unsigned pick_weighted(uint32_t *rng, uint8_t *w, unsigned n)
{
    unsigned total = 0;
    for (unsigned i = 0; i < n; ++i) total += w[i];
    if (!total) return n;
    unsigned r = zr_rand(rng) % total;
    for (unsigned i = 0; i < n; ++i) {
        if (r < w[i]) { w[i] = 0; return i; }
        r -= w[i];
    }
    return n;
}
static int reflect(int v, int max)
{
    if (max <= 0) return 0;
    while (v < 0 || v > max) v = v < 0 ? -v : 2 * max - v;
    return v;
}

static void gen_drums(zr_pattern *out, const zr_gen_params *gp, uint32_t *rng)
{
    unsigned bars = gp->length / 16;
    for (unsigned b = 0; b < bars; ++b) {
        zr_step *s = &out->steps[b * 16];
        uint8_t vel[16];
        for (unsigned i = 0; i < 16; ++i) vel[i] = 0;
#define HIT(step, drum, v) do { s[step].note |= (uint8_t)(1U << (drum)); if ((v) > vel[step]) vel[step] = (uint8_t)(v); } while (0)
        /* kick */
        if (gp->complexity > 95) {
            static const uint8_t extra[5] = {3, 6, 7, 11, 14};
            HIT(0, ZDR_KICK, 120); HIT(10, ZDR_KICK, 112);
            HIT(extra[zr_rand(rng) % 5], ZDR_KICK, 100);
        } else if (gp->density < 40) { HIT(0, ZDR_KICK, 120); HIT(8, ZDR_KICK, 112); }
        else {
            for (unsigned i = 0; i < 16; i += 4) HIT(i, ZDR_KICK, i ? 112 : 120);
            if (gp->density > 85) {
                static const uint8_t extra[4] = {7, 10, 14, 15};
                for (unsigned k = 0; k < 1 + gp->complexity / 64U; ++k) HIT(extra[zr_rand(rng) % 4], ZDR_KICK, 92);
            }
        }
        /* backbeat */
        HIT(4, ZDR_SNARE, 112); HIT(12, ZDR_SNARE, 116);
        if (gp->density > 70) HIT(12, ZDR_CLAP, 100);
        for (unsigned k = 0; k < gp->complexity / 40U; ++k) {
            unsigned st = (zr_rand(rng) % 8) * 2 + 1;
            if (!(s[st].note & (1U << ZDR_KICK))) HIT(st, ZDR_SNARE, 44);
        }
        /* hats */
        if (gp->density < 30) for (unsigned i = 2; i < 16; i += 4) HIT(i, ZDR_CHAT, 96);
        else if (gp->density < 90) for (unsigned i = 0; i < 16; i += 2) HIT(i, ZDR_CHAT, (i & 2) ? 100 : 76);
        else for (unsigned i = 0; i < 16; ++i) HIT(i, ZDR_CHAT, (i & 1) ? 56 : (i & 2) ? 100 : 80);
        if (gp->density > 45) {
            unsigned st = (zr_rand(rng) & 1) ? 14 : 6;
            s[st].note = (uint8_t)((s[st].note & ~(1U << ZDR_CHAT)) | (1U << ZDR_OHAT));
        }
        /* percussion */
        if (gp->complexity > 50)
            for (unsigned k = 0; k < (gp->complexity - 40U) / 30U; ++k) {
                static const uint8_t perc[3] = {ZDR_RIM, ZDR_BELL, ZDR_TOM};
                HIT((zr_rand(rng) % 16) | 1, perc[zr_rand(rng) % 3], 84);
            }
        /* fill at the end of the phrase */
        if (b == bars - 1 && gp->variation > 40 && (bars > 1 || gp->variation > 100)) {
            HIT(13, ZDR_SNARE, 80); HIT(14, ZDR_SNARE, 96); HIT(15, ZDR_SNARE, 110);
            if (gp->variation > 80) { HIT(14, ZDR_TOM, 100); s[15].gate = (uint8_t)(1U << 4); }
        }
#undef HIT
        for (unsigned i = 0; i < 16; ++i) s[i].vel = s[i].note ? vel[i] : 0;
    }
}

void zr_generate(zr_pattern *out, const zr_gen_params *gp_in, int drums)
{
    zr_gen_params gp = *gp_in;
    if (gp.length != 16 && gp.length != 32 && gp.length != 48 && gp.length != 64) gp.length = 16;
    if (gp.scale >= ZR_SCALE_COUNT) gp.scale = 0;
    if (gp.range < 1 || gp.range > 3) gp.range = 1;
    zr_pattern_clear(out, gp.length);
    uint32_t prog_rng = (uint32_t)gp.seed * 2654435761u + 0x9e3779b9u;
    uint32_t rng = prog_rng ^ ((uint32_t)(drums ? 7 : gp.style) * 0x85ebca6bu) ^ 0x1b873593u;
    (void)zr_rand(&rng);
    const uint8_t *prog = progressions[zr_rand(&prog_rng) % 6];
    if (drums) { gen_drums(out, &gp, &rng); return; }

    ladder_t lad;
    build_ladder(&lad, &gp, gp.range);
    if (!lad.count) return;
    unsigned bars = gp.length / 16;
    uint8_t motif_mask[2][16], motif_deg[2][16];
    int degree = 0;

    for (unsigned b = 0; b < bars; ++b) {
        zr_step *s = &out->steps[b * 16];
        unsigned chord_deg = prog[b % 4] % lad.per_octave; /* ladder index of the bar's chord root */
        unsigned motif = (b == 2 && bars > 2) ? 1 : 0;
        int fresh = b == 0 || (b == 2 && bars > 2);

        if (gp.style == ZGEN_CHORDS) {
            unsigned hits = 1 + (gp.density * 3U) / 128;
            static const uint8_t pos[4][4] = {{0, 0, 0, 0}, {0, 8, 0, 0}, {0, 6, 12, 0}, {0, 4, 8, 12}};
            unsigned root = lad.note[chord_deg];
            unsigned chord = degree_chord(&gp, (root - gp.base_note) % 12);
            if (gp.complexity > 80 && chord == 3) chord = 6;
            else if (gp.complexity > 80 && chord == 4) chord = 7;
            for (unsigned k = 0; k < hits; ++k) {
                unsigned st = pos[hits - 1][k];
                if (gp.complexity > 60 && k && (zr_rand(&rng) & 1)) st = (st + 15) & 15; /* push */
                s[st].note = (uint8_t)root;
                s[st].vel = (uint8_t)(k ? 88 : 104);
                s[st].gate = (uint8_t)(hits == 1 ? 12 : hits == 2 ? 10 : 9);
                s[st].fx = (uint8_t)(chord << 3);
            }
            continue;
        }
        if (gp.style == ZGEN_ARP) {
            /* chord tones of the bar: ladder degrees root, +2, +4, then up in octaves */
            unsigned shape = zr_rand(&prog_rng) % 4, tones = 3 + gp.complexity / 43U, n = 0;
            for (unsigned i = 0; i < 16; ++i) {
                unsigned step = gp.density > 100 ? 1U : 2U;
                if (i % step) continue;
                if (gp.density < 60 && (zr_rand(&rng) % 4) == 0) { ++n; continue; }
                unsigned k = n % tones, span = tones > 1 ? 2 * tones - 2 : 1;
                if (shape == 1) k = tones - 1 - k;
                else if (shape == 2) { unsigned x = n % span; k = x < tones ? x : span - x; }
                else if (shape == 3) k = (n * 2) % tones;
                unsigned idx = chord_deg + k * 2;
                if (idx >= lad.count) idx = idx % lad.count;
                s[i].note = lad.note[idx];
                s[i].vel = (uint8_t)((i & 3) ? 84 : 108);
                s[i].gate = (uint8_t)(step == 1 ? 3 : 5);
                ++n;
            }
            continue;
        }

        /* MELODY and BASS share the motif machinery */
        if (fresh) {
            uint8_t w[16];
            unsigned hits = 2 + (gp.density * 12U) / 127;
            if (gp.style == ZGEN_BASS) hits = 1 + (gp.density * 9U) / 127;
            for (unsigned i = 0; i < 16; ++i) {
                motif_mask[motif][i] = 0;
                w[i] = (uint8_t)(!(i & 3) ? 9 : !(i & 1) ? 5 : 1 + gp.complexity / 14U);
            }
            motif_mask[motif][0] = 1;
            w[0] = 0;
            for (unsigned k = 1; k < hits; ++k) {
                unsigned st = pick_weighted(&rng, w, 16);
                if (st < 16) motif_mask[motif][st] = 1;
            }
            int d = motif ? 2 : 0;
            for (unsigned i = 0; i < 16; ++i) {
                if (!motif_mask[motif][i]) { motif_deg[motif][i] = 0; continue; }
                if (gp.style == ZGEN_BASS) {
                    static const int8_t bass_moves[8] = {0, 0, 0, 4, 4, 7, 2, -1};
                    unsigned pool = 3 + (gp.complexity * 5U) / 128;
                    d = bass_moves[zr_rand(&rng) % pool];
                    if (!i) d = 0;
                } else {
                    unsigned r = zr_rand(&rng) % 128;
                    int step = (zr_rand(&rng) & 1) ? 1 : -1;
                    if (r < gp.complexity / 2U) step *= 2 + (int)(zr_rand(&rng) % 3);
                    else if (r > 112) step = 0;
                    d += step;
                    /* strong beats lean on chord tones when the line is simple */
                    if (!(i & 3) && (zr_rand(&rng) % 128) > gp.complexity && (d % 2)) d -= 1;
                }
                motif_deg[motif][i] = (uint8_t)(d + 64);
            }
        }
        int top = (int)lad.count - 1;
        for (unsigned i = 0; i < 16; ++i) {
            if (!motif_mask[motif][i]) continue;
            int d = (int)motif_deg[motif][i] - 64;
            if (!fresh && (zr_rand(&rng) % 128) < gp.variation / 2U) {
                if (zr_rand(&rng) & 1) d += (zr_rand(&rng) & 1) ? 1 : -1;
                else if (i && gp.variation > 64) continue; /* drop the note */
            }
            degree = reflect((int)chord_deg + d, top);
            unsigned next = 16;
            for (unsigned j = i + 1; j < 16; ++j) if (motif_mask[motif][j]) { next = j; break; }
            unsigned gap = next - i;
            s[i].note = lad.note[degree];
            s[i].vel = (uint8_t)((!(i & 3) ? 108 : !(i & 1) ? 94 : 78) + (zr_rand(&rng) % 9));
            s[i].gate = (uint8_t)(gp.style == ZGEN_BASS ? (gap > 2 ? 7 : 3)
                                  : gap >= 4 && !(zr_rand(&rng) % 3) ? 9 : gap >= 2 ? 5 + (zr_rand(&rng) % 3) : 3);
            if (gp.complexity > 80 && (i & 1)) {
                unsigned r = zr_rand(&rng) % 12;
                if (!r) s[i].gate |= 1U << 4;        /* ratchet x2 */
                else if (r == 1) s[i].fx = 2;        /* skips a quarter of the time */
            }
        }
        /* resolve the phrase on the tonic */
        if (b == bars - 1 && gp.style == ZGEN_MELODY) {
            for (int i = 15; i >= 0; --i)
                if (s[i].note) { s[i].note = lad.note[reflect((degree / lad.per_octave) * lad.per_octave, top)]; break; }
        }
    }
}
