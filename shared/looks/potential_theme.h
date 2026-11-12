/* potential_theme.h: the LOOK switch every Potential plugin shares (parameter "theme": Print Plate / Night Rack / Colour Block).
 *
 * MPC reads a plugin's skin once, when the plugin is put on a track or a project loads, and has no way to redraw it
 * afterwards. So the three looks ship side by side (Plugin Skins/print, /rack, /block, built by shared/looks/build_looks.py
 * with the kit's make_skinset.py), and switching copies the chosen look's two small files (TUI.json and the Q-Link maps)
 * to the top of "Plugin Skins". No picture is touched, so a plugin that is running is not disturbed; the new look shows
 * the next time the plugin loads. The method and its limits: mpc-vst-plugins docs/SKIN_VARIANTS.md.
 *
 * - Plugin Skins/ui.txt holds the look's name and is the truth: the choice belongs to the plugin on this MPC, not to a
 *   project or a preset, so "theme" is never saved in the state and a value that arrives while the plugin is still
 *   loading (the first 3 seconds) is ignored.
 * - vst.json needs "defines": {"MODULE_SUBDIR": "\".\""} so create() is told the plugin's own folder.
 * - Without a folder (the computer test, a desktop host) the switch does nothing and the look stays Print Plate.
 *
 * Use, in an engine:
 *     pt_theme_t theme;                                  in the instance
 *     pt_theme_init(&s->theme, data_dir);                in create()
 *     if (pt_theme_set(&s->theme, key, val)) return;     first thing in set_param()
 *     n = pt_theme_get(&s->theme, key, buf, buf_len); if (n) return n;     first thing in get_param()
 */
#ifndef POTENTIAL_THEME_H
#define POTENTIAL_THEME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PT_LOOKS 3
#define PT_GUARD_MS 3000

static const char *const PT_LOOK_DIR[PT_LOOKS] = {"print", "rack", "block"};
static const char *const PT_LOOK_FILES[] = {"TUI.json", "Q-Links.json", "Q-Links - 8by1.json"};

typedef struct {
    char dir[700];        /* "<plugin folder>/Plugin Skins", or "" when the folder is not known */
    int look;             /* 0..PT_LOOKS-1 */
    long long born_ms;
} pt_theme_t;

static long long pt_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* copy src to dst through "<dst>.tmp" and a rename, so MPC never reads half a file. 1 = copied, 0 = not (src missing, disk full...) */
static int pt_copy(const char *src, const char *dst) {
    char tmp[800], buf[4096];
    size_t n;
    int ok = 1;
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    if (snprintf(tmp, sizeof tmp, "%s.tmp", dst) >= (int)sizeof tmp) { fclose(in); return 0; }
    FILE *out = fopen(tmp, "wb");
    if (!out) { fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (ok && rename(tmp, dst) != 0) ok = 0;
    if (!ok) remove(tmp);
    return ok;
}

static void pt_theme_init(pt_theme_t *t, const char *data_dir) {
    char path[800], name[32] = "";
    memset(t, 0, sizeof *t);
    t->born_ms = pt_now_ms();
    if (!data_dir || !data_dir[0]) return;                       /* folder unknown: no switching */
    if (snprintf(t->dir, sizeof t->dir, "%s/Plugin Skins", data_dir) >= (int)sizeof t->dir) { t->dir[0] = 0; return; }
    snprintf(path, sizeof path, "%s/ui.txt", t->dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(name, sizeof name, f)) name[strcspn(name, "\r\n")] = 0;
    fclose(f);
    for (int i = 0; i < PT_LOOKS; i++)
        if (!strcmp(name, PT_LOOK_DIR[i])) t->look = i;
}

/* make look `idx` the one MPC reads next time. 1 = done. */
static int pt_theme_apply(pt_theme_t *t, int idx) {
    char src[800], dst[800];
    int ok = 0;
    if (!t->dir[0] || idx < 0 || idx >= PT_LOOKS) return 0;
    for (unsigned k = 0; k < sizeof PT_LOOK_FILES / sizeof PT_LOOK_FILES[0]; k++) {
        if (snprintf(src, sizeof src, "%s/%s/%s", t->dir, PT_LOOK_DIR[idx], PT_LOOK_FILES[k]) >= (int)sizeof src) continue;
        if (snprintf(dst, sizeof dst, "%s/%s", t->dir, PT_LOOK_FILES[k]) >= (int)sizeof dst) continue;
        if (pt_copy(src, dst)) { if (k == 0) ok = 1; }
        else if (k == 0) return 0;                               /* no TUI.json for that look: leave everything as it is */
    }
    if (ok) {
        snprintf(dst, sizeof dst, "%s/ui.txt", t->dir);
        snprintf(src, sizeof src, "%s/ui.txt.tmp", t->dir);
        FILE *f = fopen(src, "w");
        if (f) {
            int w = fprintf(f, "%s\n", PT_LOOK_DIR[idx]);
            if (fclose(f) == 0 && w > 0) rename(src, dst); else remove(src);
        }
        t->look = idx;
    }
    return ok;
}

/* set_param(): 1 if the key was "theme" (handled here), 0 if it is someone else's */
static int pt_theme_set(pt_theme_t *t, const char *key, const char *val) {
    if (strcmp(key, "theme")) return 0;
    int idx = (int)(atof(val) + 0.5);
    if (idx < 0) idx = 0;
    if (idx >= PT_LOOKS) idx = PT_LOOKS - 1;
    if (pt_now_ms() - t->born_ms < PT_GUARD_MS) return 1;         /* still loading: not a tap */
    if (idx != t->look) pt_theme_apply(t, idx);
    return 1;
}

/* get_param(): the number of characters written for "theme", else 0 */
static int pt_theme_get(const pt_theme_t *t, const char *key, char *buf, int buf_len) {
    if (strcmp(key, "theme")) return 0;
    return snprintf(buf, buf_len, "%d", t->look);
}

#endif
