/* test_theme.c: the LOOK switch of a Potential plugin, on the computer.
 *   gcc -fsanitize=address,undefined -I vst/build -I <kit>/wrapper shared/looks/test_theme.c src/<engine>.c -lm -lpthread -o vst/build/test_theme
 *   vst/build/test_theme "<a copy of the built plugin folder>"     (it rewrites that copy's Plugin Skins/TUI.json)
 * Checks: a "theme" set while loading is ignored; after that each look's TUI.json lands on top and ui.txt names it;
 * a new instance starts on the saved look; "theme" is not in the saved state; no folder = no crash. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"

static int fails;
#define CHECK(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)

static char *slurp(const char *p, long *n) {
    FILE *f = fopen(p, "rb"); if (!f) { *n = -1; return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); rewind(f);
    char *b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t)*n) *n = -1; b[*n < 0 ? 0 : *n] = 0; fclose(f); return b;
}
static int same(const char *a, const char *b) {
    long na, nb; char *x = slurp(a, &na), *y = slurp(b, &nb);
    int r = na >= 0 && na == nb && !memcmp(x, y, na); free(x); free(y); return r;
}
static int look_of(const mpc_engine_t *e, void *s) { char b[32] = ""; e->get_param(s, "theme", b, sizeof b); return atoi(b); }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: test_theme <plugin folder copy>\n"); return 2; }
    const mpc_engine_t *e = mpc_engine();
    const char *names[3] = {"print", "rack", "block"};
    char top[900], sub[900], ui[900], line[64];
    snprintf(top, sizeof top, "%s/Plugin Skins/TUI.json", argv[1]);
    snprintf(ui, sizeof ui, "%s/Plugin Skins/ui.txt", argv[1]);

    void *s = e->create(argv[1]);
    CHECK(s != NULL, "create with the plugin folder");
    CHECK(look_of(e, s) == 0, "starts on Print Plate (ui.txt says print)");
    e->set_param(s, "theme", "1");
    snprintf(sub, sizeof sub, "%s/Plugin Skins/print/TUI.json", argv[1]);
    CHECK(look_of(e, s) == 0 && same(top, sub), "a theme value while loading is ignored");
    usleep(3200 * 1000);
    for (int k = 1; k <= 3; k++) {
        int i = k % 3;   /* rack, block, print */
        snprintf(line, sizeof line, "%d", i);
        e->set_param(s, "theme", line);
        snprintf(sub, sizeof sub, "%s/Plugin Skins/%s/TUI.json", argv[1], names[i]);
        long n; char *u = slurp(ui, &n);
        int named = u && !strncmp(u, names[i], strlen(names[i]));
        free(u);
        printf("%s %s: TUI.json on top, ui.txt names it, the control shows it\n",
               (same(top, sub) && named && look_of(e, s) == i) ? "ok  " : "FAIL", names[i]);
        if (!(same(top, sub) && named && look_of(e, s) == i)) fails++;
        if (i == 1) {
            snprintf(top, sizeof top, "%s/Plugin Skins/Q-Links.json", argv[1]);
            snprintf(sub, sizeof sub, "%s/Plugin Skins/rack/Q-Links.json", argv[1]);
            CHECK(same(top, sub), "Q-Links.json follows the look");
            snprintf(top, sizeof top, "%s/Plugin Skins/TUI.json", argv[1]);
            void *s2 = e->create(argv[1]);
            CHECK(s2 && look_of(e, s2) == 1, "a new instance starts on the saved look");
            if (s2) e->destroy(s2);
        }
    }
    char st[8192] = "";
    e->get_param(s, "state", st, sizeof st);
    CHECK(!strstr(st, "theme"), "the look is not part of the saved state");
    e->set_param(s, "theme", "2");
    e->set_param(s, "state", st);
    CHECK(look_of(e, s) == 2, "loading a state does not change the look");
    e->set_param(s, "theme", "0");
    e->destroy(s);

    void *n = e->create(NULL);
    CHECK(n != NULL, "create without a folder");
    if (n) { usleep(3200 * 1000); e->set_param(n, "theme", "2"); CHECK(look_of(e, n) == 0, "no folder: the switch does nothing"); e->destroy(n); }
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails != 0;
}
