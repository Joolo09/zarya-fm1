/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* ZARYA on-device interface and top-level application glue.
 * Panel: SELECT = track, ALGORITHM = sub-page, KNOB 1-4 = the four dials on
 * screen, PRESETS = sound browser (step cursor on SEQ). */
#include "zr_dsp.h"

void zr_sysex_notify_patch(unsigned track);

#define C_BG ZR_RGB(0x0b, 0x10, 0x18)
#define C_PANEL ZR_RGB(0x11, 0x1a, 0x25)
#define C_PANEL2 ZR_RGB(0x19, 0x24, 0x32)
#define C_LINE ZR_RGB(0x2a, 0x38, 0x4a)
#define C_TEXT ZR_RGB(0xea, 0xf2, 0xfb)
#define C_DIM ZR_RGB(0xa2, 0xb0, 0xc2)
#define C_LIME ZR_RGB(0xcb, 0xf3, 0x6b)
#define C_CYAN ZR_RGB(0x5a, 0xdb, 0xcb)
#define C_CORAL ZR_RGB(0xff, 0x84, 0x6c)
#define C_AMBER ZR_RGB(0xff, 0xc8, 0x57)
#define C_RED ZR_RGB(0xff, 0x4d, 0x5e)
static const uint16_t track_color[ZR_TRACKS] = {C_LIME, C_CYAN, C_CORAL, C_AMBER};

enum { P_HOME, P_SOUND, P_ENV, P_LFO, P_FX, P_SCALE, P_MIX, P_ARP, P_SEQ, P_SAVE, PAGES };
enum { B_NONE, B_PATCH, B_OP, B_GLOBAL, B_TRACK, B_DRUM, B_KIT, B_STEP_NOTE, B_STEP_VEL, B_STEP_GATE, B_STEP_PROB,
       B_STEP_RATCH, B_STEP_CHORD, B_PAT_LEN, B_SEED, B_SLOT, B_MIXVOL, B_MIXDLY, B_MIXREV, B_MIXMUTE, B_CONFIRM,
       B_DEGREE, B_EVO_AMT, B_EVO_MATE, B_EVO_PICK };
enum { SUB_EVOLVE = 7 };
typedef struct { uint8_t kind, id; } bind_t;
typedef struct { const char *title; bind_t k[4]; } sub_t;
#define NB {B_NONE, 0}

static const sub_t s_home[] = {
    {"MACROS", {{B_PATCH, ZP_M1_VAL}, {B_PATCH, ZP_M2_VAL}, {B_PATCH, ZP_M3_VAL}, {B_PATCH, ZP_M4_VAL}}},
    {"SONG", {{B_GLOBAL, ZG_TEMPO}, {B_GLOBAL, ZG_SWING}, {B_GLOBAL, ZG_SCENE}, {B_GLOBAL, ZG_MIDI_OUT}}}};
static const sub_t s_home_kit[] = {
    {"KIT", {{B_KIT, ZK_LEVEL}, {B_KIT, ZK_DLY_SEND}, {B_KIT, ZK_REV_SEND}, {B_KIT, ZK_PAN_WIDTH}}},
    {"SONG", {{B_GLOBAL, ZG_TEMPO}, {B_GLOBAL, ZG_SWING}, {B_GLOBAL, ZG_SCENE}, {B_GLOBAL, ZG_MIDI_OUT}}}};
static const sub_t s_sound[] = {
    {"VOICE", {{B_PATCH, ZP_ALGO}, {B_PATCH, ZP_FEEDBACK}, {B_PATCH, ZP_LEVEL}, {B_PATCH, ZP_PAN}}},
    {"OP PITCH", {{B_OP, ZO_RATIO}, {B_OP, ZO_FINE}, {B_OP, ZO_DETUNE}, {B_OP, ZO_WAVE}}},
    {"OP LEVEL", {{B_OP, ZO_LEVEL}, {B_OP, ZO_VEL}, {B_OP, ZO_KSC}, NB}},
    {"FILTER", {{B_PATCH, ZP_FLT_TYPE}, {B_PATCH, ZP_CUTOFF}, {B_PATCH, ZP_RESO}, {B_PATCH, ZP_FLT_ENV}}},
    {"FILTER MOD", {{B_PATCH, ZP_FLT_KEY}, {B_PATCH, ZP_FLT_VEL}, NB, NB}},
    {"PITCH", {{B_PATCH, ZP_TRANSPOSE}, {B_PATCH, ZP_GLIDE}, {B_PATCH, ZP_MODE}, {B_PATCH, ZP_BEND}}},
    {"SENDS", {{B_PATCH, ZP_DLY_SEND}, {B_PATCH, ZP_REV_SEND}, {B_PATCH, ZP_SPREAD}, NB}},
    {"EVOLVE", {{B_EVO_AMT, 0}, {B_EVO_MATE, 0}, {B_EVO_PICK, 0}, NB}}};
static const sub_t s_sound_kit[] = {
    {"DRUM", {{B_DRUM, ZD_TUNE}, {B_DRUM, ZD_DECAY}, {B_DRUM, ZD_TONE}, {B_DRUM, ZD_LEVEL}}},
    {"KIT", {{B_KIT, ZK_LEVEL}, {B_KIT, ZK_DLY_SEND}, {B_KIT, ZK_REV_SEND}, {B_KIT, ZK_PAN_WIDTH}}}};
static const sub_t s_env[] = {
    {"OP ENV", {{B_OP, ZO_ATK}, {B_OP, ZO_DEC}, {B_OP, ZO_SUS}, {B_OP, ZO_REL}}},
    {"FILTER ENV", {{B_PATCH, ZP_FA}, {B_PATCH, ZP_FD}, {B_PATCH, ZP_FS}, {B_PATCH, ZP_FR}}}};
static const sub_t s_lfo[] = {
    {"LFO", {{B_PATCH, ZP_LFO_RATE}, {B_PATCH, ZP_LFO_SHAPE}, {B_PATCH, ZP_LFO_DEST}, {B_PATCH, ZP_LFO_DEPTH}}},
    {"LFO 2", {{B_PATCH, ZP_LFO_FADE}, {B_PATCH, ZP_LFO_SYNC}, NB, NB}},
    {"MACRO 1+2", {{B_PATCH, ZP_M1_DEST}, {B_PATCH, ZP_M1_AMT}, {B_PATCH, ZP_M2_DEST}, {B_PATCH, ZP_M2_AMT}}},
    {"MACRO 3+4", {{B_PATCH, ZP_M3_DEST}, {B_PATCH, ZP_M3_AMT}, {B_PATCH, ZP_M4_DEST}, {B_PATCH, ZP_M4_AMT}}}};
static const sub_t s_none[] = {{"SYNTH TRACKS ONLY", {NB, NB, NB, NB}}};
static const sub_t s_fx[] = {
    {"DELAY", {{B_GLOBAL, ZG_DLY_TIME}, {B_GLOBAL, ZG_DLY_FB}, {B_GLOBAL, ZG_DLY_TONE}, {B_GLOBAL, ZG_DLY_LEVEL}}},
    {"REVERB", {{B_GLOBAL, ZG_REV_SIZE}, {B_GLOBAL, ZG_REV_DAMP}, {B_GLOBAL, ZG_REV_LEVEL}, NB}},
    {"MASTER", {{B_GLOBAL, ZG_DRIVE}, {B_GLOBAL, ZG_DJ_FILTER}, {B_GLOBAL, ZG_PUMP}, {B_GLOBAL, ZG_PUMP_REL}}}};
static const sub_t s_scale[] = {
    {"SCALE", {{B_GLOBAL, ZG_ROOT}, {B_GLOBAL, ZG_SCALE}, {B_GLOBAL, ZG_SCALE_LOCK}, {B_GLOBAL, ZG_KB_OCT}}},
    {"GENERATE", {{B_SEED, 0}, {B_GLOBAL, ZG_GEN_DENSITY}, {B_GLOBAL, ZG_GEN_COMPLEX}, {B_GLOBAL, ZG_GEN_STYLE}}},
    {"GENERATE 2", {{B_GLOBAL, ZG_GEN_LENGTH}, {B_GLOBAL, ZG_GEN_RANGE}, {B_GLOBAL, ZG_GEN_VARIATION}, NB}},
    {"LIVE", {{B_GLOBAL, ZG_PURE}, {B_GLOBAL, ZG_DUET}, {B_GLOBAL, ZG_DUET_VAR}, {B_DEGREE, 0}}}};
static const sub_t s_mix[] = {
    {"VOLUME", {{B_MIXVOL, 0}, {B_MIXVOL, 1}, {B_MIXVOL, 2}, {B_MIXVOL, 3}}},
    {"DELAY SEND", {{B_MIXDLY, 0}, {B_MIXDLY, 1}, {B_MIXDLY, 2}, {B_MIXDLY, 3}}},
    {"REVERB SEND", {{B_MIXREV, 0}, {B_MIXREV, 1}, {B_MIXREV, 2}, {B_MIXREV, 3}}},
    {"MUTE", {{B_MIXMUTE, 0}, {B_MIXMUTE, 1}, {B_MIXMUTE, 2}, {B_MIXMUTE, 3}}}};
static const sub_t s_arp[] = {
    {"ARP", {{B_TRACK, ZT_ARP_ON}, {B_TRACK, ZT_ARP_MODE}, {B_TRACK, ZT_ARP_RATE}, {B_TRACK, ZT_ARP_OCT}}},
    {"ARP 2", {{B_TRACK, ZT_ARP_GATE}, {B_TRACK, ZT_ARP_HOLD}, NB, NB}}};
static const sub_t s_seq[] = {
    {"STEP", {{B_STEP_NOTE, 0}, {B_STEP_VEL, 0}, {B_STEP_GATE, 0}, {B_STEP_PROB, 0}}},
    {"STEP 2", {{B_STEP_RATCH, 0}, {B_STEP_CHORD, 0}, NB, NB}},
    {"PATTERN", {{B_PAT_LEN, 0}, {B_GLOBAL, ZG_SWING}, {B_GLOBAL, ZG_TEMPO}, {B_GLOBAL, ZG_SCENE}}}};
static const sub_t s_save[] = {
    {"SAVE PRESET", {{B_SLOT, ZR_USER_PRESETS}, NB, NB, NB}},
    {"SAVE PROJECT", {{B_SLOT, ZR_PROJECT_SLOTS}, NB, NB, NB}},
    {"LOAD PROJECT", {{B_SLOT, ZR_PROJECT_SLOTS}, NB, NB, NB}},
    {"SYSTEM", {NB, NB, NB, {B_CONFIRM, 0}}}};
static const char *const page_names[PAGES] = {"HOME", "SOUND", "ENV", "LFO", "FX", "SCALE", "MIX", "ARP", "SEQ", "SAVE"};
static const uint8_t white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static struct {
    const zr_platform *plat;
    uint8_t page, sub[PAGES], op, drum, cursor, last_note, slot, confirm, armed, dirty, undo_valid, undo_track,
        undo_scene, boot_done;
    uint8_t key_note[ZR_KEYS], preset[ZR_TRACKS];
    uint32_t prev_buttons, prev_keys, now, notice_until, last_anim;
    char notice[32];
    zr_pattern undo;
    uint8_t evo_active, evo_track, evo_amt, evo_mate, evo_pick, evo_gen, cap_seq;
    zr_patch evo_orig, evo_parent, evo_cand[5];
} U;
static const char *const roman[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};

unsigned zr_ui_page(void) { return U.page; }
void zr_ui_touch(void) { U.dirty = 1; }
int zr_ui_dirty(void) { int d = U.dirty; U.dirty = 0; return d; }
int zr_screen_dirty(void) { return zr_ui_dirty(); }
void zr_ui_notice(const char *text)
{
    unsigned i = 0;
    for (; i < sizeof U.notice - 1 && text[i]; ++i) U.notice[i] = text[i];
    U.notice[i] = 0;
    U.notice_until = U.now + 1500U;
    U.dirty = 1;
}

static void scpy(char *d, const char *s) { while ((*d++ = *s++)) {} }

/* ---- shared edit paths ---- */
static unsigned sel(void) { return zr_live.sel_track % ZR_TRACKS; }
static int is_kit(unsigned track) { return track == ZR_DRUM_TRACK; }
void zr_set_patch_param(unsigned track, unsigned id, int value, int notify)
{
    if (track >= ZR_TRACKS) return;
    const zr_pdesc *d = is_kit(track) ? zr_kit_desc(id) : zr_patch_desc(id);
    if (!d) return;
    unsigned v = zr_clamp_param(d, value);
    if (zr_proj.patch[track].p[id] == v) return;
    zr_proj.patch[track].p[id] = (uint8_t)v;
    zr_patch_touch(track);
    U.dirty = 1;
    if (notify) zr_sysex_notify_param(0, track, id, v);
}
void zr_set_global(unsigned id, int value, int notify)
{
    const zr_pdesc *d = zr_global_desc(id);
    if (!d || id >= ZG_COUNT) return;
    unsigned v = zr_clamp_param(d, value);
    if (id == ZG_SCENE && zr_status.playing) { zr_live.scene_req = (uint8_t)v; U.dirty = 1; return; }
    if (zr_proj.g[id] == v) return;
    zr_proj.g[id] = (uint8_t)v;
    U.dirty = 1;
    if (notify) zr_sysex_notify_param(1, 0, id, v);
}
void zr_load_patch(unsigned track, const zr_patch *p)
{
    if (track >= ZR_TRACKS) return;
    zr_patch tmp = *p;
    tmp.type = is_kit(track) ? ZR_PATCH_KIT : ZR_PATCH_SYNTH;
    zr_patch_sanitize(&tmp);
    zr_copy(&zr_proj.patch[track], &tmp, sizeof tmp);
    zr_patch_touch(track);
    U.dirty = 1;
    zr_sysex_notify_patch(track);
}

/* ---- EVOLVE: breed sounds. PICK auditions the parent or one of four children;
 * SAVE makes the picked one the parent of the next generation; REC restores
 * the sound you started from. ---- */
static void evo_refresh(void)
{
    zr_patch mate;
    const zr_patch *m = 0;
    if (U.evo_mate) { zr_factory_patch(&mate, U.evo_mate - 1U, 0); m = &mate; }
    U.evo_cand[0] = U.evo_parent;
    for (unsigned k = 1; k < 5; ++k) zr_patch_breed(&U.evo_cand[k], &U.evo_parent, m, U.evo_amt, U.evo_gen * 16U + k);
}
static int evo_begin(void)
{
    unsigned t = zr_live.sel_track % ZR_TRACKS;
    if (t == ZR_DRUM_TRACK) return 0;
    if (U.evo_active && U.evo_track == t) return 1;
    U.evo_orig = U.evo_parent = zr_proj.patch[t];
    U.evo_active = 1; U.evo_track = (uint8_t)t; U.evo_gen = 1; U.evo_pick = 0;
    if (!U.evo_amt) U.evo_amt = 48;
    evo_refresh();
    return 1;
}

/* ---- pages ---- */
static const sub_t *subs(unsigned page, unsigned *count)
{
    int kit = is_kit(sel());
#define RET(a) do { *count = sizeof(a) / sizeof(a[0]); return a; } while (0)
    switch (page) {
    case P_HOME: if (kit) RET(s_home_kit); RET(s_home);
    case P_SOUND: if (kit) RET(s_sound_kit); RET(s_sound);
    case P_ENV: if (kit) RET(s_none); RET(s_env);
    case P_LFO: if (kit) RET(s_none); RET(s_lfo);
    case P_FX: RET(s_fx);
    case P_SCALE: RET(s_scale);
    case P_MIX: RET(s_mix);
    case P_ARP: RET(s_arp);
    case P_SEQ: RET(s_seq);
    default: RET(s_save);
    }
#undef RET
}
static const sub_t *cur_sub(void)
{
    unsigned n;
    const sub_t *s = subs(U.page, &n);
    if (U.sub[U.page] >= n) U.sub[U.page] = 0;
    return &s[U.sub[U.page]];
}
static zr_pattern *cur_pattern(void) { return &zr_proj.pat[zr_proj.g[ZG_SCENE] & 7][sel()]; }
static zr_step *cur_step(void) { return &cur_pattern()->steps[U.cursor % ZR_STEPS]; }
static unsigned send_id(unsigned track, int rev)
{
    return is_kit(track) ? (rev ? ZK_REV_SEND : ZK_DLY_SEND) : (rev ? ZP_REV_SEND : ZP_DLY_SEND);
}
static void note_name(unsigned note, char *out)
{
    const char *n = zr_note_names[note % 12];
    while (*n) *out++ = *n++;
    zr_utoa(note / 12 > 0 ? note / 12 - 1 : 0, out);
}

/* Describe a knob: name, value text, position 0..max. Returns 0 for an unused knob. */
static int bind_info(bind_t b, unsigned knob, const char **name, char text[12], unsigned *value, unsigned *max)
{
    unsigned t = sel();
    const zr_pdesc *d = 0;
    unsigned v = 0;
    static char nm[10];
    text[0] = 0;
    switch (b.kind) {
    case B_PATCH: if (is_kit(t)) return 0; d = zr_patch_desc(b.id); v = zr_proj.patch[t].p[b.id]; break;
    case B_OP: if (is_kit(t)) return 0; d = zr_patch_desc(ZP_OP(U.op, b.id)); v = zr_proj.patch[t].p[ZP_OP(U.op, b.id)]; break;
    case B_DRUM: d = zr_kit_desc(ZK_DRUM(U.drum, b.id)); v = zr_proj.patch[ZR_DRUM_TRACK].p[ZK_DRUM(U.drum, b.id)]; break;
    case B_KIT: d = zr_kit_desc(b.id); v = zr_proj.patch[ZR_DRUM_TRACK].p[b.id]; break;
    case B_GLOBAL: d = zr_global_desc(b.id); v = zr_proj.g[b.id]; break;
    case B_TRACK: d = zr_global_desc(ZG_TRACK(t, b.id)); v = zr_proj.g[ZG_TRACK(t, b.id)]; break;
    case B_MIXVOL: d = zr_global_desc(ZG_TRACK(knob, ZT_VOLUME)); v = zr_proj.g[ZG_TRACK(knob, ZT_VOLUME)]; break;
    case B_MIXMUTE: d = zr_global_desc(ZG_TRACK(knob, ZT_MUTE)); v = zr_proj.g[ZG_TRACK(knob, ZT_MUTE)]; break;
    case B_MIXDLY: case B_MIXREV: {
        unsigned id = send_id(knob, b.kind == B_MIXREV);
        d = is_kit(knob) ? zr_kit_desc(id) : zr_patch_desc(id);
        v = zr_proj.patch[knob].p[id];
        break;
    }
    case B_STEP_NOTE: {
        zr_step *s = cur_step();
        if (is_kit(t)) { *name = "DRUM"; *value = U.drum; *max = 7; scpy(text, zr_drum_names[U.drum]); return 1; }
        *name = "NOTE"; *value = s->note; *max = 127;
        if (s->note) note_name(s->note, text); else scpy(text, "--");
        return 1;
    }
    case B_STEP_VEL: *name = "VELOCITY"; *value = cur_step()->vel; *max = 127; zr_utoa(*value, text); return 1;
    case B_STEP_GATE: {
        unsigned e = zr_gate_eighths[cur_step()->gate & 15];
        *name = "GATE"; *value = cur_step()->gate & 15; *max = 15;
        char *p = zr_utoa(e / 8, text) + zr_strlen(text);
        if (e % 8) { *p++ = '.'; zr_utoa((e % 8) * 125, p); }
        return 1;
    }
    case B_STEP_PROB: *name = "CHANCE"; *value = 7 - (cur_step()->fx & 7); *max = 7;
        zr_utoa(((8 - (cur_step()->fx & 7)) * 100) / 8, text); return 1;
    case B_STEP_RATCH: *name = "RATCHET"; *value = (cur_step()->gate >> 4) & 3; *max = 3; text[0] = 'X'; zr_utoa(*value + 1, text + 1); return 1;
    case B_STEP_CHORD:
        if (is_kit(t)) return 0;
        *name = "CHORD"; *value = (cur_step()->fx >> 3) & 7; *max = 7;
        scpy(text, zr_chord_names[*value]);
        return 1;
    case B_PAT_LEN: *name = "LENGTH"; *value = cur_pattern()->length; *max = 64; zr_utoa(*value, text); return 1;
    case B_SEED: *name = "SEED"; *value = zr_proj.g[ZG_GEN_SEED_H] * 128U + zr_proj.g[ZG_GEN_SEED_L]; *max = 16383; zr_utoa(*value, text); return 1;
    case B_SLOT: *name = "SLOT"; if (U.slot >= b.id) U.slot = 0; *value = U.slot; *max = b.id - 1U; zr_utoa(U.slot + 1U, text); return 1;
    case B_CONFIRM: *name = "FORMAT"; *value = U.confirm; *max = 1; scpy(text, U.confirm ? "YES" : "NO"); return 1;
    case B_DEGREE: *name = "CHORD"; *value = zr_live.degree % 7; *max = 6; scpy(text, roman[*value]); return 1;
    case B_EVO_AMT: if (is_kit(t)) return 0; *name = "MUTATE"; *value = U.evo_amt ? U.evo_amt : 48; *max = 127; zr_utoa(*value, text); return 1;
    case B_EVO_MATE: {
        if (is_kit(t)) return 0;
        *name = "MATE"; *value = U.evo_mate; *max = zr_factory_count(0);
        if (!U.evo_mate) scpy(text, "NONE");
        else { zr_patch m; zr_factory_patch(&m, U.evo_mate - 1U, 0); zr_copy(text, m.name, 9); text[9] = 0; }
        return 1;
    }
    case B_EVO_PICK:
        if (is_kit(t)) return 0;
        *name = "PICK"; *value = U.evo_active && U.evo_track == t ? U.evo_pick : 0; *max = 4;
        if (*value) { scpy(text, "CHILD "); text[6] = (char)('0' + *value); text[7] = 0; } else scpy(text, "PARENT");
        return 1;
    default: return 0;
    }
    if (!d) return 0;
    *name = d->name;
    if (b.kind == B_MIXVOL || b.kind == B_MIXDLY || b.kind == B_MIXREV || b.kind == B_MIXMUTE) {
        nm[0] = 'T'; nm[1] = (char)('1' + knob); nm[2] = 0; *name = nm;
    }
    *value = v - d->min;
    *max = (unsigned)(d->max - d->min);
    zr_format(d, v, text);
    return 1;
}
static void bind_edit(bind_t b, unsigned knob, int delta)
{
    unsigned t = sel();
    zr_step *s = cur_step();
    switch (b.kind) {
    case B_PATCH: if (!is_kit(t)) zr_set_patch_param(t, b.id, zr_proj.patch[t].p[b.id] + delta, 1); break;
    case B_OP: if (!is_kit(t)) zr_set_patch_param(t, ZP_OP(U.op, b.id), zr_proj.patch[t].p[ZP_OP(U.op, b.id)] + delta, 1); break;
    case B_DRUM: zr_set_patch_param(ZR_DRUM_TRACK, ZK_DRUM(U.drum, b.id), zr_proj.patch[ZR_DRUM_TRACK].p[ZK_DRUM(U.drum, b.id)] + delta, 1); break;
    case B_KIT: zr_set_patch_param(ZR_DRUM_TRACK, b.id, zr_proj.patch[ZR_DRUM_TRACK].p[b.id] + delta, 1); break;
    case B_GLOBAL: zr_set_global(b.id, (b.id == ZG_SCENE && zr_live.scene_req != 0xff ? zr_live.scene_req : zr_proj.g[b.id]) + delta, 1); break;
    case B_TRACK: zr_set_global(ZG_TRACK(t, b.id), zr_proj.g[ZG_TRACK(t, b.id)] + delta, 1); break;
    case B_MIXVOL: zr_set_global(ZG_TRACK(knob, ZT_VOLUME), zr_proj.g[ZG_TRACK(knob, ZT_VOLUME)] + delta, 1); break;
    case B_MIXMUTE: zr_set_global(ZG_TRACK(knob, ZT_MUTE), zr_proj.g[ZG_TRACK(knob, ZT_MUTE)] + delta, 1); break;
    case B_MIXDLY: case B_MIXREV: {
        unsigned id = send_id(knob, b.kind == B_MIXREV);
        zr_set_patch_param(knob, id, zr_proj.patch[knob].p[id] + delta, 1);
        break;
    }
    case B_STEP_NOTE:
        if (is_kit(t)) { int d = (int)U.drum + delta; U.drum = (uint8_t)(d < 0 ? 0 : d > 7 ? 7 : d); break; }
        { int n = (s->note ? s->note : U.last_note) + delta; s->note = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n);
          if (!s->vel) s->vel = 100;
          if (!(s->gate & 15)) s->gate |= 3;
          U.last_note = s->note; }
        break;
    case B_STEP_VEL: { int v = s->vel + delta; s->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); break; }
    case B_STEP_GATE: { int g = (s->gate & 15) + delta; s->gate = (uint8_t)((s->gate & 0xf0) | (g < 0 ? 0 : g > 15 ? 15 : g)); break; }
    case B_STEP_PROB: { int p = (s->fx & 7) - delta; s->fx = (uint8_t)((s->fx & 0x38) | (p < 0 ? 0 : p > 7 ? 7 : p)); break; }
    case B_STEP_RATCH: { int r = ((s->gate >> 4) & 3) + delta; s->gate = (uint8_t)((s->gate & 15) | ((r < 0 ? 0 : r > 3 ? 3 : r) << 4)); break; }
    case B_STEP_CHORD: { int c = ((s->fx >> 3) & 7) + delta; s->fx = (uint8_t)((s->fx & 7) | ((c < 0 ? 0 : c > 7 ? 7 : c) << 3)); break; }
    case B_PAT_LEN: { int l = cur_pattern()->length + delta; cur_pattern()->length = (uint8_t)(l < 1 ? 1 : l > 64 ? 64 : l); break; }
    case B_SEED: {
        int v = (int)(zr_proj.g[ZG_GEN_SEED_H] * 128U + zr_proj.g[ZG_GEN_SEED_L]) + delta;
        v = v < 0 ? 0 : v > 16383 ? 16383 : v;
        zr_proj.g[ZG_GEN_SEED_H] = (uint8_t)(v >> 7); zr_proj.g[ZG_GEN_SEED_L] = (uint8_t)(v & 127);
        break;
    }
    case B_SLOT: { int v = (int)U.slot + delta; U.slot = (uint8_t)(v < 0 ? 0 : v >= (int)b.id ? (int)b.id - 1 : v); U.armed = 0; break; }
    case B_CONFIRM: U.confirm = (uint8_t)(delta > 0); U.armed = 0; break;
    case B_DEGREE: { int v = (int)(zr_live.degree % 7) + delta; zr_live.degree = (uint8_t)(v < 0 ? 0 : v > 6 ? 6 : v); break; }
    case B_EVO_AMT: case B_EVO_MATE: case B_EVO_PICK: {
        if (!evo_begin()) break;
        int max = b.kind == B_EVO_AMT ? 127 : b.kind == B_EVO_MATE ? (int)zr_factory_count(0) : 4;
        uint8_t *f = b.kind == B_EVO_AMT ? &U.evo_amt : b.kind == B_EVO_MATE ? &U.evo_mate : &U.evo_pick;
        int v = (int)*f + delta;
        *f = (uint8_t)(v < (b.kind == B_EVO_AMT ? 1 : 0) ? (b.kind == B_EVO_AMT ? 1 : 0) : v > max ? max : v);
        if (b.kind != B_EVO_PICK) evo_refresh();
        zr_load_patch(t, &U.evo_cand[U.evo_pick]);
        break;
    }
    default: break;
    }
    U.dirty = 1;
}

/* ---- actions ---- */
static void browse_preset(int delta)
{
    unsigned t = sel(), kit = is_kit(t), count = zr_factory_count((int)kit), total = count + (kit ? 0 : ZR_USER_PRESETS);
    int idx = (int)U.preset[t];
    zr_patch p;
    for (unsigned guard = 0; guard < total; ++guard) {
        idx += delta > 0 ? 1 : -1;
        if (idx < 0) idx = (int)total - 1;
        if (idx >= (int)total) idx = 0;
        if ((unsigned)idx < count) { zr_factory_patch(&p, (unsigned)idx, (int)kit); break; }
        if (zr_user_presets[(unsigned)idx - count].type != 0xff) { p = zr_user_presets[(unsigned)idx - count]; break; }
    }
    U.preset[t] = (uint8_t)idx;
    zr_load_patch(t, &p);
    zr_ui_notice(zr_category_names[p.category & 7]);
}
static void do_generate(void)
{
    unsigned t = sel();
    zr_gen_params gp;
    static zr_pattern tmp;
    U.undo = *cur_pattern();
    U.undo_valid = 1; U.undo_track = (uint8_t)t; U.undo_scene = zr_proj.g[ZG_SCENE] & 7;
    zr_gen_from_globals(&gp, t);
    zr_generate(&tmp, &gp, is_kit(t));
    zr_copy(cur_pattern(), &tmp, sizeof tmp);
    zr_ui_notice("GENERATED - REC: UNDO");
}
static void do_save(void)
{
    unsigned sub = U.sub[P_SAVE];
    static const char *const ask[4] = {"SAVE AGAIN: WRITE PRESET", "SAVE AGAIN: WRITE PROJECT", "SAVE AGAIN: LOAD PROJECT",
                                       "SAVE AGAIN: ERASE + FORMAT"};
    int r = 0;
    if (sub == 3) {
        if (zr_store_state() == ZS_ABSENT) { zr_ui_notice("NO STORAGE"); return; }
        if (!U.confirm) { zr_ui_notice("SET FORMAT TO YES FIRST"); return; }
    } else if (zr_store_state() != ZS_READY) { zr_ui_notice("STORAGE NOT FORMATTED"); return; }
    if (!U.armed) { U.armed = 1; zr_ui_notice(ask[sub]); return; }
    U.armed = 0;
    switch (sub) {
    case 0:
        if (is_kit(sel())) { zr_ui_notice("KITS LIVE IN PROJECTS"); return; }
        zr_user_presets[U.slot % ZR_USER_PRESETS] = zr_proj.patch[sel()];
        r = zr_user_presets_save();
        break;
    case 1: r = zr_project_save(U.slot); break;
    case 2: r = zr_project_load(U.slot); break;
    default: r = zr_store_format(); U.confirm = 0; break;
    }
    zr_ui_notice(r == 0 ? "DONE" : r == -3 && sub == 2 ? "EMPTY SLOT" : "STORAGE ERROR");
}
static unsigned key_to_note(unsigned key)
{
    int n = (int)ZR_KEY_BASE_NOTE + (int)key + ((int)zr_proj.g[ZG_KB_OCT] - 3) * 12;
    return (unsigned)(n < 0 ? 0 : n > 127 ? 127 : n);
}
static void toggle_step(unsigned index)
{
    zr_pattern *p = cur_pattern();
    zr_step *s = &p->steps[index % ZR_STEPS];
    if (is_kit(sel())) {
        s->note ^= (uint8_t)(1U << U.drum);
        if (s->note && !s->vel) s->vel = 100;
    } else if (s->note) s->note = 0;
    else {
        s->note = U.last_note ? U.last_note : 60;
        if (!s->vel) s->vel = 100;
        if (!(s->gate & 15)) s->gate |= 3;
    }
    U.cursor = (uint8_t)index;
}

void zr_ui_init(const zr_platform *platform)
{
    zr_zero(&U, sizeof U);
    U.plat = platform;
    U.last_note = 60;
    U.dirty = 1;
    for (unsigned i = 0; i < ZR_KEYS; ++i) U.key_note[i] = 0xff;
    U.preset[0] = 1; U.preset[1] = 11; U.preset[2] = 24;
}

void zr_ui_control(const zr_controls *c, uint32_t now_ms)
{
    U.now = now_ms;
    uint32_t down = c->buttons & ~U.prev_buttons, kdown = c->keys & ~U.prev_keys, kup = ~c->keys & U.prev_keys;
    int glo = (c->buttons >> ZR_BTN_GLO) & 1;
    static const uint8_t page_of[ZR_BUTTONS] = {0xff, 0xff, P_FX, P_SCALE, P_ENV, P_LFO, P_SOUND, P_MIX, P_HOME, P_SAVE,
                                                P_ARP, P_SEQ, 0xff, 0xff};
    if (down) U.dirty = 1;
    for (unsigned b = 0; b < ZR_BUTTONS; ++b) {
        if (!(down & (1U << b))) continue;
        if (b == ZR_BTN_PLAY) { zr_post(ZQ_CONTROL, zr_status.playing ? ZE_STOP : ZE_PLAY, 0xff, 0, 0); continue; }
        if (b == ZR_BTN_REC && glo) { /* CAPTURE what was just played */
            U.undo = *cur_pattern();
            U.undo_valid = 1; U.undo_track = (uint8_t)sel(); U.undo_scene = zr_proj.g[ZG_SCENE] & 7;
            zr_post(ZQ_CONTROL, ZE_CAPTURE, sel(), 0, 0);
            continue;
        }
        if (b == ZR_BTN_REC && U.page == P_SOUND && U.sub[P_SOUND] == SUB_EVOLVE && U.evo_active && U.evo_track == sel()) {
            zr_load_patch(sel(), &U.evo_orig);
            U.evo_active = 0; U.evo_pick = 0;
            zr_ui_notice("ORIGINAL RESTORED");
            continue;
        }
        if (b == ZR_BTN_SAVE && U.page == P_SOUND && U.sub[P_SOUND] == SUB_EVOLVE && evo_begin()) {
            U.evo_parent = zr_proj.patch[sel()];
            ++U.evo_gen; U.evo_pick = 0;
            evo_refresh();
            zr_ui_notice("NEXT GENERATION");
            continue;
        }
        if (b == ZR_BTN_REC) {
            if (U.page == P_SCALE && U.undo_valid) {
                zr_copy(&zr_proj.pat[U.undo_scene][U.undo_track], &U.undo, sizeof U.undo);
                U.undo_valid = 0;
                zr_ui_notice("UNDONE");
            } else { zr_live.record = !zr_live.record; zr_live.rec_cursor = U.cursor; }
            continue;
        }
        if (b == ZR_BTN_OCT_DOWN || b == ZR_BTN_OCT_UP) {
            int d = b == ZR_BTN_OCT_UP ? 1 : -1;
            if (U.page == P_SEQ) {
                int pg = U.cursor / 16 + d, pages = (cur_pattern()->length + 15) / 16;
                U.cursor = (uint8_t)((pg < 0 ? 0 : pg >= pages ? pages - 1 : pg) * 16 + U.cursor % 16);
            } else zr_set_global(ZG_KB_OCT, zr_proj.g[ZG_KB_OCT] + d, 1);
            continue;
        }
        if (b == ZR_BTN_SAVE && U.page == P_SCALE) { do_generate(); continue; }
        if (b == ZR_BTN_SAVE && U.page == P_SAVE) { do_save(); continue; }
        unsigned pg = page_of[b];
        if (pg == 0xff) continue;
        if (pg == U.page) { /* second press: cycle the focus of that page */
            if (pg == P_SOUND || pg == P_ENV) { if (is_kit(sel())) U.drum = (U.drum + 1) & 7; else U.op = (U.op + 1) & 3; }
            else if (pg == P_ARP) zr_set_global(ZG_TRACK(sel(), ZT_ARP_ON), !zr_proj.g[ZG_TRACK(sel(), ZT_ARP_ON)], 1);
        }
        U.page = (uint8_t)pg;
        U.armed = 0;
    }
    /* encoders */
    if (c->encoders[ZR_ENC_SELECT]) {
        int t = (int)sel() + c->encoders[ZR_ENC_SELECT];
        zr_post(ZQ_CONTROL, ZE_ALL_OFF, sel(), 0, 0);
        zr_live.sel_track = (uint8_t)(t < 0 ? 0 : t >= ZR_TRACKS ? ZR_TRACKS - 1 : t);
        U.dirty = 1;
    }
    if (c->encoders[ZR_ENC_ALGO]) {
        unsigned n;
        (void)subs(U.page, &n);
        int s = (int)U.sub[U.page] + c->encoders[ZR_ENC_ALGO];
        U.sub[U.page] = (uint8_t)(s < 0 ? 0 : s >= (int)n ? (int)n - 1 : s);
        U.armed = 0;
        U.dirty = 1;
    }
    if (c->encoders[ZR_ENC_PRESETS]) {
        if (U.page == P_SEQ) {
            int s = (int)U.cursor + c->encoders[ZR_ENC_PRESETS], len = cur_pattern()->length;
            U.cursor = (uint8_t)(s < 0 ? 0 : s >= len ? len - 1 : s);
            zr_live.rec_cursor = U.cursor;
            U.dirty = 1;
        } else browse_preset(c->encoders[ZR_ENC_PRESETS]);
    }
    const sub_t *s = cur_sub();
    for (unsigned k = 0; k < 4; ++k) {
        int d = c->encoders[ZR_ENC_K1 + k];
        if (!d) continue;
        if (d > 3 || d < -3) d *= 3; /* fast turns travel further */
        bind_edit(s->k[k], k, d);
    }
    /* keys */
    for (unsigned k = 0; k < ZR_KEYS; ++k) {
        uint32_t bit = 1U << k;
        if (kdown & bit) {
            unsigned w = 0xff;
            for (unsigned i = 0; i < 16; ++i) if (white_keys[i] == k) w = i;
            if (((c->buttons >> ZR_BTN_SCL) & 1) && w < 7) { zr_live.degree = (uint8_t)w; zr_ui_notice(roman[w]); continue; }
            if (glo && w < 4) { zr_set_global(ZG_TRACK(w, ZT_MUTE), !zr_proj.g[ZG_TRACK(w, ZT_MUTE)], 1); continue; }
            if (glo && w >= 4 && w < 12) { zr_set_global(ZG_SCENE, (int)w - 4, 1); if (!zr_status.playing) zr_proj.g[ZG_SCENE] = (uint8_t)(w - 4); continue; }
            if (U.page == P_SEQ && w != 0xff) { toggle_step((U.cursor / 16) * 16 + w); U.dirty = 1; continue; }
            unsigned note = is_kit(sel()) ? k % ZR_DRUMS : key_to_note(k);
            if (is_kit(sel())) U.drum = (uint8_t)note; else U.last_note = (uint8_t)note;
            U.key_note[k] = (uint8_t)note;
            zr_post(ZQ_CONTROL, ZE_NOTE_ON, sel(), note, 100);
            U.dirty = 1;
        } else if ((kup & bit) && U.key_note[k] != 0xff) {
            zr_post(ZQ_CONTROL, ZE_NOTE_OFF, sel(), U.key_note[k], 0);
            U.key_note[k] = 0xff;
        }
    }
    U.prev_buttons = c->buttons;
    U.prev_keys = c->keys;
    if (zr_status.capture_seq != U.cap_seq) {
        U.cap_seq = zr_status.capture_seq;
        zr_ui_notice(zr_status.capture_count ? "CAPTURED - SCL PAGE REC: UNDO" : "NOTHING TO CAPTURE");
    }
    if (U.notice[0] && (int32_t)(now_ms - U.notice_until) >= 0) { U.notice[0] = 0; U.dirty = 1; }
    if (now_ms - U.last_anim >= 50U) { /* meters, playhead */
        U.last_anim = now_ms;
        if (zr_status.playing || zr_status.peak_master || zr_status.voices) U.dirty = 1;
    }
    if (U.plat && U.plat->led) {
        U.plat->led(ZR_BTN_PLAY, zr_status.playing);
        U.plat->led(ZR_BTN_REC, zr_live.record);
        U.plat->led(ZR_BTN_ARP, zr_proj.g[ZG_TRACK(sel(), ZT_ARP_ON)]);
        for (unsigned b = 2; b < 12; ++b) if (page_of[b] != 0xff && b != ZR_BTN_ARP) U.plat->led(b, page_of[b] == U.page);
        const zr_pattern *p = cur_pattern();
        for (unsigned i = 0; i < 16; ++i) {
            unsigned st = (U.cursor / 16) * 16 + i;
            int on = U.page == P_SEQ && st < p->length &&
                     ((p->steps[st].note != 0) ^ (zr_status.playing && zr_status.step[sel()] == st));
            U.plat->led(14 + white_keys[i], on);
        }
    }
}

/* ---- drawing ---- */
static void knob_strip(void)
{
    const sub_t *s = cur_sub();
    uint16_t tc = track_color[sel()];
    zr_fill(0, 172, 240, 68, C_PANEL);
    zr_fill(0, 172, 240, 1, C_LINE);
    for (unsigned k = 0; k < 4; ++k) {
        int cx = 30 + (int)k * 60;
        const char *name; char text[12]; unsigned v, max;
        if (k) zr_fill((int)k * 60, 178, 1, 56, C_LINE);
        if (!bind_info(s->k[k], k, &name, text, &v, &max)) { zr_text_center(cx, 202, "-", 1, C_LINE); continue; }
        zr_text_center(cx, 177, name, 1, C_DIM);
        zr_arc(cx - 1, 205, 13, -512, 2560, 2, C_LINE);
        if (max) zr_arc(cx - 1, 205, 13, 2560 - (int)((v * 3072U) / max), 2560, 2, tc);
        zr_text_center(cx, 227, text, 1, C_TEXT);
    }
}
static void header(void)
{
    char t[12];
    unsigned n;
    (void)subs(U.page, &n);
    zr_fill(0, 0, 240, 18, C_PANEL);
    zr_fill(0, 0, 4, 18, track_color[sel()]);
    int x = zr_text(8, 5, page_names[U.page], 1, C_TEXT);
    x = zr_text(x + 4, 5, cur_sub()->title, 1, track_color[sel()]);
    for (unsigned i = 0; i < n && n > 1; ++i) zr_fill(x + 6 + (int)i * 5, 8, 3, 3, i == U.sub[U.page] ? C_TEXT : C_LINE);
    zr_utoa(zr_proj.g[ZG_TEMPO] + 40U, t);
    zr_text_right(214, 5, t, 1, C_DIM);
    { /* live state: chord degree, PURE root, DUET answering */
        int xr = 190;
        if (zr_status.duet_active && xr - 24 > x + 44) { zr_text_right(xr, 5, "DUET", 1, C_CORAL); xr -= 30; }
        if (zr_status.pure_root < 12 && xr - 12 > x + 44) { zr_text_right(xr, 5, zr_note_names[zr_status.pure_root], 1, C_CYAN); xr -= 18; }
        if (zr_live.degree % 7 && xr - 18 > x + 44) zr_text_right(xr, 5, roman[zr_live.degree % 7], 1, C_AMBER);
    }
    if (zr_live.record) zr_fill(220, 6, 6, 6, C_RED);
    if (zr_status.playing) { zr_fill(230, 5, 2, 8, C_LIME); zr_fill(232, 7, 2, 4, C_LIME); zr_fill(234, 8, 2, 2, C_LIME); }
    else zr_fill(230, 6, 6, 6, C_LINE);
}
static void name_text(int x, int y, const char *name, unsigned scale, uint16_t c)
{
    char t[ZR_NAME_LEN + 1];
    unsigned n = ZR_NAME_LEN;
    zr_copy(t, name, ZR_NAME_LEN);
    while (n && t[n - 1] == ' ') --n;
    t[n] = 0;
    zr_text(x, y, t, scale, c);
}
static void draw_home(void)
{
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        int y = 22 + (int)t * 30;
        int cur = t == sel();
        zr_fill(4, y, 232, 27, cur ? C_PANEL2 : C_PANEL);
        zr_fill(4, y, 3, 27, track_color[t]);
        char n[2] = {(char)('1' + t), 0};
        zr_text(12, y + 6, n, 2, track_color[t]);
        name_text(30, y + 4, zr_proj.patch[t].name, cur ? 2 : 1, zr_proj.g[ZG_TRACK(t, ZT_MUTE)] ? C_LINE : C_TEXT);
        if (!cur) zr_text(30, y + 15, zr_category_names[zr_proj.patch[t].category & 7], 1, C_DIM);
        if (zr_proj.g[ZG_TRACK(t, ZT_MUTE)]) zr_text_right(232, y + 4, "MUTE", 1, C_RED);
        else if (zr_proj.g[ZG_TRACK(t, ZT_ARP_ON)]) zr_text_right(232, y + 4, "ARP", 1, C_DIM);
        zr_fill(30, y + 22, 200, 2, C_LINE);
        zr_fill(30, y + 22, (zr_status.peak[t] * 200) / 127, 2, track_color[t]);
    }
    name_text(6, 146, zr_proj.name, 1, C_DIM);
    for (unsigned s = 0; s < ZR_SCENES; ++s) {
        int x = 100 + (int)s * 17;
        unsigned cur = (zr_proj.g[ZG_SCENE] & 7) == s, next = zr_live.scene_req == s;
        char n[2] = {(char)('A' + s), 0};
        zr_fill(x, 142, 15, 15, cur ? C_TEXT : next ? C_AMBER : C_PANEL2);
        zr_text(x + 5, 146, n, 1, cur || next ? C_BG : C_DIM);
    }
    zr_fill(6, 162, 228, 3, C_PANEL2);
    zr_fill(6, 162, (zr_status.peak_master * 228) / 127, 3, zr_status.clip ? C_RED : C_TEXT);
}
static void draw_algo(void)
{
    /* operator boxes in a row, modulation arrows above, carriers underlined */
    static const uint8_t mods[8][4] = {{2, 4, 8, 0}, {2, 12, 0, 0}, {10, 4, 0, 0}, {6, 0, 8, 0},
                                       {2, 0, 8, 0}, {8, 8, 8, 0}, {0, 0, 8, 0}, {0, 0, 0, 0}};
    static const uint8_t car[8] = {1, 1, 1, 1, 5, 7, 7, 15};
    const uint8_t *p = zr_proj.patch[sel()].p;
    unsigned a = p[ZP_ALGO] & 7;
    uint16_t tc = track_color[sel()];
    for (unsigned i = 0; i < 4; ++i) {
        int x = 22 + (int)i * 54;
        unsigned lv = p[ZP_OP(i, ZO_LEVEL)];
        zr_fill(x, 60, 36, 36, i == U.op ? C_PANEL2 : C_PANEL);
        zr_frame(x, 60, 36, 36, i == U.op ? C_TEXT : C_LINE);
        char n[2] = {(char)('1' + i), 0};
        zr_text(x + 13, 67, n, 2, lv ? C_TEXT : C_LINE);
        zr_fill(x + 4, 88, 28, 3, C_LINE);
        zr_fill(x + 4, 88, (int)(lv * 28) / 127, 3, tc);
        if (car[a] & (1U << i)) { zr_fill(x + 17, 96, 2, 14, tc); zr_fill(x + 12, 110, 12, 3, tc); }
        for (unsigned sI = i + 1; sI < 4; ++sI)
            if (mods[a][i] & (1U << sI)) {
                int xs = 22 + (int)sI * 54 + 18, xt = x + 18, yy = 52 - (int)(sI - i) * 7;
                zr_fill(xt, yy, xs - xt, 1, C_DIM);
                zr_fill(xs, yy, 1, 60 - yy, C_DIM);
                zr_fill(xt, yy, 1, 60 - yy, C_DIM);
                zr_fill(xt - 2, 56, 5, 2, C_DIM);
            }
    }
    if (p[ZP_FEEDBACK]) { zr_frame(196, 46, 14, 14, C_AMBER); zr_text(214, 50, "FB", 1, C_AMBER); }
    zr_text(22, 122, "OUT", 1, C_DIM);
    zr_text(22, 140, "EDIT AGAIN: NEXT OPERATOR", 1, C_LINE);
}
static void draw_evolve(void)
{
    static const uint8_t genes[12] = {ZP_OP(0, ZO_LEVEL), ZP_OP(1, ZO_LEVEL), ZP_OP(2, ZO_LEVEL), ZP_OP(3, ZO_LEVEL),
        ZP_OP(1, ZO_RATIO), ZP_OP(2, ZO_RATIO), ZP_OP(3, ZO_RATIO), ZP_ALGO, ZP_FEEDBACK, ZP_CUTOFF, ZP_RESO, ZP_LFO_DEPTH};
    int live = U.evo_active && U.evo_track == sel();
    uint16_t tc = track_color[sel()];
    for (unsigned k = 0; k < 5; ++k) {
        int x = 6 + (int)k * 46, cur = live && U.evo_pick == k;
        const uint8_t *p = live ? U.evo_cand[k].p : zr_proj.patch[sel()].p;
        zr_fill(x, 26, 43, 92, cur ? C_PANEL2 : C_PANEL);
        if (cur) zr_frame(x, 26, 43, 92, C_TEXT);
        char n[2] = {(char)(k ? '0' + k : 'P'), 0};
        zr_text(x + 17, 30, n, 2, !live && k ? C_LINE : cur ? C_TEXT : C_DIM);
        if (!live && k) continue;
        for (unsigned g = 0; g < 12; ++g) { /* the sound's fingerprint */
            const zr_pdesc *d = zr_patch_desc(genes[g]);
            int hgt = 2 + ((p[genes[g]] - d->min) * 60) / (d->max > d->min ? d->max - d->min : 1);
            zr_fill(x + 3 + (int)g * 3, 114 - hgt, 2, hgt, k && live && p[genes[g]] != U.evo_cand[0].p[genes[g]] ? C_AMBER : tc);
        }
    }
    if (live) { char g[16] = "GENERATION "; zr_utoa(U.evo_gen, g + 11); zr_text(6, 124, g, 1, C_DIM); }
    zr_text(6, 138, "PICK: LISTEN   SAVE: BREED", 1, C_TEXT);
    zr_text(6, 152, live ? "REC: BACK TO THE ORIGINAL" : "TURN PICK TO GROW 4 CHILDREN", 1, C_LINE);
}
static void draw_env(void)
{
    const uint8_t *p = zr_proj.patch[sel()].p;
    unsigned filter = U.sub[P_ENV] == 1;
    unsigned a = filter ? p[ZP_FA] : p[ZP_OP(U.op, ZO_ATK)], d = filter ? p[ZP_FD] : p[ZP_OP(U.op, ZO_DEC)];
    unsigned s = filter ? p[ZP_FS] : p[ZP_OP(U.op, ZO_SUS)], r = filter ? p[ZP_FR] : p[ZP_OP(U.op, ZO_REL)];
    int x0 = 20, base = 150, top = 50, h = base - top;
    int xa = x0 + 4 + (int)(a * 50) / 127, xd = xa + 4 + (int)(d * 50) / 127, xs = xd + 40, xr = xs + 4 + (int)(r * 50) / 127;
    int ys = base - (int)(s * (unsigned)h) / 127;
    uint16_t c = track_color[sel()];
    zr_fill(x0, base, 200, 1, C_LINE);
    zr_line(x0, base, xa, top, c);
    zr_line(xa, top, xd, ys, c);
    zr_line(xd, ys, xs, ys, c);
    zr_line(xs, ys, xr, base, c);
    if (!filter) { char n[5] = {'O', 'P', ' ', (char)('1' + U.op), 0}; zr_text(20, 28, n, 2, C_TEXT); zr_text(90, 34, "ENV AGAIN: NEXT", 1, C_LINE); }
    else zr_text(20, 28, "FILTER", 2, C_TEXT);
}
static void draw_seq(void)
{
    const zr_pattern *p = cur_pattern();
    unsigned t = sel(), page = U.cursor / 16, kit = (unsigned)is_kit(t);
    uint16_t tc = track_color[t];
    char txt[12];
    for (unsigned i = 0; i < 16; ++i) {
        unsigned st = page * 16 + i;
        int x = 8 + (int)(i % 8) * 28, y = 30 + (int)(i / 8) * 44;
        const zr_step *s = &p->steps[st];
        int in = st < p->length, on = in && (kit ? (s->note >> U.drum) & 1 : s->note != 0);
        int play = zr_status.playing && zr_status.step[t] == st;
        zr_fill(x, y, 25, 38, !in ? C_BG : on ? tc : (i % 4 == 0 ? C_PANEL2 : C_PANEL));
        if (!in) { zr_frame(x, y, 25, 38, C_PANEL); continue; }
        if (on && !kit) { note_name(s->note, txt); zr_text(x + 2, y + 4, txt, 1, C_BG); }
        if (on) zr_fill(x + 2, y + 32, (s->vel * 21) / 127, 3, C_BG);
        if (kit && s->note && !on) zr_fill(x + 10, y + 16, 5, 5, C_LINE);
        if (in && (s->gate >> 4) & 3) zr_fill(x + 20, y + 3, 3, 3, on ? C_BG : C_AMBER);
        if (in && (s->fx & 7)) zr_fill(x + 20, y + 8, 3, 3, on ? C_BG : C_CYAN);
        if (st == U.cursor) zr_frame(x - 1, y - 1, 27, 40, C_TEXT);
        if (play) zr_fill(x, y + 39, 25, 2, C_TEXT);
    }
    for (unsigned i = 0; i < 4; ++i)
        zr_fill(8 + (int)i * 20, 124, 17, 4, i == page ? C_TEXT : i * 16 < p->length ? C_LINE : C_PANEL);
    zr_utoa(U.cursor + 1U, txt);
    zr_text_right(232, 122, txt, 1, C_DIM);
    if (kit) zr_text(100, 122, zr_drum_names[U.drum], 1, tc);
    zr_text(8, 136, "WHITE KEYS: STEPS  OCT: PAGE", 1, C_LINE);
    zr_text(8, 148, "PRESETS: CURSOR  REC: RECORD", 1, C_LINE);
}
static void draw_roll(void)
{
    const zr_pattern *p = cur_pattern();
    unsigned t = sel(), lo = 127, hi = 0;
    uint16_t tc = track_color[t];
    zr_fill(8, 24, 224, 112, C_PANEL);
    if (is_kit(t)) { lo = 0; hi = 7; }
    else {
        for (unsigned i = 0; i < p->length; ++i) if (p->steps[i].note) { if (p->steps[i].note < lo) lo = p->steps[i].note; if (p->steps[i].note > hi) hi = p->steps[i].note; }
        if (lo > hi) { lo = 48; hi = 72; }
        if (hi - lo < 12) hi = lo + 12;
    }
    unsigned span = hi - lo + 1, w = 224 / (p->length ? p->length : 16);
    if (!w) w = 1;
    for (unsigned i = 0; i < p->length; ++i) {
        const zr_step *s = &p->steps[i];
        int x = 8 + (int)(i * 224) / p->length;
        if (!(i & 15)) zr_fill(x, 24, 1, 112, C_LINE);
        if (!s->note) continue;
        if (is_kit(t)) { for (unsigned d = 0; d < 8; ++d) if (s->note & (1U << d)) zr_fill(x, 124 - (int)d * 13, (int)w, 8, tc); }
        else zr_fill(x, 130 - (int)((s->note - lo) * 104) / (int)span, (int)w + ((zr_gate_eighths[s->gate & 15] > 8) ? (int)w : 0), 4, tc);
    }
    if (zr_status.playing) zr_fill(8 + (int)(zr_status.step[t] * 224) / (p->length ? p->length : 16), 24, 1, 112, C_TEXT);
    zr_text(8, 142, U.sub[P_SCALE] == 3 ? "HOLD SCL + KEY: CHORD" : "SAVE: GENERATE", 1, C_TEXT);
    zr_text(140, 142, U.undo_valid ? "REC: UNDO" : "", 1, C_AMBER);
    char txt[12];
    zr_text(8, 156, zr_note_names[zr_proj.g[ZG_ROOT] % 12], 1, C_DIM);
    zr_text(26, 156, zr_scale_names[zr_proj.g[ZG_SCALE] % ZR_SCALE_COUNT], 1, C_DIM);
    zr_text_right(232, 156, zr_utoa(p->length, txt), 1, C_DIM);
}
static void draw_mix(void)
{
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        int x = 14 + (int)t * 58;
        unsigned vol = zr_proj.g[ZG_TRACK(t, ZT_VOLUME)], mute = zr_proj.g[ZG_TRACK(t, ZT_MUTE)];
        zr_fill(x, 26, 40, 112, C_PANEL);
        zr_fill(x + 6, 32, 10, 100, C_BG);
        zr_fill(x + 6, 132 - (int)(vol * 100) / 127, 10, (int)(vol * 100) / 127, mute ? C_LINE : track_color[t]);
        zr_fill(x + 24, 32, 6, 100, C_BG);
        zr_fill(x + 24, 132 - (zr_status.peak[t] * 100) / 127, 6, (zr_status.peak[t] * 100) / 127, C_TEXT);
        name_text(x - 6, 144, zr_proj.patch[t].name, 1, mute ? C_RED : C_DIM);
    }
    zr_text(8, 158, "HOLD GLO + KEYS: MUTE / SCENE", 1, C_LINE);
}
static void draw_list(void)
{
    /* generic overview: all sub-pages of the page with their four parameters */
    unsigned n;
    const sub_t *s = subs(U.page, &n);
    int y = 26;
    for (unsigned i = 0; i < n && y < 160; ++i) {
        int cur = i == U.sub[U.page];
        zr_fill(6, y, 228, cur ? 30 : 16, cur ? C_PANEL2 : C_PANEL);
        zr_text(12, y + 4, s[i].title, 1, cur ? track_color[sel()] : C_DIM);
        if (cur)
            for (unsigned k = 0; k < 4; ++k) {
                const char *name; char text[12]; unsigned v, max;
                if (bind_info(s[i].k[k], k, &name, text, &v, &max)) zr_text(12 + (int)k * 56, y + 18, text, 1, C_TEXT);
            }
        y += cur ? 33 : 19;
    }
    if (U.page == P_SAVE) {
        static const char *const st[4] = {"STORAGE: NONE", "STORAGE: NOT FORMATTED", "STORAGE: READY", "STORAGE: ERROR"};
        char t[24] = "CPU ";
        zr_text(8, 146, st[zr_store_state() & 3], 1, zr_store_state() == ZS_READY ? C_LIME : C_AMBER);
        zr_utoa(zr_status.load, t + 4);
        zr_text(8, 158, t, 1, C_DIM);
        zr_text(80, 158, "ZARYA 0.2.0", 1, C_DIM);
    }
}

void zr_ui_draw(uint16_t *buf, unsigned y0, unsigned h)
{
    zr_gfx_begin(buf, y0, h);
    zr_fill(0, 0, 240, 240, C_BG);
    header();
    switch (U.page) {
    case P_HOME: draw_home(); break;
    case P_SOUND:
        if (!is_kit(sel()) && U.sub[P_SOUND] < 3) draw_algo();
        else if (!is_kit(sel()) && U.sub[P_SOUND] == SUB_EVOLVE) draw_evolve();
        else draw_list();
        break;
    case P_ENV: if (is_kit(sel())) draw_list(); else draw_env(); break;
    case P_SEQ: draw_seq(); break;
    case P_SCALE: draw_roll(); break;
    case P_MIX: draw_mix(); break;
    default: draw_list(); break;
    }
    if (U.page == P_SOUND && is_kit(sel())) zr_text(12, 150, zr_drum_names[U.drum], 2, C_AMBER);
    knob_strip();
    if (U.notice[0]) {
        int w = zr_text_width(U.notice, 1) + 16;
        zr_fill(120 - w / 2, 96, w, 24, C_TEXT);
        zr_text_center(120, 105, U.notice, 1, C_BG);
    }
    zr_gfx_begin(0, 0, 0);
}

/* ---- application ---- */
static const zr_platform *app_plat;
void zr_init(const zr_platform *platform)
{
    app_plat = platform;
    zr_engine_init();
    zr_project_demo(&zr_proj);
    zr_store_init(platform);
    zr_sysex_init(platform);
    zr_ui_init(platform);
}
void zr_control(const zr_controls *controls, uint32_t now_ms)
{
    uint8_t m[3];
    if (controls) zr_ui_control(controls, now_ms);
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        unsigned prog = zr_take_program(t);
        if (prog) { zr_patch p; zr_factory_patch(&p, prog - 1, is_kit(t)); zr_load_patch(t, &p); }
    }
    for (unsigned i = 0; i < 8; ++i) {
        unsigned n = zr_midi_out_pop(m);
        if (!n) break;
        if (app_plat && app_plat->midi_send) (void)app_plat->midi_send(m, n);
    }
    zr_sysex_poll(now_ms);
}
void zr_draw(uint16_t *buf, unsigned y0, unsigned h)
{
    if (buf && h && y0 + h <= ZR_SCREEN_H) zr_ui_draw(buf, y0, h);
}
