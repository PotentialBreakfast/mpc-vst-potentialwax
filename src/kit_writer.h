/* kit_writer.h: SAVE KIT's output, an MPC OS 3.10.1 drum program of up to 16 pads, written the way the MPC saves one:
 *   <dir>/<name>.xpm                       gzip of the program (src/kit_template.h, tools/make_kit_template.py)
 *   <dir>/<name>_[ProgramData]/<pad>.wav   the samples, 16-bit stereo 44.1 kHz
 * Standard C only: its own gzip writer (deflate with the fixed Huffman codes and a hash-chain match finder, ample for
 * this very repetitive JSON: about 8.5 MB down to a few hundred KB). Shared by PotentialBreaks and PotentialWax (the
 * same file in both repos); included once, by the plugin's .c file. */
#include <errno.h>
#include "kit_template.h"

/* ---------------- gzip ---------------- */
typedef struct { unsigned char *b; size_t n, cap; uint32_t acc; int nbits; int oom; } kw_out_t;

static void kw_put(kw_out_t *o, unsigned char c) {
    if (o->n == o->cap) {
        size_t nc = o->cap ? o->cap * 2 : 1 << 16;
        unsigned char *nb = realloc(o->b, nc);
        if (!nb) { o->oom = 1; return; }
        o->b = nb; o->cap = nc;
    }
    o->b[o->n++] = c;
}
static void kw_bits(kw_out_t *o, uint32_t v, int n) {   /* LSB first, as deflate packs */
    o->acc |= v << o->nbits; o->nbits += n;
    while (o->nbits >= 8) { kw_put(o, (unsigned char)o->acc); o->acc >>= 8; o->nbits -= 8; }
}
static void kw_huff(kw_out_t *o, uint32_t code, int len) {   /* Huffman codes go MSB first */
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    kw_bits(o, r, len);
}
static void kw_lit(kw_out_t *o, int sym) {   /* the fixed literal/length code (RFC 1951 3.2.6) */
    if (sym < 144) kw_huff(o, 0x30 + sym, 8);
    else if (sym < 256) kw_huff(o, 0x190 + sym - 144, 9);
    else if (sym < 280) kw_huff(o, sym - 256, 7);
    else kw_huff(o, 0xC0 + sym - 280, 8);
}
static const uint16_t KW_LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                                      131, 163, 195, 227, 258};
static const uint8_t KW_LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t KW_DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                      2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t KW_DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void kw_match(kw_out_t *o, int len, int dist) {
    int i = 28; while (KW_LBASE[i] > len) i--;
    kw_lit(o, 257 + i);
    if (KW_LEXT[i]) kw_bits(o, (uint32_t)(len - KW_LBASE[i]), KW_LEXT[i]);
    int d = 29; while (KW_DBASE[d] > dist) d--;
    kw_huff(o, (uint32_t)d, 5);
    if (KW_DEXT[d]) kw_bits(o, (uint32_t)(dist - KW_DBASE[d]), KW_DEXT[d]);
}

static uint32_t kw_crc(uint32_t c, const unsigned char *p, size_t n) {
    static uint32_t t[256]; static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) { uint32_t r = i; for (int k = 0; k < 8; k++) r = r & 1 ? 0xEDB88320u ^ (r >> 1) : r >> 1; t[i] = r; }
        ready = 1;
    }
    c = ~c;
    while (n--) c = t[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

/* gzip of in[0..n) into *out (malloc'd); 0 on success */
static int kw_gzip(const unsigned char *in, size_t n, unsigned char **out, size_t *out_n) {
    enum { WBITS = 15, WSIZE = 1 << WBITS, HBITS = 15, HSIZE = 1 << HBITS, CHAIN = 32, MAXLEN = 258 };
    kw_out_t o = {0};
    static const unsigned char hdr[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
    for (int i = 0; i < 10; i++) kw_put(&o, hdr[i]);
    int32_t *head = malloc(sizeof(int32_t) * HSIZE), *prev = malloc(sizeof(int32_t) * WSIZE);
    if (!head || !prev) { free(head); free(prev); free(o.b); return -1; }
    for (int i = 0; i < HSIZE; i++) head[i] = -1;
    kw_bits(&o, 1, 1); kw_bits(&o, 1, 2);   /* the only block: final, fixed Huffman */
    size_t i = 0;
#define KW_HASH(p) ((((uint32_t)(p)[0] << 10) ^ ((uint32_t)(p)[1] << 5) ^ (uint32_t)(p)[2]) & (HSIZE - 1))
    while (i < n) {
        int best = 0, bdist = 0;
        if (i + 3 <= n) {
            uint32_t h = KW_HASH(in + i);
            int32_t c = head[h];
            size_t lim = n - i < MAXLEN ? n - i : MAXLEN;
            for (int k = 0; k < CHAIN && c >= 0 && i - (size_t)c <= WSIZE - 1; k++) {
                const unsigned char *a = in + c, *b = in + i;
                if (a[best] == b[best]) {
                    size_t l = 0;
                    while (l < lim && a[l] == b[l]) l++;
                    if ((int)l > best) { best = (int)l; bdist = (int)(i - (size_t)c); if (l == lim) break; }
                }
                c = prev[c & (WSIZE - 1)];
            }
            prev[i & (WSIZE - 1)] = head[h]; head[h] = (int32_t)i;
        }
        if (best >= 3) {
            kw_match(&o, best, bdist);
            for (size_t k = 1; k < (size_t)best && i + k + 3 <= n; k++) {   /* index the matched bytes too */
                uint32_t h = KW_HASH(in + i + k);
                prev[(i + k) & (WSIZE - 1)] = head[h]; head[h] = (int32_t)(i + k);
            }
            i += (size_t)best;
        } else kw_lit(&o, in[i++]);
    }
#undef KW_HASH
    kw_lit(&o, 256);
    if (o.nbits) kw_bits(&o, 0, 8 - o.nbits);
    free(head); free(prev);
    uint32_t crc = kw_crc(0, in, n), sz = (uint32_t)n;
    for (int k = 0; k < 4; k++) kw_put(&o, (unsigned char)(crc >> (8 * k)));
    for (int k = 0; k < 4; k++) kw_put(&o, (unsigned char)(sz >> (8 * k)));
    if (o.oom) { free(o.b); return -1; }
    *out = o.b; *out_n = o.n;
    return 0;
}

/* ---------------- the program text ---------------- */
typedef struct { char *b; size_t n, cap; int oom; } kw_str_t;
static void kw_add(kw_str_t *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap : 1 << 20;
        while (s->n + n + 1 > nc) nc *= 2;
        char *nb = realloc(s->b, nc);
        if (!nb) { s->oom = 1; return; }
        s->b = nb; s->cap = nc;
    }
    memcpy(s->b + s->n, t, n); s->n += n; s->b[s->n] = 0;
}
static void kw_adds(kw_str_t *s, const char *t) { kw_add(s, t, strlen(t)); }

/* a template piece with its @FIELD@ markers filled in (NULL value: leave the marker out) */
typedef struct { const char *key, *val; } kw_field_t;
static void kw_fill(kw_str_t *s, const char *tpl, const kw_field_t *f, int nf) {
    const char *p = tpl;
    while (*p) {
        const char *at = strchr(p, '@');
        if (!at) { kw_adds(s, p); return; }
        kw_add(s, p, (size_t)(at - p));
        int done = 0;
        for (int i = 0; i < nf && !done; i++) {
            size_t kl = strlen(f[i].key);
            if (!strncmp(at, f[i].key, kl)) { kw_adds(s, f[i].val); p = at + kl; done = 1; }
        }
        if (!done) { kw_add(s, "@", 1); p = at + 1; }
    }
}

/* names become safe file names and JSON strings: letters, digits, space and - _ ( ) . only */
static void kw_clean(char *dst, size_t n, const char *src) {
    size_t k = 0; int sp = 1;
    for (const char *c = src; *c && k + 1 < n; c++) {
        unsigned char ch = (unsigned char)*c;
        int ok = isalnum(ch) || ch == '-' || ch == '_' || ch == '(' || ch == ')' || ch == '.';
        if (ok) { dst[k++] = (char)ch; sp = 0; }
        else if (!sp) { dst[k++] = ' '; sp = 1; }
    }
    while (k && (dst[k - 1] == ' ' || dst[k - 1] == '.')) k--;
    dst[k] = 0;
    if (!k) snprintf(dst, n, "Break");
}

static int kw_mkdirs(const char *path) {   /* mkdir -p */
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; if (mkdir(tmp, 0777) && errno != EEXIST) return -1; *p = '/'; }
    return mkdir(tmp, 0777) && errno != EEXIST ? -1 : 0;
}

static int kw_write_wav(const char *path, const float *lr, long frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data = (uint32_t)frames * 4;
    unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                           0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t riff = 36 + data;
    for (int k = 0; k < 4; k++) { h[4 + k] = (unsigned char)(riff >> (8 * k)); h[40 + k] = (unsigned char)(data >> (8 * k)); }
    int ok = fwrite(h, 1, 44, f) == 44;
    int16_t buf[2048];
    for (long i = 0; ok && i < frames * 2; ) {
        int m = 0;
        for (; m < 2048 && i < frames * 2; m++, i++) {
            float y = lr[i] * 32767.0f;
            buf[m] = (int16_t)(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : lrintf(y));
        }
        ok = fwrite(buf, 2, (size_t)m, f) == (size_t)m;
    }
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* Pad i's sound for kit_write_cb(): stereo frames (*frames of them; 0 or NULL = an empty pad), made one pad at a time so
 * a kit of long pads never needs them all in memory. done(ctx, i, pad) is called once that pad is written. */
typedef float *(*kit_pad_fn)(void *ctx, int pad, long *frames);
typedef void (*kit_done_fn)(void *ctx, int pad, float *lr);

/* Write the drum program <dir>/<name>.xpm (name made unique: "<name> 2", ...) with pad i = get(ctx, i), at tempo bpm.
 * frames_out[i] gets each pad's length (0 = empty), *out_name the name used. 0 on success, else a message in err. */
static int kit_write_cb(const char *dir, const char *want, double bpm, int npads, kit_pad_fn get, kit_done_fn done,
                        void *ctx, long *frames_out, char *out_name, size_t out_n, char *err, size_t errn) {
    long *frames = frames_out;
    for (int i = 0; i < npads; i++) frames[i] = 0;
    char base[96], name[112], path[1400], pdir[1200];
    kw_clean(base, sizeof base, want);
    if (kw_mkdirs(dir)) { snprintf(err, errn, "CAN'T MAKE THE KIT FOLDER"); return -1; }
    struct stat st;
    snprintf(name, sizeof name, "%s", base);
    for (int k = 2; ; k++) {
        snprintf(path, sizeof path, "%s/%s.xpm", dir, name);
        snprintf(pdir, sizeof pdir, "%s/%s_[ProgramData]", dir, name);
        if (stat(path, &st) && stat(pdir, &st)) break;
        if (k > 99) { snprintf(err, errn, "TOO MANY KITS CALLED %.40s", base); return -1; }
        snprintf(name, sizeof name, "%s %d", base, k);
    }
    if (mkdir(pdir, 0777)) { snprintf(err, errn, "CAN'T WRITE TO THE KIT FOLDER"); return -1; }
    char tempo[32]; snprintf(tempo, sizeof tempo, "%.6g", bpm > 0 ? bpm : 120.0);
    if (!strchr(tempo, '.') && !strchr(tempo, 'e')) strcat(tempo, ".0");
    uint32_t rnd = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)ctx;
    kw_str_t s = {0};
    kw_field_t top[] = {{"@PROGRAM@", NULL}};
    char qname[128]; snprintf(qname, sizeof qname, "\"%s\"", name);
    top[0].val = qname;
    kw_fill(&s, KIT_HEAD, top, 1);
    int used = 0;
    for (int i = 0; i < 128; i++) {
        char sn[160], qn[170], qf[176], end[24], seed[16], rseed[16];
        rnd = rnd * 1664525u + 1013904223u; snprintf(seed, sizeof seed, "%u", 1 + (rnd >> 8) % 999999u);
        rnd = rnd * 1664525u + 1013904223u; snprintf(rseed, sizeof rseed, "%u", 1 + (rnd >> 8) % 999999u);
        float *lr = NULL; long fr = 0;
        if (i < npads) lr = get(ctx, i, &fr);
        int filled = lr && fr > 1;
        if (i < npads) frames[i] = filled ? fr : 0;
        if (filled) {
            snprintf(sn, sizeof sn, "%s %02d", name, i + 1);
            snprintf(path, sizeof path, "%s/%s.wav", pdir, sn);
            int bad = kw_write_wav(path, lr, fr);
            done(ctx, i, lr);
            if (bad) { free(s.b); snprintf(err, errn, "COULDN'T WRITE THE SAMPLES"); return -1; }
            snprintf(qn, sizeof qn, "\"%s\"", sn); snprintf(qf, sizeof qf, "\"%s.wav\"", sn);
            snprintf(end, sizeof end, "%ld", frames[i] - 1);
            used++;
        } else {
            if (i < npads && lr) done(ctx, i, lr); snprintf(qn, sizeof qn, "\"\""); snprintf(qf, sizeof qf, "\"\""); snprintf(end, sizeof end, "0"); }
        kw_field_t f[] = {{"@TEMPO@", filled ? tempo : "120.0"}, {"@NAME@", qn}, {"@FILE@", qf}, {"@END@", end},
                          {"@SEED@", seed}, {"@RSEED@", rseed}};
        if (i) kw_adds(&s, ",\n");
        kw_fill(&s, KIT_PAD, f, 6);
    }
    kw_fill(&s, KIT_MID, NULL, 0);
    int first = 1;
    for (int i = 0; i < npads; i++) {
        if (!(frames[i] > 1)) continue;
        char sn[160], qn[170], qf[176];
        snprintf(sn, sizeof sn, "%s %02d", name, i + 1);
        snprintf(qn, sizeof qn, "\"%s\"", sn); snprintf(qf, sizeof qf, "\"%s.wav\"", sn);
        kw_field_t f[] = {{"@TEMPO@", tempo}, {"@NAME@", qn}, {"@FILE@", qf}};
        if (!first) kw_adds(&s, ",\n");
        kw_fill(&s, KIT_SAMPLE, f, 3);
        first = 0;
    }
    kw_fill(&s, KIT_TAIL, NULL, 0);
    if (s.oom || !used) { free(s.b); snprintf(err, errn, s.oom ? "OUT OF MEMORY" : "NOTHING TO SAVE"); return -1; }
    unsigned char *gz = NULL; size_t gn = 0;
    int r = kw_gzip((const unsigned char *)s.b, s.n, &gz, &gn);
    free(s.b);
    if (r) { snprintf(err, errn, "OUT OF MEMORY"); return -1; }
    char tmp[1100];
    snprintf(path, sizeof path, "%s/%s.xpm", dir, name);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(gz, 1, gn, f) == gn;
    if (f && fclose(f)) ok = 0;
    free(gz);
    if (!ok || rename(tmp, path)) { unlink(tmp); snprintf(err, errn, "COULDN'T WRITE THE PROGRAM"); return -1; }
    snprintf(out_name, out_n, "%s", name);
    return 0;
}

/* the same with every pad already made: pad i = pads[i], frames[i] stereo frames (frames 0 = empty pad) */
typedef struct { float *const *pads; const long *frames; } kw_arr_t;
static float *kw_arr_get(void *ctx, int i, long *frames) { kw_arr_t *a = ctx; *frames = a->frames[i]; return a->pads[i]; }
static void kw_arr_done(void *ctx, int i, float *lr) { (void)ctx; (void)i; (void)lr; }
static int kit_write(const char *dir, const char *want, double bpm, int npads, float *const *pads, const long *frames,
                     char *out_name, size_t out_n, char *err, size_t errn) {
    kw_arr_t a = {pads, frames};
    long fr[128];
    if (npads > 128) npads = 128;
    return kit_write_cb(dir, want, bpm, npads, kw_arr_get, kw_arr_done, &a, fr, out_name, out_n, err, errn);
}
