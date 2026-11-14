/* potential_manual.h: the MANUAL tab's page (parameter "manual_page"), which every Potential plugin shares.
 *
 * The page buttons on the MANUAL tab set "manual_page" and the skin shows that page's text (shared/looks/make_looks.py
 * builds the tab from vst/manual.txt). The engine only has to remember the number so the buttons stay lit: it is not
 * part of the sound, the saved state or a preset.
 *
 *     pm_manual_t manual;                                       in the instance (zeroed with it: page 0)
 *     if (pm_set(&s->manual, key, val)) return;                 in set_param()
 *     n = pm_get(&s->manual, key, buf, buf_len); if (n) return n;   in get_param()
 */
#ifndef POTENTIAL_MANUAL_H
#define POTENTIAL_MANUAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int page; } pm_manual_t;

static int pm_set(pm_manual_t *m, const char *key, const char *val) {
    if (strcmp(key, "manual_page")) return 0;
    int v = (int)(atof(val) + 0.5);
    m->page = v < 0 ? 0 : v > 15 ? 15 : v;
    return 1;
}

static int pm_get(const pm_manual_t *m, const char *key, char *buf, int buf_len) {
    if (strcmp(key, "manual_page")) return 0;
    return snprintf(buf, buf_len, "%d", m->page);
}

#endif
