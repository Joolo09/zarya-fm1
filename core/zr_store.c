/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Persistent storage in the FM-1 user Flash partition.
 *
 * Safety rules implemented here:
 *  - Nothing is ever written unless the superblock carries the ZARYA magic.
 *    A foreign partition (stock projects, samples, another firmware) is
 *    reported as ZS_UNFORMATTED and left untouched until the user explicitly
 *    confirms FORMAT on the device.
 *  - Every record has two copies. A save erases and rewrites only the older
 *    copy and commits it by writing its header last, so a power loss at any
 *    point leaves the previous version readable.
 *  - Records carry a schema number and a CRC-32; anything that does not
 *    validate is ignored, never "repaired" by overwriting.
 */
#include "zr_dsp.h"

enum { SECTOR = 4096, HDR = 20, SUPER_MAGIC = 0x5a525953u /* "ZRYS" */, REC_MAGIC = 0x5a524543u /* "ZREC" */ };
enum { PROJECT_SECTORS = 3, FIRST_PROJECT_SECTOR = 5, RECORDS = 2 + ZR_PROJECT_SLOTS,
       TOTAL_SECTORS = FIRST_PROJECT_SECTOR + ZR_PROJECT_SLOTS * 2 * PROJECT_SECTORS };

static const zr_platform *plat;
static uint8_t state = ZS_ABSENT;
static uint32_t writes;
zr_patch zr_user_presets[ZR_USER_PRESETS];

static uint32_t rd32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static unsigned rec_sectors(unsigned record) { return record >= ZREC_PROJECT ? PROJECT_SECTORS : 1; }
static uint32_t rec_offset(unsigned record, unsigned copy)
{
    if (record < ZREC_PROJECT) return (1 + record * 2 + copy) * SECTOR;
    return (FIRST_PROJECT_SECTOR + (record - ZREC_PROJECT) * 2 * PROJECT_SECTORS + copy * PROJECT_SECTORS) * SECTOR;
}
static uint32_t rec_capacity(unsigned record) { return rec_sectors(record) * SECTOR - HDR; }

/* Returns the payload length when the copy is valid, else -1. seq receives its sequence number. */
static int copy_check(unsigned record, unsigned copy, uint32_t *seq)
{
    uint8_t h[HDR], buf[128];
    uint32_t base = rec_offset(record, copy);
    if (plat->flash_read(base, h, HDR) != 0) return -1;
    if (rd32(h) != REC_MAGIC || h[4] != record || h[5] != ZR_SCHEMA_VERSION) return -1;
    uint32_t len = rd32(h + 12);
    if (!len || len > rec_capacity(record)) return -1;
    uint32_t crc = 0;
    for (uint32_t done = 0; done < len;) {
        uint32_t n = len - done < sizeof buf ? len - done : (uint32_t)sizeof buf;
        if (plat->flash_read(base + HDR + done, buf, n) != 0) return -1;
        crc = zr_crc32(buf, n, crc);
        done += n;
    }
    if (crc != rd32(h + 16)) return -1;
    *seq = rd32(h + 8);
    return (int)len;
}
static int super_valid(void)
{
    uint8_t h[12];
    if (plat->flash_read(0, h, sizeof h) != 0) return 0;
    return rd32(h) == SUPER_MAGIC && rd32(h + 4) == 1 && rd32(h + 8) == zr_crc32(h, 8, 0);
}

void zr_store_init(const zr_platform *p)
{
    plat = p;
    state = ZS_ABSENT;
    for (unsigned i = 0; i < ZR_USER_PRESETS; ++i) { zr_zero(&zr_user_presets[i], sizeof(zr_patch)); zr_user_presets[i].type = 0xff; }
    if (!p || !p->flash_size || !p->flash_read || !p->flash_write || !p->flash_erase) return;
    if (p->flash_size() < (uint32_t)TOTAL_SECTORS * SECTOR) return;
    state = super_valid() ? ZS_READY : ZS_UNFORMATTED;
    if (state == ZS_READY) (void)zr_user_presets_load();
}
unsigned zr_store_state(void) { return state; }
uint32_t zr_store_writes(void) { return writes; }

int zr_store_format(void)
{
    if (state == ZS_ABSENT) return -1;
    uint8_t h[12];
    /* Invalidate every record copy first, the superblock last. */
    for (unsigned r = 0; r < RECORDS; ++r)
        for (unsigned c = 0; c < 2; ++c)
            if (plat->flash_erase(rec_offset(r, c)) != 0) { state = ZS_ERROR; return -2; }
    if (plat->flash_erase(0) != 0) { state = ZS_ERROR; return -2; }
    wr32(h, SUPER_MAGIC);
    wr32(h + 4, 1);
    wr32(h + 8, zr_crc32(h, 8, 0));
    if (plat->flash_write(0, h, sizeof h) != 0 || !super_valid()) { state = ZS_ERROR; return -2; }
    ++writes;
    state = ZS_READY;
    for (unsigned i = 0; i < ZR_USER_PRESETS; ++i) zr_user_presets[i].type = 0xff;
    return 0;
}

int zr_store_exists(unsigned record)
{
    uint32_t seq;
    if (state != ZS_READY || record >= RECORDS) return 0;
    return copy_check(record, 0, &seq) > 0 || copy_check(record, 1, &seq) > 0;
}

int zr_store_load(unsigned record, void *data, uint32_t bytes)
{
    if (state != ZS_READY) return -1;
    if (record >= RECORDS) return -2;
    uint32_t s0 = 0, s1 = 0;
    int l0 = copy_check(record, 0, &s0), l1 = copy_check(record, 1, &s1);
    if (l0 < 0 && l1 < 0) return -3;
    unsigned copy = l1 < 0 ? 0 : l0 < 0 ? 1 : (int32_t)(s1 - s0) > 0 ? 1 : 0;
    uint32_t len = (uint32_t)(copy ? l1 : l0);
    if (len != bytes) return -4;
    if (plat->flash_read(rec_offset(record, copy) + HDR, data, len) != 0) return -5;
    return (int)len;
}

int zr_store_save(unsigned record, const void *data, uint32_t bytes)
{
    if (state != ZS_READY) return -1;
    if (record >= RECORDS || !bytes || bytes > rec_capacity(record)) return -2;
    uint32_t s0 = 0, s1 = 0;
    int l0 = copy_check(record, 0, &s0), l1 = copy_check(record, 1, &s1);
    unsigned copy;
    uint32_t seq;
    if (l0 < 0) { copy = 0; seq = l1 < 0 ? 1 : s1 + 1; }
    else if (l1 < 0) { copy = 1; seq = s0 + 1; }
    else if ((int32_t)(s1 - s0) > 0) { copy = 0; seq = s1 + 1; }
    else { copy = 1; seq = s0 + 1; }
    uint32_t base = rec_offset(record, copy);
    for (unsigned s = 0; s < rec_sectors(record); ++s)
        if (plat->flash_erase(base + s * SECTOR) != 0) return -3;
    if (plat->flash_write(base + HDR, data, bytes) != 0) return -4;
    uint8_t h[HDR];
    wr32(h, REC_MAGIC);
    h[4] = (uint8_t)record; h[5] = ZR_SCHEMA_VERSION; h[6] = h[7] = 0;
    wr32(h + 8, seq);
    wr32(h + 12, bytes);
    wr32(h + 16, zr_crc32((const uint8_t *)data, bytes, 0));
    if (plat->flash_write(base, h, HDR) != 0) return -4; /* commit */
    ++writes;
    uint32_t check;
    return copy_check(record, copy, &check) == (int)bytes ? 0 : -5;
}

int zr_user_presets_load(void)
{
    static zr_patch tmp[ZR_USER_PRESETS];
    int r = zr_store_load(ZREC_PRESETS, tmp, sizeof tmp);
    if (r < 0) return r;
    for (unsigned i = 0; i < ZR_USER_PRESETS; ++i) {
        zr_user_presets[i] = tmp[i];
        if (zr_user_presets[i].type != 0xff) zr_patch_sanitize(&zr_user_presets[i]);
    }
    return 0;
}
int zr_user_presets_save(void) { return zr_store_save(ZREC_PRESETS, zr_user_presets, sizeof zr_user_presets); }
int zr_project_save(unsigned slot)
{
    if (slot >= ZR_PROJECT_SLOTS) return -2;
    return zr_store_save(ZREC_PROJECT + slot, &zr_proj, sizeof zr_proj);
}
int zr_project_load(unsigned slot)
{
    static zr_project tmp; /* validated before it replaces the live project */
    if (slot >= ZR_PROJECT_SLOTS) return -2;
    int r = zr_store_load(ZREC_PROJECT + slot, &tmp, sizeof tmp);
    if (r < 0) return r;
    for (unsigned i = 0; i < ZR_NAME_LEN; ++i) if (tmp.name[i] < 32 || tmp.name[i] > 126) tmp.name[i] = ' ';
    zr_globals_sanitize(tmp.g);
    for (unsigned t = 0; t < ZR_TRACKS; ++t) {
        tmp.patch[t].type = t == ZR_DRUM_TRACK ? ZR_PATCH_KIT : ZR_PATCH_SYNTH;
        zr_patch_sanitize(&tmp.patch[t]);
    }
    for (unsigned s = 0; s < ZR_SCENES; ++s)
        for (unsigned t = 0; t < ZR_TRACKS; ++t) zr_pattern_sanitize(&tmp.pat[s][t]);
    zr_post(ZQ_CONTROL, ZE_STOP, 0xff, 0, 0);
    zr_post(ZQ_CONTROL, ZE_PANIC, 0xff, 0, 0);
    zr_copy(&zr_proj, &tmp, sizeof zr_proj);
    for (unsigned t = 0; t < ZR_TRACKS; ++t) zr_patch_touch(t);
    return 0;
}
