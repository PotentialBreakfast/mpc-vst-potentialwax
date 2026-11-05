/* PotentialWax (PotentialBreaks): a melodic sampler for MPC OS standalone devices, in the spirit of Serato Sample.
 *
 *  - Finds songs on the MPC's drives: a "MELODIC LIBRARY" folder at the top of a drive, any top-level folder with
 *    "melodic", "wax", "flip" or "songs" in its name, and folders listed in /media/<drive>/PotentialWax/folders.txt.
 *    Every sub-folder is a library (plus "ALL"). MP3, WAV and FLAC; the first 6 minutes of a file are used.
 *  - RANDOM / PREV / NEXT / LIBRARY pick a song. It is analysed for tempo (or the "NNbpm" in its name) and key.
 *  - 16 cues are found by themselves on phrases, bars or hits; FIND CUES rolls a new set; NUDGE moves the last pad's cue.
 *  - PADS mode: each pad plays from its cue (while held, to the next cue, or for 1 or 2 bars).
 *    KEYS mode: the last cue played, chromatically from the ROOT note.
 *  - TEMPO "Match MPC" follows the MPC tempo; KEY moves the song to the chosen key; PITCH +/-12. "Keep Tempo" does both
 *    without changing the other (a WSOLA time-stretch with resampled grains); "Vinyl" changes speed and pitch together.
 *  - SAVE PADS writes the 16 chops, as heard, as WAV files (PotentialWax/<song>/ on the song's drive).
 * 44.1 kHz, interleaved int16 stereo; render() may be asked for 1..128 frames (SAMPLE_ACCURATE).
 * set_param, midi and render are never called at the same time for one instance (the wrapper's lock), so the audio-side
 * state (voices, cues, pad order) is changed directly from set_param; only the worker thread needs the hand-over locks. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO_WIDE
#include "vendor/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_WCHAR
#include "vendor/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "vendor/dr_mp3.h"

#define FS 44100
#define NPADS 16
#define NVOICES 8
#define MIN_FILE_SEC 1.8       /* shorter files are one-shots: skipped (a 1-bar loop at 130 BPM is 1.85 s) */
#define MAX_FILE_SEC 360.0     /* never use more than the first 6 minutes of a file (memory: 63 MB) */
#define HOP 441                /* 10 ms analysis hop */
#define DM 8                   /* decimation of the stretcher's search copy */

/* ---------------- parameters (same order as vst/params.json) ---------------- */
enum {
    P_RANDOM, P_PREV, P_NEXT, P_LIB_NEXT,
    P_LIB_NAME, P_TRACK_NAME, P_INFO, P_STATUS,
    P_MODE, P_FIND_CUES, P_CUE_MODE, P_PLAY_MODE,
    P_TEMPO, P_KEY, P_PITCH, P_PITCH_MODE, P_CHOKE, P_VOLUME, P_RELEASE,
    P_NUDGE_BACK, P_NUDGE_FWD, P_PLAY_SONG, P_STOP, P_SAVE_PADS, P_LEARN, P_BASE_NOTE, P_ROOT_NOTE, P_RESCAN,
    P_PICK1, P_PICK10 = P_PICK1 + 9, P_PAGE_UP, P_PAGE_DOWN, P_BROWSE_INFO, P_BROWSE_LIB,
    P_TEMPO_X2, P_TEMPO_HALF,
    NP
};
#define NROWS 10   /* BROWSE: track names per page */
static const char *const PKEY[NP] = {
    "random", "prev", "next", "lib_next",
    "lib_name", "track_name", "info", "status",
    "mode", "find_cues", "cue_mode", "play_mode",
    "tempo", "key", "pitch", "pitch_mode", "choke", "volume", "release",
    "nudge_back", "nudge_fwd", "play_song", "stop", "save_pads", "learn", "base_note", "root_note", "rescan",
    "pick_1", "pick_2", "pick_3", "pick_4", "pick_5", "pick_6", "pick_7", "pick_8", "pick_9", "pick_10",
    "page_up", "page_down", "browse_info", "browse_lib",
    "tempo_x2", "tempo_half",
};
static int is_trigger(int i) {
    return i == P_RANDOM || i == P_PREV || i == P_NEXT || i == P_LIB_NEXT || i == P_FIND_CUES || i == P_NUDGE_BACK ||
           i == P_NUDGE_FWD || i == P_PLAY_SONG || i == P_STOP || i == P_SAVE_PADS || i == P_LEARN || i == P_RESCAN ||
           i == P_PAGE_UP || i == P_PAGE_DOWN || i == P_BROWSE_LIB || i == P_TEMPO_X2 || i == P_TEMPO_HALF;
}
enum { MODE_PADS, MODE_KEYS };
enum { CUE_PHRASES, CUE_BARS, CUE_HITS };
enum { PLAY_HOLD, PLAY_TO_NEXT, PLAY_1BAR, PLAY_2BARS };
enum { PM_KEEP_TEMPO, PM_VINYL };

static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* ---------------- library ---------------- */
typedef struct { char *path; const char *name; int lib; } entry_t;
typedef struct { char name[96]; int count; } lib_t;
#define MAX_LIBS 48

typedef struct {
    entry_t *e; int n, cap;
    lib_t lib[MAX_LIBS]; int nlib;      /* lib[0] = ALL */
    char root_drive[256];               /* fallback drive for SAVE PADS */
} library_t;

static const char *media_root(void) {   /* /media on the MPC; PWAX_MEDIA_ROOT points elsewhere for tests on a computer */
    const char *r = getenv("PWAX_MEDIA_ROOT");
    return r && *r ? r : "/media";
}

static int has_ext(const char *n, const char *ext) {
    size_t a = strlen(n), b = strlen(ext);
    return a > b && !strcasecmp(n + a - b, ext);
}
static int audio_file(const char *n) {
    return n[0] != '.' && (has_ext(n, ".wav") || has_ext(n, ".flac") || has_ext(n, ".mp3"));
}
/* a 16-bit stereo WAV at 44.1 kHz is 176 kB per second; an MP3 ~16 kB: skip what can't be 8 s long */
static int big_enough(const char *n, off_t size) { return size > (has_ext(n, ".mp3") ? 60000 : 300000); }

static void lib_add(library_t *L, const char *path, int lib) {
    if (L->n == L->cap) {
        int nc = L->cap ? L->cap * 2 : 1024;
        entry_t *ne = realloc(L->e, (size_t)nc * sizeof *ne);
        if (!ne) return;
        L->e = ne; L->cap = nc;
    }
    entry_t *x = &L->e[L->n++];
    x->path = strdup(path);
    x->lib = lib;
    const char *s = strrchr(x->path, '/');
    x->name = s ? s + 1 : x->path;
}

static void scan_dir(library_t *L, const char *dir, int lib, int depth) {
    if (depth > 4) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    char p[1024];
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        struct stat st;
        if (lstat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) scan_dir(L, p, lib, depth + 1);
        else if (S_ISREG(st.st_mode) && audio_file(de->d_name) && big_enough(de->d_name, st.st_size)) lib_add(L, p, lib);
    }
    closedir(d);
}

static int cmp_entry(const void *a, const void *b) {
    const entry_t *x = a, *y = b;
    if (x->lib != y->lib) return x->lib - y->lib;
    return strcasecmp(x->path, y->path);
}
static int cmp_str(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

#define MAX_ROOTS 32
#define LIST_DIR "PotentialWax"
#define LIST_FILE "folders.txt"

typedef struct { char path[512]; char real[512]; } root_t;

static int add_root(root_t *R, int *nr, const char *path) {
    if (*nr >= MAX_ROOTS) return 0;
    struct stat st;
    if (stat(path, &st) || !S_ISDIR(st.st_mode)) return 0;
    char rp[4096], real[512];   /* realpath() needs a PATH_MAX buffer */
    snprintf(real, sizeof real, "%s", realpath(path, rp) ? rp : path);
    size_t rl = strlen(real);
    for (int i = 0; i < *nr; i++) {   /* the same folder, or one inside / around one already listed */
        size_t ol = strlen(R[i].real);
        if (!strcmp(R[i].real, real)) return 0;
        if (rl > ol && !strncmp(real, R[i].real, ol) && real[ol] == '/') return 0;
        if (ol > rl && !strncmp(R[i].real, real, rl) && R[i].real[rl] == '/') {
            snprintf(R[i].path, sizeof R[i].path, "%s", path);
            snprintf(R[i].real, sizeof R[i].real, "%s", real);
            return 1;
        }
    }
    snprintf(R[*nr].path, sizeof R[0].path, "%s", path);
    snprintf(R[*nr].real, sizeof R[0].real, "%s", real);
    (*nr)++;
    return 1;
}

static void read_list(root_t *R, int *nr, const char *drive) {
    char p[600];
    snprintf(p, sizeof p, "%s/" LIST_DIR "/" LIST_FILE, drive);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char *t = line;
        while (*t == ' ' || *t == '\t' || *t == '"') t++;
        size_t n = strlen(t);
        while (n && (t[n - 1] == '\n' || t[n - 1] == '\r' || t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '"' ||
                     t[n - 1] == '/')) t[--n] = 0;
        if (!n || *t == '#') continue;
        char q[600];
        if (*t == '/') snprintf(q, sizeof q, "%s", t);
        else snprintf(q, sizeof q, "%s/%s", drive, t);
        add_root(R, nr, q);
    }
    fclose(f);
}

static const char LIST_TEMPLATE[] =
    "# PotentialWax - your song folders\n"
    "#\n"
    "# Found by itself, nothing to type:\n"
    "#   - a folder called MELODIC LIBRARY at the top of any drive\n"
    "#   - any folder at the top of a drive with MELODIC, WAX, FLIP or SONGS in its name\n"
    "#\n"
    "# Anything else: put one folder per line below, then tap RESCAN in the plugin.\n"
    "# A full path (/media/SDMPC/Soul Records) or a folder on this drive (Soul Records).\n"
    "# Each sub-folder becomes a library you can step through with LIBRARY.\n"
    "# MP3, WAV and FLAC files of 8 seconds or more are used.\n"
    "\n";

static int is_internal(const char *d) { return !strncmp(d, "az01", 4) || !strncmp(d, "acvs", 4); }

/* the first drive that takes a file gets folders.txt with the instructions in it (once; never overwritten) */
static void write_list_template(char drives[][256], int nd, const char *prefer) {
    const char *M = media_root();
    for (int i = 0; i < nd; i++) {
        char p[600];
        snprintf(p, sizeof p, "%s/%s/" LIST_DIR "/" LIST_FILE, M, drives[i]);
        if (!access(p, F_OK)) return;
    }
    if (prefer && prefer[0]) {
        char d[600], p[600];
        snprintf(d, sizeof d, "%s/" LIST_DIR, prefer);
        mkdir(d, 0777);
        snprintf(p, sizeof p, "%s/" LIST_FILE, d);
        FILE *f = fopen(p, "w");
        if (f) { fputs(LIST_TEMPLATE, f); fclose(f); return; }
    }
    for (int i = 0; i < nd; i++) {
        if (is_internal(drives[i])) continue;
        char d[600], p[600];
        snprintf(d, sizeof d, "%s/%s/" LIST_DIR, M, drives[i]);
        mkdir(d, 0777);
        snprintf(p, sizeof p, "%s/" LIST_FILE, d);
        FILE *f = fopen(p, "w");
        if (!f) continue;
        fputs(LIST_TEMPLATE, f);
        fclose(f);
        return;
    }
}

static int wax_folder_name(const char *n) {
    return strcasestr(n, "melodic") || strcasestr(n, "wax") || strcasestr(n, "flip") || strcasestr(n, "songs");
}

/* "/media/SDMPC/Soul/x.mp3" -> "/media/SDMPC" */
static void drive_of(const char *path, char *out, size_t n) {
    const char *M = media_root();
    size_t ml = strlen(M);
    out[0] = 0;
    if (strncmp(path, M, ml) || path[ml] != '/') return;
    const char *e = strchr(path + ml + 1, '/');
    snprintf(out, n, "%.*s", (int)(e ? e - path : (long)strlen(path)), path);
}

static void library_scan(library_t *L, int *nroots_out) {
    memset(L, 0, sizeof *L);
    snprintf(L->lib[0].name, sizeof L->lib[0].name, "ALL SONGS");
    L->nlib = 1;
    const char *M = media_root();
    char drives[24][256]; int nd = 0;
    DIR *m = opendir(M);
    struct dirent *de;
    if (m) {
        while ((de = readdir(m)) && nd < 24) if (de->d_name[0] != '.') snprintf(drives[nd++], 256, "%s", de->d_name);
        closedir(m);
    }
    qsort(drives, nd, sizeof drives[0], (int (*)(const void *, const void *))strcasecmp);
    static root_t R[MAX_ROOTS];
    int nr = 0;
    for (int i = 0; i < nd; i++) {
        char p[600];
        snprintf(p, sizeof p, "%s/%s/MELODIC LIBRARY", M, drives[i]);
        add_root(R, &nr, p);
    }
    for (int i = 0; i < nd; i++) {
        char p[300];
        snprintf(p, sizeof p, "%s/%s", M, drives[i]);
        read_list(R, &nr, p);
    }
    for (int i = 0; i < nd; i++) {
        if (is_internal(drives[i])) continue;
        char p[300];
        snprintf(p, sizeof p, "%s/%s", M, drives[i]);
        DIR *d = opendir(p);
        if (!d) continue;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.' || !wax_folder_name(de->d_name) || !strcasecmp(de->d_name, LIST_DIR)) continue;
            char q[600];
            snprintf(q, sizeof q, "%s/%s", p, de->d_name);
            add_root(R, &nr, q);
        }
        closedir(d);
    }
    *nroots_out = nr;
    for (int r = 0; r < nr && !L->root_drive[0]; r++) drive_of(R[r].path, L->root_drive, sizeof L->root_drive);
    write_list_template(drives, nd, L->root_drive);
    for (int r = 0; r < nr; r++) {
        DIR *d = opendir(R[r].path);
        if (!d) continue;
        char *subs[MAX_LIBS]; int ns = 0, loose = -1;
        const char *rname = strrchr(R[r].path, '/');
        rname = rname ? rname + 1 : R[r].path;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char q[1024];
            snprintf(q, sizeof q, "%s/%s", R[r].path, de->d_name);
            struct stat st;
            if (stat(q, &st)) continue;
            if (S_ISDIR(st.st_mode)) { if (ns < MAX_LIBS) subs[ns++] = strdup(de->d_name); }
            else if (S_ISREG(st.st_mode) && audio_file(de->d_name) && big_enough(de->d_name, st.st_size)) {
                if (loose < 0 && L->nlib < MAX_LIBS) {
                    loose = L->nlib++;
                    snprintf(L->lib[loose].name, sizeof L->lib[loose].name, "%s", rname);
                }
                if (loose >= 0) lib_add(L, q, loose);
            }
        }
        closedir(d);
        qsort(subs, ns, sizeof *subs, cmp_str);
        for (int i = 0; i < ns; i++) {
            if (L->nlib < MAX_LIBS && strcasecmp(subs[i], LIST_DIR)) {
                int li = L->nlib++;
                snprintf(L->lib[li].name, sizeof L->lib[li].name, "%s", subs[i]);
                char q[1024];
                snprintf(q, sizeof q, "%s/%s", R[r].path, subs[i]);
                scan_dir(L, q, li, 1);
            }
            free(subs[i]);
        }
    }
    if (L->n) qsort(L->e, L->n, sizeof *L->e, cmp_entry);
    for (int i = 0; i < L->n; i++) L->lib[L->e[i].lib].count++;
    L->lib[0].count = L->n;
    int map[MAX_LIBS], k = 1;   /* drop empty libraries */
    map[0] = 0;
    for (int i = 1; i < L->nlib; i++) {
        if (L->lib[i].count) { map[i] = k; if (k != i) L->lib[k] = L->lib[i]; k++; }
        else map[i] = -1;
    }
    L->nlib = k;
    for (int i = 0; i < L->n; i++) L->e[i].lib = map[L->e[i].lib];
}

static void library_free(library_t *L) {
    for (int i = 0; i < L->n; i++) free(L->e[i].path);
    free(L->e);
    L->e = NULL; L->n = L->cap = 0;
}

/* ---------------- decoding: streamed in chunks to 44.1 kHz int16 stereo, so a long MP3 never needs a float copy ---------------- */
typedef struct {
    int kind;   /* 0 wav, 1 flac, 2 mp3 */
    drwav wav; drflac *flac; drmp3 mp3;
    unsigned ch, rate;
} dec_t;

static int dec_open(dec_t *d, const char *path) {
    memset(d, 0, sizeof *d);
    if (has_ext(path, ".flac")) {
        d->kind = 1;
        d->flac = drflac_open_file(path, NULL);
        if (!d->flac) return -1;
        d->ch = d->flac->channels; d->rate = d->flac->sampleRate;
    } else if (has_ext(path, ".mp3")) {
        d->kind = 2;
        if (!drmp3_init_file(&d->mp3, path, NULL)) return -1;
        d->ch = d->mp3.channels; d->rate = d->mp3.sampleRate;
    } else {
        d->kind = 0;
        if (!drwav_init_file(&d->wav, path, NULL)) return -1;
        d->ch = d->wav.channels; d->rate = d->wav.sampleRate;
    }
    if (!d->ch || !d->rate || d->rate < 8000 || d->rate > 192000) return -1;
    return 0;
}
static long dec_read(dec_t *d, int16_t *buf, long frames) {
    if (d->kind == 1) return (long)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)frames, buf);
    if (d->kind == 2) return (long)drmp3_read_pcm_frames_s16(&d->mp3, (drmp3_uint64)frames, buf);
    return (long)drwav_read_pcm_frames_s16(&d->wav, (drwav_uint64)frames, buf);
}
static void dec_close(dec_t *d) {
    if (d->kind == 1) { if (d->flac) drflac_close(d->flac); }
    else if (d->kind == 2) drmp3_uninit(&d->mp3);
    else drwav_uninit(&d->wav);
}

static int16_t *decode(const char *path, long *out_frames) {
    dec_t d;
    if (dec_open(&d, path)) { dec_close(&d); return NULL; }
    long cap = (long)(MAX_FILE_SEC * FS);
    int16_t *out = malloc((size_t)cap * 2 * sizeof *out);
    enum { CH = 4096 };
    int16_t *chunk = malloc((size_t)CH * d.ch * sizeof *chunk);
    if (!out || !chunk) { free(out); free(chunk); dec_close(&d); return NULL; }
    double ratio = (double)d.rate / FS;   /* input frames per output frame */
    long base = 0;                        /* input index of chunk[0] */
    float prev[2] = {0, 0};               /* input frame base-1 */
    double opos = 0;                      /* input position of the next output frame */
    long nout = 0;
    for (;;) {
        long n = dec_read(&d, chunk, CH);
        if (n <= 0) break;
        if (d.rate == FS) {
            for (long i = 0; i < n && nout < cap; i++, nout++) {
                int16_t l = chunk[i * d.ch], r = d.ch > 1 ? chunk[i * d.ch + 1] : l;
                out[2 * nout] = l; out[2 * nout + 1] = r;
            }
        } else {
            while (nout < cap) {
                long i0 = (long)floor(opos);
                if (i0 + 1 >= base + n) break;   /* needs the next chunk */
                double fr = opos - i0;
                for (int c = 0; c < 2; c++) {
                    int cc = d.ch > 1 ? c : 0;
                    float a = i0 < base ? prev[c] : chunk[(i0 - base) * d.ch + cc];
                    float b = chunk[(i0 + 1 - base) * d.ch + cc];
                    float y = (float)(a + (b - a) * fr);
                    out[2 * nout + c] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
                }
                nout++;
                opos = nout * ratio;
            }
            for (int c = 0; c < 2; c++) prev[c] = chunk[(n - 1) * d.ch + (d.ch > 1 ? c : 0)];
            base += n;
        }
        if (nout >= cap) break;
    }
    free(chunk);
    dec_close(&d);
    if (nout < (long)(MIN_FILE_SEC * FS)) { free(out); return NULL; }
    int16_t *shrunk = realloc(out, (size_t)nout * 2 * sizeof *out);
    *out_frames = nout;
    return shrunk ? shrunk : out;
}

/* ---------------- analysis ---------------- */
typedef struct {
    int16_t *lr; long frames;   /* the song, 44.1 kHz stereo */
    float bpm;
    double beat0;               /* sample position of a downbeat */
    int key;                    /* 0-11 major (C..B), 12-23 minor */
    float *onset, *energy; int nh;
    float *beat_o;              /* onsets for tempo and grid: full band + highs (hats), each scaled to its average */
    float *dm; long ndm;        /* mono, averaged over 8 samples (5.5 kHz): what the stretcher lines grains up on */
    int loop_beats;             /* > 0: the file is a cut loop of this many beats (starts on its downbeat) */
    long cue0[NPADS];           /* the cues it arrived with */
    char name[160];
    char path[512];
    char drive[256];
} song_t;

static void song_free(song_t *s) { if (s) { free(s->lr); free(s->onset); free(s->energy); free(s->beat_o); free(s->dm); free(s); } }

static void analyse_env(song_t *S) {
    int H = (int)(S->frames / HOP);
    S->nh = H;
    S->onset = calloc((size_t)(H > 0 ? H : 1), sizeof(float));
    S->energy = calloc((size_t)(H > 0 ? H : 1), sizeof(float));
    if (!S->onset || !S->energy || H < 4) return;
    float *hi = calloc((size_t)H, sizeof(float));   /* the highs (second difference): hats and the tops of snares */
    float prev = 0, prevd = 0;
    for (int h = 0; h < H; h++) {
        double acc = 0, en = 0, hacc = 0;
        for (int i = 0; i < HOP; i++) {
            long k = (long)h * HOP + i;
            float m = (S->lr[2 * k] + S->lr[2 * k + 1]) * (0.5f / 32768.0f);
            float d = m - prev; prev = m;
            float dd = d - prevd; prevd = d;
            acc += (double)d * d + 0.1 * (double)m * m;
            hacc += (double)dd * dd;
            en += (double)m * m;
        }
        S->energy[h] = logf((float)(en / HOP) + 1e-9f);
        S->onset[h] = logf((float)(acc / HOP) + 1e-9f);
        if (hi) hi[h] = logf((float)(hacc / HOP) + 1e-10f);
    }
    for (int h = H - 1; h >= 1; h--) { float d = S->onset[h] - S->onset[h - 1]; S->onset[h] = d > 0 ? d : 0; }
    S->onset[0] = 0;
    S->beat_o = calloc((size_t)H, sizeof(float));
    if (hi && S->beat_o) {
        for (int h = H - 1; h >= 1; h--) { float d = hi[h] - hi[h - 1]; hi[h] = d > 0 ? d : 0; }
        hi[0] = 0;
        double mo = 1e-6, mh = 1e-6;
        for (int h = 0; h < H; h++) { mo += S->onset[h]; mh += hi[h]; }
        mo /= H; mh /= H;
        for (int h = 0; h < H; h++) S->beat_o[h] = (float)(S->onset[h] / mo + hi[h] / mh);
    } else if (S->beat_o) memcpy(S->beat_o, S->onset, (size_t)H * sizeof(float));
    free(hi);
    S->ndm = S->frames / DM;
    S->dm = malloc((size_t)(S->ndm + 2) * sizeof *S->dm);
    if (S->dm) {
        for (long i = 0; i < S->ndm; i++) {
            int a = 0;
            for (int k = 0; k < DM; k++) a += S->lr[2 * (i * DM + k)] + S->lr[2 * (i * DM + k) + 1];
            S->dm[i] = a * (1.0f / (2 * DM * 32768.0f));
        }
        S->dm[S->ndm] = S->dm[S->ndm + 1] = 0;
    }
}

static float bpm_from_name(const char *name) {
    for (const char *p = name; *p; p++) {
        if (!strncasecmp(p, "bpm", 3) && p > name) {
            const char *q = p;
            while (q > name && q[-1] == ' ') q--;
            const char *s = q;
            while (s > name && isdigit((unsigned char)s[-1])) s--;
            if (q - s >= 2 && q - s <= 3) {
                float b = (float)atof(s);
                if (b >= 50 && b <= 220) return b;
            }
        }
    }
    return 0;
}

/* tempo from the onset envelope's autocorrelation, 75-150 BPM, over the first 90 s */
static float detect_bpm(const float *o, int H) {
    if (!o || H < 150) return 0;
    if (H > 9000) H = 9000;
    double best = -1; float bb = 0;
    for (float bpm = 75.0f; bpm <= 150.0f; bpm += 0.25f) {
        double lag = 6000.0 / bpm;
        double s = 0;
        for (int m = 1; m <= 4; m++) {
            double L = lag * m;
            int l0 = (int)L; double fr = L - l0;
            double a = 0;
            for (int h = l0 + 1; h < H; h++) a += o[h] * ((1 - fr) * o[h - l0] + fr * o[h - l0 - 1]);
            s += a / (m == 1 ? 1.0 : 1.5);
        }
        if (s > best) { best = s; bb = bpm; }
    }
    return bb;
}

/* the beat grid's phase, then which of 4 beats is the downbeat (the strongest on average) */
static double find_downbeat(const float *o, int H, float bpm) {
    if (!o || H < 100 || bpm <= 0) return 0;
    double P = 6000.0 / bpm;   /* hops per beat */
    int span = H < 12000 ? H : 12000;
    double best = -1, ph = 0;
    for (double phi = 0; phi < P; phi += 0.5) {
        double s = 0;
        for (double t = phi; t < span - 1; t += P) { int i = (int)t; double fr = t - i; s += (1 - fr) * o[i] + fr * o[i + 1]; }
        if (s > best) { best = s; ph = phi; }
    }
    double bbest = -1; int bb = 0;
    for (int b = 0; b < 4; b++) {
        double s = 0;
        for (double t = ph + b * P; t < span - 1; t += 4 * P) {   /* a little window: hits wander */
            int i = (int)t;
            float m = 0;
            for (int k = i - 2; k <= i + 2; k++) if (k >= 0 && k < H && o[k] > m) m = o[k];
            s += m;
        }
        if (s > bbest) { bbest = s; bb = b; }
    }
    double pos = (ph + bb * P) * HOP;
    while (pos - 4 * P * HOP >= 0) pos -= 4 * P * HOP;   /* the first downbeat in the song */
    return pos;
}

/* ---- key: chroma from a 4096-point FFT at 11025 Hz, then the Krumhansl-Kessler profiles ---- */
static void fft(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        float wr = (float)cos(a), wi = (float)sin(a);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int u = i + k, v = i + k + len / 2;
                float tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                re[v] = re[u] - tr; im[v] = im[u] - ti;
                re[u] += tr; im[u] += ti;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

static int detect_key(const song_t *S) {
    enum { N = 4096, DEC = 4 };
    const double fs = (double)FS / DEC;
    float *re = malloc(N * sizeof *re), *im = malloc(N * sizeof *im), *win = malloc(N * sizeof *win);
    int *pc = malloc((N / 2) * sizeof *pc); float *pw = malloc((N / 2) * sizeof *pw);
    if (!re || !im || !win || !pc || !pw) { free(re); free(im); free(win); free(pc); free(pw); return 9 + 12; }
    for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / N);
    for (int k = 0; k < N / 2; k++) {
        double f = k * fs / N;
        pc[k] = -1; pw[k] = 0;
        if (f < 55 || f > 2000) continue;
        double midi = 69 + 12 * log2(f / 440.0);
        double r = floor(midi + 0.5);
        double dev = fabs(midi - r);   /* bins between two notes count less */
        if (dev > 0.4) continue;
        pc[k] = ((int)r % 12 + 12) % 12;
        pw[k] = (float)(1.0 - dev * 2.0);
    }
    double chroma[12] = {0};
    long total = S->frames / DEC;
    long from = (long)(4.0 * fs), to = total - N;   /* skip a 4 s intro; at most ~150 frames */
    long step = N / 2;
    if ((to - from) / step > 150) step = (to - from) / 150;
    for (long f0 = from; f0 < to; f0 += step) {
        for (int i = 0; i < N; i++) {
            long k = (f0 + i) * DEC;
            float m = 0;
            for (int d = 0; d < DEC; d++) m += S->lr[2 * (k + d)] + S->lr[2 * (k + d) + 1];
            re[i] = m / (2.0f * DEC * 32768.0f) * win[i];
            im[i] = 0;
        }
        fft(re, im, N);
        double c[12] = {0}, sum = 0;
        for (int k = 0; k < N / 2; k++) if (pc[k] >= 0) {
            double mag = sqrt((double)re[k] * re[k] + (double)im[k] * im[k]);
            c[pc[k]] += pw[k] * (float)sqrt(mag);   /* linear-ish: log compression lifted the overtones (the fifth) too much */
        }
        for (int i = 0; i < 12; i++) sum += c[i];
        if (sum > 1e-6) for (int i = 0; i < 12; i++) chroma[i] += c[i] / sum;
    }
    free(re); free(im); free(win); free(pc); free(pw);
    static const double MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    double cm = 0; for (int i = 0; i < 12; i++) cm += chroma[i] / 12;
    double best = -2; int bk = 21;
    for (int mode = 0; mode < 2; mode++) {
        const double *P = mode ? MIN : MAJ;
        double pm = 0; for (int i = 0; i < 12; i++) pm += P[i] / 12;
        for (int root = 0; root < 12; root++) {
            double num = 0, da = 0, db = 0;
            for (int i = 0; i < 12; i++) {
                double a = chroma[(root + i) % 12] - cm, b = P[i] - pm;
                num += a * b; da += a * a; db += b * b;
            }
            double r = (da > 0 && db > 0) ? num / sqrt(da * db) : -1;
            if (r > best) { best = r; bk = root + 12 * mode; }
        }
    }
    return bk;
}

static void key_text(int key, char *out, size_t n) {
    snprintf(out, n, "%s %s", NOTE_NAME[key % 12], key >= 12 ? "MINOR" : "MAJOR");
}

/* ---- cues ---- */
static float avg(const float *x, int a, int b, int H) {
    if (a < 0) a = 0;
    if (b > H) b = H;
    if (b <= a) return -20;
    double s = 0;
    for (int i = a; i < b; i++) s += x[i];
    return (float)(s / (b - a));
}

/* A cut loop: the file is a whole number of bars (1, 2, 4, 8, 16 or 32) at a tempo close to the detected one
 * (or the one in its name). Then the tempo is exact, the downbeat is the start of the file, and cues sit on its grid. */
static int detect_loop(song_t *S, int named) {
    double dur = (double)S->frames / FS;
    if (dur > 75 || S->bpm <= 0) return 0;
    static const int BARS[] = {1, 2, 4, 8, 16, 32};
    double best_err = 1; int best = 0; double best_bpm = 0;
    for (int i = 0; i < 6; i++) {
        double b = BARS[i] * 240.0 / dur;
        if (b < 55 || b > 200) continue;
        for (int m = 0; m < 3; m++) {   /* the detector can land on half or double time; a name can't */
            if (named && m) break;
            double ref = S->bpm * (m == 0 ? 1.0 : m == 1 ? 2.0 : 0.5);
            double err = fabs(log(b / ref)) + (m ? 0.004 : 0);
            if (err < best_err) { best_err = err; best = BARS[i]; best_bpm = b; }
        }
    }
    if (!best || best_err > (named ? 0.02 : 0.035)) return 0;
    S->bpm = (float)best_bpm;
    S->beat0 = 0;
    S->loop_beats = best * 4;
    return 1;
}

/* fill cue[n..15] with free grid points: bars, then half bars, beats, 8ths, 16ths, spread over the song */
static int fill_grid(const song_t *S, long *cue, int n, long last) {
    double beat = 60.0 / S->bpm * FS;
    static const double DIV[] = {4, 2, 1, 0.5, 0.25};
    for (int d = 0; d < 5 && n < NPADS; d++) {
        double step = DIV[d] * beat;
        double t0 = S->beat0;
        while (t0 - step >= 0) t0 -= step;
        long cnt = 0;
        for (double t = t0; t < last; t += step) cnt++;
        if (cnt <= 0) continue;
        long *free_pts = malloc((size_t)cnt * sizeof *free_pts);
        if (!free_pts) break;
        long nf = 0;
        for (double t = t0; t < last && nf < cnt; t += step) {
            long p = (long)t - 88; if (p < 0) p = 0;
            int ok = 1;
            for (int j = 0; j < n; j++) if (labs(cue[j] - p) < (long)(step / 2)) { ok = 0; break; }
            if (ok) free_pts[nf++] = p;
        }
        int need = NPADS - n;
        if (nf >= need || d == 4) {
            int take = nf < need ? (int)nf : need;
            for (int k = 0; k < take; k++) cue[n++] = free_pts[(long)((double)k * nf / take)];
        }
        free(free_pts);
    }
    return n;
}

/* move a grid cue onto the hit that's actually there: the strongest onset within +-40 ms, if it stands out */
static long snap_to_hit(const song_t *S, long pos) {
    const float *o = S->beat_o ? S->beat_o : S->onset;
    if (!o || pos <= 0) return pos;
    int h0 = (int)((pos + 88) / HOP), bi = -1; float bv = 0;
    for (int h = h0 - 4; h <= h0 + 4; h++) if (h >= 1 && h < S->nh && o[h] > bv) { bv = o[h]; bi = h; }
    if (bi < 0 || bv < 3.0f) return pos;   /* nothing clear: keep the grid */
    long p = (long)bi * HOP - 88;
    return p < 0 ? 0 : p;
}

/* 16 cues, sorted in time. mode: phrases (every 4 bars), bars, hits. jitter > 0 rolls a different set. */
static void find_cues(const song_t *S, int mode, unsigned *seed, int jitter, long *cue) {
    int H = S->nh;
    double beat = 60.0 / S->bpm * FS;   /* samples */
    long frames = S->frames;
    long last = frames - (long)(2 * beat);   /* leave room to play */
    if (last < 1) last = 1;
    enum { MAXC = 4096 };
    long *cand = malloc(MAXC * sizeof *cand); float *score = malloc(MAXC * sizeof *score);   /* per call: instances run at once */
    if (!cand || !score) { free(cand); free(score); for (int i = 0; i < NPADS; i++) cue[i] = (long)((double)last * i / NPADS); return; }
    int nc = 0;
    if (S->loop_beats > 0) {   /* a cut loop: everything on its grid */
        free(cand); free(score);
        long len = frames;
        int n16 = S->loop_beats * 4;   /* 16th notes in the loop */
        if (mode != CUE_HITS && !jitter) {   /* 16 equal slices: a beat each for 4 bars, 2 beats for 8, an 8th for 2 */
            for (int i = 0; i < NPADS; i++) { long p = (long)((double)len * i / NPADS) - (i ? 88 : 0); cue[i] = snap_to_hit(S, p < 0 ? 0 : p); }
            return;
        }
        /* HITS, or FIND CUES on a loop: the 16th-note grid points with the strongest hits (FIND CUES adds chance) */
        float *sc = malloc((size_t)n16 * sizeof *sc);
        if (!sc) { for (int i = 0; i < NPADS; i++) cue[i] = (long)((double)len * i / NPADS); return; }
        for (int g = 0; g < n16; g++) {
            int h = (int)((double)len * g / n16 / HOP);
            float o = 0;
            for (int k = h - 2; k <= h + 2; k++) if (k >= 0 && k < H && S->onset[k] > o) o = S->onset[k];
            sc[g] = o + (g % 4 == 0 ? 0.15f : 0) + (g % 16 == 0 ? 0.25f : 0);   /* toward beats and bars */
            if (jitter) { *seed = *seed * 1664525u + 1013904223u; sc[g] *= 0.4f + 1.2f * ((*seed >> 8) & 0xffff) / 65535.0f; }
        }
        sc[0] = 1e6f;   /* pad 1 is always the start of the loop */
        int n = 0;
        if (n16 <= NPADS) { for (int g = 0; g < n16 && n < NPADS; g++) cue[n++] = (long)((double)len * g / n16); }
        else {
            for (; n < NPADS; n++) {
                int bi = -1;
                for (int g = 0; g < n16; g++) if (sc[g] > -1e8f && (bi < 0 || sc[g] > sc[bi])) bi = g;
                if (bi < 0) break;
                cue[n] = (long)((double)len * bi / n16); sc[bi] = -1e9f;
            }
        }
        free(sc);
        for (int i = 0; i < n; i++) { cue[i] -= cue[i] ? 88 : 0; if (cue[i] < 0) cue[i] = 0; cue[i] = snap_to_hit(S, cue[i]); }
        for (int i = n; i < NPADS; i++) cue[i] = (long)((double)len * i / NPADS);
        for (int a = 0; a < NPADS; a++) for (int b = a + 1; b < NPADS; b++) if (cue[b] < cue[a]) { long t = cue[a]; cue[a] = cue[b]; cue[b] = t; }
        return;
    }
    if (mode == CUE_HITS) {
        for (int h = 2; h + 2 < H && nc < MAXC; h++) {
            const float *o = S->onset;
            if (o[h] < o[h - 1] || o[h] < o[h + 1] || o[h] < 0.25f) continue;
            long pos = (long)h * HOP - 88;
            if (pos < 0 || pos >= last) continue;
            cand[nc] = pos;
            score[nc] = o[h] + 0.3f * avg(S->energy, h, h + 50, H) / 10.0f;
            nc++;
        }
    } else {
        double step = beat * 4;   /* every bar; in PHRASES mode the 4-bar starts win, other bars only fill in */
        int bar = 0;
        for (double t = S->beat0; t < last && nc < MAXC; t += step, bar++) {
            long pos = (long)t - 88;
            if (pos < 0) pos = 0;
            int h = (int)(t / HOP);
            int bh = (int)(step / HOP);
            float before = avg(S->energy, h - bh, h, H), after = avg(S->energy, h, h + bh, H);
            if (after < -9.0f) continue;   /* silence (about -40 dB) */
            float onset = 0;
            for (int k = h - 3; k <= h + 3; k++) if (k >= 0 && k < H && S->onset[k] > onset) onset = S->onset[k];
            cand[nc] = pos;
            score[nc] = fabsf(after - before) * 1.5f + after * 0.15f + onset;   /* a change, loud, starts on a hit */
            if (mode == CUE_PHRASES) score[nc] += bar % 4 == 0 ? 100.0f : 0.0f;
            nc++;
        }
    }
    if (jitter) for (int i = 0; i < nc; i++) { *seed = *seed * 1664525u + 1013904223u; score[i] *= 0.4f + 1.2f * ((*seed >> 8) & 0xffff) / 65535.0f; }
    long min_gap = (long)(frames / 40);
    long unit = (long)(beat * (mode == CUE_HITS ? 1 : mode == CUE_BARS ? 4 : 16));
    if (min_gap < unit) min_gap = unit;
    int n = 0;
    for (int round = 0; round < 3 && n < NPADS; round++) {   /* relax the gap if there aren't enough */
        for (;;) {
            int bi = -1;
            for (int i = 0; i < nc; i++) if (cand[i] >= 0 && (bi < 0 || score[i] > score[bi])) bi = i;
            if (bi < 0 || n >= NPADS) break;
            long p = cand[bi];
            cand[bi] = -1 - cand[bi];   /* used */
            int ok = 1;
            for (int j = 0; j < n; j++) if (labs(cue[j] - p) < min_gap) { ok = 0; break; }
            if (ok) cue[n++] = p;
        }
        for (int i = 0; i < nc; i++) if (cand[i] < 0) cand[i] = -1 - cand[i];
        min_gap /= 2;
        if (min_gap < unit / 2) min_gap = unit / 2 > 1 ? unit / 2 : 1;
    }
    free(cand); free(score);
    if (n < NPADS) n = fill_grid(S, cue, n, last);   /* not enough: free points on the beat grid */
    if (mode != CUE_HITS) for (int i = 0; i < n; i++) cue[i] = snap_to_hit(S, cue[i]);
    for (int i = n; i < NPADS; i++) cue[i] = (long)((double)last * i / NPADS);   /* a very short song: even spacing */
    for (int a = 0; a < NPADS; a++) for (int b = a + 1; b < NPADS; b++) if (cue[b] < cue[a]) { long t = cue[a]; cue[a] = cue[b]; cue[b] = t; }
}

/* ---------------- playing: a voice reads the song directly, or through the WSOLA stretcher ---------------- */
#define GN 2048                 /* grain length (output samples) */
#define GH (GN / 2)             /* hop */
#define TOL 512                 /* search +/- this many input samples */
static float HANN[GN];

typedef struct {
    int on, pad, note, held, stopping;
    long start, end;            /* input region */
    double pos;                 /* direct: read position */
    double rate;                /* direct: input samples per output sample */
    int stretch;
    double p, t;                /* stretch: pitch ratio (read rate inside grains), tempo ratio (input per output) */
    double nom, prev;           /* nominal grain position, actual start of the last grain */
    float acc[2][GN]; int opos; /* overlap-add buffer, read position in it */
    int first;
    long played, max_out;
    float gain, env; float rel_step; int fade_in;
} voice_t;

static inline float smp(const song_t *S, double x, int c) {
    long i = (long)x;
    if (i < 0 || i + 1 >= S->frames) return 0;
    float fr = (float)(x - i);
    float a = S->lr[2 * i + c], b = S->lr[2 * (i + 1) + c];
    return (a + (b - a) * fr) * (1.0f / 32768.0f);
}
static inline float mono(const song_t *S, double x) { return 0.5f * (smp(S, x, 0) + smp(S, x, 1)); }

static inline float dms(const song_t *S, double x) {   /* the search copy at input position x */
    double d = x * (1.0 / DM);
    long i = (long)d;
    if (i < 0 || i >= S->ndm) return 0;
    float fr = (float)(d - i);
    return S->dm[i] + (S->dm[i + 1] - S->dm[i]) * fr;
}

/* best start near x for a grain that follows on from template position tp (both read at rate p):
 * a coarse search on the 5.5 kHz copy (steps of 8 input samples), then a fine one at full rate */
static double wsola_search(const song_t *S, double x, double tp, double p) {
    enum { PTS = GH / DM };
    float tpl[PTS], cand[PTS + 2 * (TOL / DM) + 2];
    double te = 0;
    for (int j = 0; j < PTS; j++) { tpl[j] = dms(S, tp + j * DM * p); te += tpl[j] * tpl[j]; }
    if (te < 1e-7 || !S->dm) return x;   /* silence: nothing to line up */
    /* the candidates' samples: for lag d (a multiple of DM), point j sits at x + d + j*DM*p. With p = 1 these are
     * shared between lags; otherwise read them per lag from the small copy (still cheap). */
    double best = -1e30; int bd = 0;
    int fast = fabs(p - 1.0) < 1e-9;
    if (fast) for (int k = 0; k < PTS + 2 * (TOL / DM) + 1; k++) cand[k] = dms(S, x - TOL + k * DM);
    for (int d = -TOL; d <= TOL; d += DM) {
        double num = 0, en = 1e-9;
        if (fast) {
            const float *c = cand + (d + TOL) / DM;
            for (int j = 0; j < PTS; j++) { num += c[j] * tpl[j]; en += c[j] * c[j]; }
        } else {
            for (int j = 0; j < PTS; j++) { float v = dms(S, x + d + j * DM * p); num += v * tpl[j]; en += v * v; }
        }
        double sc = num / sqrt(en);
        if (sc > best) { best = sc; bd = d; }
    }
    /* fine: +/- 4 samples around the best, 64 points of the full-rate signal */
    float ft[64];
    for (int j = 0; j < 64; j++) ft[j] = mono(S, tp + j * 16 * p);
    double fb = -1e30; int b2 = bd;
    for (int d = bd - 4; d <= bd + 4; d++) {
        if (d < -TOL || d > TOL) continue;
        double num = 0, en = 1e-9;
        for (int j = 0; j < 64; j++) { float v = mono(S, x + d + j * 16 * p); num += v * ft[j]; en += v * v; }
        double sc = num / sqrt(en);
        if (sc > fb) { fb = sc; b2 = d; }
    }
    return x + b2;
}

static void grain_add(voice_t *v, const song_t *S, double x, int first) {
    for (int j = 0; j < GN; j++) {
        float w = (first && j < GH) ? 1.0f : HANN[j];   /* the first grain starts at full level: no softened attack */
        double xi = x + j * v->p;
        v->acc[0][j] += smp(S, xi, 0) * w;
        v->acc[1][j] += smp(S, xi, 1) * w;
    }
}

static void wsola_next(voice_t *v, const song_t *S) {
    memmove(v->acc[0], v->acc[0] + GH, GH * sizeof(float)); memset(v->acc[0] + GH, 0, GH * sizeof(float));
    memmove(v->acc[1], v->acc[1] + GH, GH * sizeof(float)); memset(v->acc[1] + GH, 0, GH * sizeof(float));
    v->nom += GH * v->t;
    double x = wsola_search(S, v->nom, v->prev + GH * v->p, v->p);
    if (x < v->start) x = v->start;
    grain_add(v, S, x, 0);
    v->prev = x;
    v->opos = 0;
}

/* ---------------- engine instance ---------------- */
typedef struct {
    float p[NP];
    float host_bpm;
    char lib_name[128], track_name[160], info[128], status[128];
    unsigned display_rev;
    pthread_mutex_t text_mx;
    /* audio side */
    long cue[NPADS];
    int last_pad;              /* last pad played: NUDGE moves it, KEYS plays it */
    int tshift;                /* x2 / /2 taps on the found tempo: -2..2 (half time, double time) */
    int learn_armed, last_note;
    voice_t v[NVOICES];
    song_t *cur;
    /* hand-over from the worker */
    pthread_mutex_t xfer_mx;
    song_t *next, *garbage;
    long pend_cue[NPADS]; int pend_cues;
    /* worker */
    pthread_t th; int th_ok;
    pthread_mutex_t mx; pthread_cond_t cv;
    int quit;
    int rq_random, rq_prev, rq_next, rq_lib, rq_cues, rq_save, rq_restore, rq_rescan, rq_pick;
    pthread_mutex_t lib_mx;    /* the library list: the worker rescans it, the screen reads names from it (BROWSE) */
    int bpage;                 /* BROWSE: the page shown */
    int rq_lib_browse;         /* LIBRARY tapped on the BROWSE page: change library without loading a song */
    time_t quiet_until;        /* list taps are ignored until then: a project opening sets values, it doesn't tap */
    char restore_path[512];
    long restore_cue[NPADS]; int restore_ncue;
    library_t libs;
    int cur_lib, cur_entry;
    song_t *wsong;             /* the newest song (worker side; read-only once made) */
    unsigned seed;
    int saved_lib; char saved_path[512];
} pw_t;

static double song_bpm(const pw_t *s, const song_t *S) { return S->bpm * ldexp(1.0, s->tshift); }   /* the tempo as the user hears it */
static void set_text(pw_t *s, char *dst, size_t n, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void set_text(pw_t *s, char *dst, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    pthread_mutex_lock(&s->text_mx);
    vsnprintf(dst, n, fmt, ap);
    s->display_rev++;
    pthread_mutex_unlock(&s->text_mx);
    va_end(ap);
}

static int key_shift(const pw_t *s, const song_t *S) {   /* semitones to the chosen key, -6..+5 */
    int t = (int)lroundf(s->p[P_KEY]);
    if (t <= 0 || !S) return 0;
    int d = ((t - 1) - S->key % 12 + 12) % 12;
    return d > 5 ? d - 12 : d;
}

static void show_info(pw_t *s, const song_t *S) {
    if (!S) return;
    char k[32], k2[32];
    key_text(S->key, k, sizeof k);
    int sh = key_shift(s, S);
    long sec = S->frames / FS;
    char len[24];
    const char *tag = s->tshift > 0 ? " (x2)" : s->tshift < 0 ? " (/2)" : "";
    if (s->tshift == 2) tag = " (x4)"; else if (s->tshift == -2) tag = " (/4)";
    if (S->loop_beats > 0) snprintf(len, sizeof len, "LOOP %d BAR%s", S->loop_beats / 4, S->loop_beats > 4 ? "S" : "");
    else snprintf(len, sizeof len, "%ld:%02ld", sec / 60, sec % 60);
    if ((int)lroundf(s->p[P_KEY]) > 0) {
        key_text((S->key % 12 + sh + 12) % 12 + (S->key >= 12 ? 12 : 0), k2, sizeof k2);
        set_text(s, s->info, sizeof s->info, "%.1f BPM%s  |  %s > %s (%+d)  |  %s", song_bpm(s, S), tag, k, k2, sh, len);
    } else set_text(s, s->info, sizeof s->info, "%.1f BPM%s  |  %s  |  %s", song_bpm(s, S), tag, k, len);
}

static int lib_first(pw_t *s, int lib) {
    if (lib <= 0) return 0;
    for (int i = 0; i < s->libs.n; i++) if (s->libs.e[i].lib == lib) return i;
    return -1;
}
static void show_lib(pw_t *s) {
    const lib_t *l = &s->libs.lib[s->cur_lib];
    set_text(s, s->lib_name, sizeof s->lib_name, "%s  (%d)", l->name, l->count);
}

static void load_entry(pw_t *s, int i, const long *cues, int ncues) {
    if (i < 0 || i >= s->libs.n) return;
    entry_t *e = &s->libs.e[i];
    set_text(s, s->status, sizeof s->status, "LOADING...");
    song_t *S = calloc(1, sizeof *S);
    if (!S) return;
    S->lr = decode(e->path, &S->frames);
    if (!S->lr) { free(S); set_text(s, s->status, sizeof s->status, "CAN'T READ THIS FILE - TRY ANOTHER"); return; }
    snprintf(S->name, sizeof S->name, "%s", e->name);
    char *dot = strrchr(S->name, '.');
    if (dot) *dot = 0;
    snprintf(S->path, sizeof S->path, "%s", e->path);
    drive_of(e->path, S->drive, sizeof S->drive);
    set_text(s, s->status, sizeof s->status, "LISTENING FOR TEMPO AND KEY...");
    analyse_env(S);
    S->bpm = bpm_from_name(e->name);
    int named = S->bpm > 0;
    const float *bo = S->beat_o ? S->beat_o : S->onset;
    if (!S->bpm) S->bpm = detect_bpm(bo, S->nh);
    if (S->bpm <= 0) S->bpm = 90;
    if (!detect_loop(S, named)) S->beat0 = find_downbeat(bo, S->nh, S->bpm);
    S->key = detect_key(S);
    if (!cues) s->tshift = 0;   /* a new song starts at the tempo found */
    if (cues && ncues == NPADS) {
        for (int k = 0; k < NPADS; k++) S->cue0[k] = cues[k] < 0 ? 0 : cues[k] >= S->frames ? S->frames - 1 : cues[k];
    } else find_cues(S, (int)lroundf(s->p[P_CUE_MODE]), &s->seed, 0, S->cue0);
    s->cur_entry = i;
    s->wsong = S;
    { int first = lib_first(s, s->cur_lib); if (first >= 0 && i >= first) s->bpage = (i - first) / NROWS; }
    set_text(s, s->track_name, sizeof s->track_name, "%s", S->name);
    show_info(s, S);
    set_text(s, s->status, sizeof s->status, "READY - PLAY THE PADS");
    pthread_mutex_lock(&s->mx);
    snprintf(s->saved_path, sizeof s->saved_path, "%s", e->path);
    pthread_mutex_unlock(&s->mx);
    pthread_mutex_lock(&s->xfer_mx);
    if (s->next) song_free(s->next);   /* never played: safe to drop */
    s->next = S;
    s->pend_cues = 0;
    pthread_mutex_unlock(&s->xfer_mx);
}

/* ---- SAVE PADS: render each pad offline, as heard, to a WAV file ---- */
static void voice_setup(pw_t *s, voice_t *v, const song_t *S, long start, long end, int semis, int play_mode, int held);
static int voice_render(pw_t *s, voice_t *v, const song_t *S, float *l, float *r, int n);

static int write_wav(const char *path, const float *lr, long frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    long data = frames * 4;
    unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                           0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    long riff = 36 + data;
    for (int i = 0; i < 4; i++) { h[4 + i] = (riff >> (8 * i)) & 255; h[40 + i] = (data >> (8 * i)) & 255; }
    fwrite(h, 1, 44, f);
    for (long i = 0; i < frames * 2; i++) {
        float y = lr[i];
        if (y > 1) y = 1;
        if (y < -1) y = -1;
        int16_t q = (int16_t)lrintf(y * 32767.0f);
        fputc(q & 255, f); fputc((q >> 8) & 255, f);
    }
    return fclose(f);
}

static void save_pads(pw_t *s, const song_t *S, const long *cue) {
    if (!S) { set_text(s, s->status, sizeof s->status, "LOAD A SONG FIRST"); return; }
    const char *drive = S->drive[0] ? S->drive : s->libs.root_drive;
    if (!drive[0]) { set_text(s, s->status, sizeof s->status, "NO DRIVE TO SAVE TO"); return; }
    char clean[100];
    int k = 0;
    for (const char *c = S->name; *c && k < (int)sizeof clean - 1; c++)
        clean[k++] = (*c == '/' || *c == '\\' || *c == ':' || *c == '*' || *c == '?' || *c == '"' || *c == '<' || *c == '>' || *c == '|') ? '-' : *c;
    clean[k] = 0;
    char dir[512], sub[700];
    snprintf(dir, sizeof dir, "%s/" LIST_DIR, drive);
    mkdir(dir, 0777);
    snprintf(sub, sizeof sub, "%s/%s", dir, clean);
    mkdir(sub, 0777);
    long maxf = 16L * FS;
    float *buf = malloc((size_t)maxf * 2 * sizeof *buf), *l = malloc(256 * sizeof *l), *r = malloc(256 * sizeof *r);
    voice_t *v = calloc(1, sizeof *v);
    if (!buf || !l || !r || !v) { free(buf); free(l); free(r); free(v); set_text(s, s->status, sizeof s->status, "OUT OF MEMORY"); return; }
    int pm = (int)lroundf(s->p[P_PLAY_MODE]);
    if (pm == PLAY_HOLD) pm = PLAY_TO_NEXT;
    int saved = 0;
    for (int pad = 0; pad < NPADS; pad++) {
        set_text(s, s->status, sizeof s->status, "SAVING PAD %d OF 16...", pad + 1);
        long start = cue[pad], end = S->frames;
        for (int j = 0; j < NPADS; j++) if (cue[j] > start && cue[j] < end) end = cue[j];
        if (pm != PLAY_TO_NEXT) end = S->frames;
        voice_setup(s, v, S, start, end, 0, pm, 0);
        long n = 0;
        while (n < maxf) {
            int want = maxf - n < 256 ? (int)(maxf - n) : 256;
            int got = voice_render(s, v, S, l, r, want);
            for (int i = 0; i < got; i++) { buf[2 * (n + i)] = l[i]; buf[2 * (n + i) + 1] = r[i]; }
            n += got;
            if (got < want) break;
        }
        char path[900];
        snprintf(path, sizeof path, "%s/%s %02d.wav", sub, clean, pad + 1);
        if (n > 0 && !write_wav(path, buf, n)) saved++;
    }
    free(buf); free(l); free(r); free(v);
    if (saved) set_text(s, s->status, sizeof s->status, "SAVED %d PADS: PotentialWax/%.40s", saved, clean);
    else set_text(s, s->status, sizeof s->status, "COULDN'T WRITE THE WAV FILES");
}

static void scan(pw_t *s) {
    int nroots = 0;
    library_t fresh;
    library_scan(&fresh, &nroots);   /* the slow part, without the lock */
    pthread_mutex_lock(&s->lib_mx);
    library_free(&s->libs);
    s->libs = fresh;
    s->cur_lib = 0;
    s->bpage = 0;
    pthread_mutex_unlock(&s->lib_mx);
    if (!s->libs.n) {
        set_text(s, s->lib_name, sizeof s->lib_name, "NO SONGS FOUND");
        set_text(s, s->status, sizeof s->status, "MAKE A 'MELODIC LIBRARY' FOLDER, THEN TAP RESCAN");
    } else {
        show_lib(s);
        set_text(s, s->status, sizeof s->status, "%d SONGS IN %d FOLDER%s - TAP RANDOM TRACK", s->libs.n, nroots, nroots == 1 ? "" : "S");
    }
}

static void *worker(void *arg) {
    pw_t *s = arg;
    scan(s);
    for (;;) {
        pthread_mutex_lock(&s->mx);
        while (!s->quit && !s->rq_random && !s->rq_prev && !s->rq_next && !s->rq_lib && !s->rq_cues && !s->rq_save &&
               !s->rq_restore && !s->rq_rescan && !s->rq_pick && !s->garbage)
            pthread_cond_wait(&s->cv, &s->mx);
        if (s->quit) { pthread_mutex_unlock(&s->mx); break; }
        int r = s->rq_random, pv = s->rq_prev, nx = s->rq_next, lb = s->rq_lib, cu = s->rq_cues, sv = s->rq_save,
            rs = s->rq_restore, rsc = s->rq_rescan, pick = s->rq_pick;
        s->rq_random = s->rq_prev = s->rq_next = s->rq_lib = s->rq_cues = s->rq_save = s->rq_restore = s->rq_rescan = 0;
        s->rq_pick = 0;
        char want[512]; snprintf(want, sizeof want, "%s", s->restore_path);
        long rcue[NPADS]; int rn = s->restore_ncue;
        memcpy(rcue, s->restore_cue, sizeof rcue);
        pthread_mutex_unlock(&s->mx);
        pthread_mutex_lock(&s->xfer_mx);
        song_t *g = s->garbage; s->garbage = NULL;
        pthread_mutex_unlock(&s->xfer_mx);
        song_free(g);
        if (rsc) {
            set_text(s, s->status, sizeof s->status, "LOOKING FOR SONGS...");
            scan(s);
            s->cur_entry = -1;
            if (s->wsong) for (int i = 0; i < s->libs.n; i++) if (!strcmp(s->libs.e[i].path, s->wsong->path)) { s->cur_entry = i; break; }
        }
        if (!s->libs.n) continue;
        if (rs) {   /* a saved project: the same file by its path, else by its name */
            int found = -1;
            for (int i = 0; i < s->libs.n && found < 0; i++) if (!strcmp(s->libs.e[i].path, want)) found = i;
            const char *wn = strrchr(want, '/'); wn = wn ? wn + 1 : want;
            for (int i = 0; i < s->libs.n && found < 0; i++) if (!strcmp(s->libs.e[i].name, wn)) found = i;
            if (found >= 0) {
                s->cur_lib = s->saved_lib >= 0 && s->saved_lib < s->libs.nlib ? s->saved_lib : 0;
                show_lib(s);
                load_entry(s, found, rcue, rn);
            } else set_text(s, s->status, sizeof s->status, "SAVED SONG NOT FOUND - TAP RANDOM TRACK");
            continue;
        }
        if (pick > 0 && pick - 1 < s->libs.n) { load_entry(s, pick - 1, NULL, 0); continue; }   /* BROWSE: a tapped name */
        if (lb) {
            pthread_mutex_lock(&s->lib_mx);
            s->cur_lib = (s->cur_lib + lb) % s->libs.nlib;
            s->bpage = 0;
            pthread_mutex_unlock(&s->lib_mx);
            show_lib(s);
            pthread_mutex_lock(&s->mx);
            int browse = s->rq_lib_browse; s->rq_lib_browse = 0;
            pthread_mutex_unlock(&s->mx);
            if (browse) { pthread_mutex_lock(&s->text_mx); s->display_rev++; pthread_mutex_unlock(&s->text_mx); continue; }
            r = 1;
        }
        if (r || pv || nx) {
            int lib = s->cur_lib, first = lib_first(s, lib), cnt = s->libs.lib[lib].count;
            if (first < 0 || cnt <= 0) continue;
            int idx;
            if (r) {
                s->seed = s->seed * 1664525u + 1013904223u;
                idx = first + (int)((s->seed >> 8) % (unsigned)cnt);
                if (cnt > 1 && idx == s->cur_entry) idx = first + (idx - first + 1) % cnt;   /* never the same one twice */
            } else {
                int rel = (s->cur_entry >= first && s->cur_entry < first + cnt) ? s->cur_entry - first : 0;
                rel = (rel + (nx ? 1 : -1) + cnt) % cnt;
                idx = first + rel;
            }
            load_entry(s, idx, NULL, 0);
        } else if (cu && s->wsong) {
            long c[NPADS];
            find_cues(s->wsong, (int)lroundf(s->p[P_CUE_MODE]), &s->seed, cu > 1, c);
            pthread_mutex_lock(&s->xfer_mx);
            memcpy(s->pend_cue, c, sizeof c);
            s->pend_cues = 1;
            pthread_mutex_unlock(&s->xfer_mx);
            set_text(s, s->status, sizeof s->status, "NEW CUES - PLAY THE PADS");
        }
        if (sv) {
            long c[NPADS];
            pthread_mutex_lock(&s->xfer_mx);
            memcpy(c, s->pend_cues ? s->pend_cue : s->cue, sizeof c);
            const song_t *S = s->wsong;
            pthread_mutex_unlock(&s->xfer_mx);
            save_pads(s, S, c);
        }
    }
    return NULL;
}

static void wake(pw_t *s, int *flag, int val) {
    pthread_mutex_lock(&s->mx);
    *flag = val;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mx);
}

static const float PDEF[NP] = {
    0, 0, 0, 0,  0, 0, 0, 0,
    MODE_PADS, 0, CUE_BARS, PLAY_HOLD,
    1, 0, 0, PM_KEEP_TEMPO, 1, 0, 80,
    0, 0, 0, 0, 0, 0, 36, 60, 0,
};

static void *create(const char *data_dir) {
    (void)data_dir;
    if (HANN[1] == 0) for (int j = 0; j < GN; j++) HANN[j] = 0.5f - 0.5f * cosf(2 * (float)M_PI * j / GN);   /* periodic: sums to 1 at 50% */
    pw_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < NP; i++) s->p[i] = PDEF[i];
    s->last_note = -1;
    s->saved_lib = -1;
    s->cur_entry = -1;
    s->seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)s;
    s->quiet_until = time(NULL) + 3;
    pthread_mutex_init(&s->text_mx, NULL);
    pthread_mutex_init(&s->xfer_mx, NULL);
    pthread_mutex_init(&s->mx, NULL);
    pthread_mutex_init(&s->lib_mx, NULL);
    pthread_cond_init(&s->cv, NULL);
    snprintf(s->lib_name, sizeof s->lib_name, "SCANNING...");
    snprintf(s->track_name, sizeof s->track_name, "-");
    snprintf(s->info, sizeof s->info, "-");
    snprintf(s->status, sizeof s->status, "LOOKING FOR YOUR SONGS...");
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024);
    s->th_ok = pthread_create(&s->th, &at, worker, s) == 0;
    pthread_attr_destroy(&at);
    return s;
}

static void destroy(void *inst) {
    pw_t *s = inst;
    if (s->th_ok) {
        pthread_mutex_lock(&s->mx); s->quit = 1; pthread_cond_signal(&s->cv); pthread_mutex_unlock(&s->mx);
        pthread_join(s->th, NULL);
    }
    song_t *a = s->cur, *b = s->next, *c = s->garbage;
    song_free(a);
    if (b != a) song_free(b);
    if (c != a && c != b) song_free(c);
    library_free(&s->libs);
    pthread_mutex_destroy(&s->text_mx); pthread_mutex_destroy(&s->xfer_mx); pthread_mutex_destroy(&s->mx);
    pthread_mutex_destroy(&s->lib_mx);
    pthread_cond_destroy(&s->cv);
    free(s);
}

/* ---------------- voices ---------------- */
static void voice_setup(pw_t *s, voice_t *v, const song_t *S, long start, long end, int semis, int play_mode, int held) {
    memset(v, 0, sizeof *v);
    v->on = 1; v->held = held; v->pad = -1; v->note = -1;
    v->start = start; v->end = end;
    double tempo = 1.0;
    if ((int)lroundf(s->p[P_TEMPO]) == 1 && s->host_bpm > 20 && S->bpm > 0) tempo = s->host_bpm / song_bpm(s, S);
    double st = semis + key_shift(s, S) + s->p[P_PITCH];
    double pitch = pow(2.0, st / 12.0);
    if ((int)lroundf(s->p[P_PITCH_MODE]) == PM_VINYL || (fabs(pitch - 1) < 1e-6 && fabs(tempo - 1) < 1e-6)) {
        v->stretch = 0;
        v->rate = tempo * pitch;
    } else {
        v->stretch = 1;
        v->p = pitch; v->t = tempo;
        v->nom = v->prev = start;
        grain_add(v, S, start, 1);
        v->opos = 0;
    }
    v->pos = start;
    double beat_out = 60.0 / song_bpm(s, S) * FS / (v->stretch ? v->t : v->rate);   /* a beat of the song, in output samples */
    v->max_out = -1;
    if (play_mode == PLAY_1BAR) v->max_out = (long)(4 * beat_out);
    else if (play_mode == PLAY_2BARS) v->max_out = (long)(8 * beat_out);
    v->gain = powf(10.0f, s->p[P_VOLUME] / 20.0f);
    v->env = 1;
    float rel = s->p[P_RELEASE] < 5 ? 5 : s->p[P_RELEASE];
    v->rel_step = 1.0f / (rel * 0.001f * FS);
    v->fade_in = 44;   /* 1 ms */
}

/* render up to n samples; returns how many it made (fewer: the voice ended) */
static int voice_render(pw_t *s, voice_t *v, const song_t *S, float *l, float *r, int n) {
    (void)s;
    int i = 0;
    for (; i < n && v->on; i++) {
        float a, b;
        if (v->stretch) {
            if (v->opos >= GH) wsola_next(v, S);
            a = v->acc[0][v->opos]; b = v->acc[1][v->opos]; v->opos++;
            if (v->prev + GN * v->p >= v->end && !v->stopping) v->stopping = 1;   /* the grains have reached the end */
        } else {
            if (v->pos + 1 >= v->end) { if (!v->stopping) v->stopping = 1; }
            a = smp(S, v->pos, 0); b = smp(S, v->pos, 1);
            v->pos += v->rate;
        }
        float g = v->gain;
        if (v->fade_in > 0) { g *= 1.0f - v->fade_in / 44.0f; v->fade_in--; }
        if (v->max_out >= 0 && v->played >= v->max_out && !v->stopping) v->stopping = 1;
        if (v->stopping) {
            float step = v->stopping == 2 ? v->rel_step : 1.0f / 441;   /* the region's end: 10 ms; a release: the RELEASE time */
            v->env -= step;
            if (v->env <= 0) { v->on = 0; l[i] = r[i] = 0; i++; break; }
        }
        g *= v->env;
        l[i] = a * g; r[i] = b * g;
        v->played++;
    }
    return i;
}

static void release_voice(voice_t *v) { if (v->on && v->stopping != 1) v->stopping = 2; }

static int alloc_voice(pw_t *s) {
    for (int i = 0; i < NVOICES; i++) if (!s->v[i].on) return i;
    long most = -1; int k = 0;
    for (int i = 0; i < NVOICES; i++) if (s->v[i].played > most) { most = s->v[i].played; k = i; }
    return k;
}

static void start_pad(pw_t *s, int pad, int note, int vel) {
    song_t *S = s->cur;
    if (!S || pad < 0 || pad >= NPADS) return;
    int keys = (int)lroundf(s->p[P_MODE]) == MODE_KEYS;
    int semis = 0, cpad = pad;
    if (keys) { cpad = s->last_pad; semis = note - (int)s->p[P_ROOT_NOTE]; }
    else s->last_pad = pad;
    if (!keys && s->p[P_CHOKE] > 0.5f) for (int i = 0; i < NVOICES; i++) if (s->v[i].on) release_voice(&s->v[i]);
    int pm = (int)lroundf(s->p[P_PLAY_MODE]);
    long start = s->cue[cpad], end = S->frames;
    if (pm == PLAY_TO_NEXT) for (int j = 0; j < NPADS; j++) if (s->cue[j] > start && s->cue[j] < end) end = s->cue[j];
    voice_t *v = &s->v[alloc_voice(s)];
    voice_setup(s, v, S, start, end, semis, pm, pm == PLAY_HOLD);
    float g = vel / 127.0f;
    v->gain *= 0.25f + 0.75f * g * g;
    v->pad = pad; v->note = note;
}

static int note_to_pad(pw_t *s, int note) {
    int base = (int)s->p[P_BASE_NOTE];
    if (note >= base && note < base + NPADS) return note - base;
    if (note >= 36 && note < 52) return note - 36;
    if (note >= 0 && note < 16) return note;
    return -1;
}

static void midi(void *inst, const uint8_t *msg, int len) {
    pw_t *s = inst;
    if (len < 3) return;
    int st = msg[0] & 0xF0, note = msg[1], vel = msg[2];
    if (st == 0x80 || (st == 0x90 && vel == 0)) {   /* note off: HOLD voices let go */
        for (int i = 0; i < NVOICES; i++) if (s->v[i].on && s->v[i].note == note && s->v[i].held) release_voice(&s->v[i]);
        return;
    }
    if (st != 0x90) return;
    s->last_note = note;
    int keys = (int)lroundf(s->p[P_MODE]) == MODE_KEYS;
    if (s->learn_armed) {
        s->learn_armed = 0;
        s->p[keys ? P_ROOT_NOTE : P_BASE_NOTE] = (float)note;
        set_text(s, s->status, sizeof s->status, keys ? "LEARNED: ROOT = NOTE %d" : "LEARNED: PAD 1 = NOTE %d", note);
        return;
    }
    if (keys) { start_pad(s, 0, note, vel); return; }
    int pad = note_to_pad(s, note);
    if (pad >= 0) start_pad(s, pad, note, vel);
}

static void take_handover(pw_t *s) {
    if (pthread_mutex_trylock(&s->xfer_mx)) return;
    int wake_worker = 0;
    if (s->next && !s->garbage) {
        s->garbage = s->cur;
        s->cur = s->next;
        s->next = NULL;
        memcpy(s->cue, s->cur->cue0, sizeof s->cue);
        for (int i = 0; i < NVOICES; i++) s->v[i].on = 0;
        s->last_pad = 0;
        wake_worker = s->garbage != NULL;
    }
    if (s->pend_cues && !s->next) {
        memcpy(s->cue, s->pend_cue, sizeof s->cue);
        s->pend_cues = 0;
    }
    pthread_mutex_unlock(&s->xfer_mx);
    if (wake_worker) { pthread_mutex_lock(&s->mx); pthread_cond_signal(&s->cv); pthread_mutex_unlock(&s->mx); }
}

static void render(void *inst, int16_t *out_lr, int frames) {
    pw_t *s = inst;
    take_handover(s);
    song_t *S = s->cur;
    float mixl[128], mixr[128], l[128], r[128];
    while (frames > 0) {
        int n = frames > 128 ? 128 : frames;
        memset(mixl, 0, sizeof mixl); memset(mixr, 0, sizeof mixr);
        if (S) for (int k = 0; k < NVOICES; k++) {
            voice_t *v = &s->v[k];
            if (!v->on) continue;
            int got = voice_render(s, v, S, l, r, n);
            for (int i = 0; i < got; i++) { mixl[i] += l[i]; mixr[i] += r[i]; }
        }
        for (int i = 0; i < n; i++) {
            float y[2] = {mixl[i], mixr[i]};
            for (int c = 0; c < 2; c++) {
                float x = y[c];
                if (x > 0.98f) x = 0.98f + 0.02f * tanhf((x - 0.98f) / 0.02f);
                else if (x < -0.98f) x = -0.98f - 0.02f * tanhf((-x - 0.98f) / 0.02f);
                out_lr[2 * i + c] = (int16_t)lrintf(x * 32767.0f);
            }
        }
        out_lr += 2 * n;
        frames -= n;
    }
}

/* ---------------- parameters ---------------- */
static int find(const char *key) {
    for (int i = 0; i < NP; i++) if (!strcmp(key, PKEY[i])) return i;
    return -1;
}

static void nudge(pw_t *s, int dir) {
    song_t *S = s->cur;
    if (!S) return;
    double beat = 60.0 / song_bpm(s, S) * FS;
    long step = (long)((int)lroundf(s->p[P_CUE_MODE]) == CUE_HITS ? beat / 4 : beat);
    int pad = s->last_pad;
    pthread_mutex_lock(&s->xfer_mx);   /* the worker copies the cues for SAVE PADS */
    long c = s->cue[pad] + dir * step;
    if (c < 0) c = 0;
    if (c > S->frames - FS) c = S->frames - FS;
    s->cue[pad] = c;
    pthread_mutex_unlock(&s->xfer_mx);
    double sec = (double)c / FS;
    set_text(s, s->status, sizeof s->status, "PAD %d CUE AT %d:%04.1f", pad + 1, (int)(sec / 60), fmod(sec, 60));
}

static void set_param(void *inst, const char *key, const char *val) {
    pw_t *s = inst;
    if (!strcmp(key, "lfo_bpm")) { s->host_bpm = (float)atof(val); return; }
    if (!strcmp(key, "state")) {   /* "mode=0;...;lib=3;file=/media/...;cues=1,2,...;pad=4" */
        s->quiet_until = time(NULL) + 3;
        char *tmp = strdup(val);
        if (!tmp) return;
        char *save = NULL;
        long c[NPADS]; int nc = 0;
        char file[512] = "";
        for (char *tok = strtok_r(tmp, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            const char *v = eq + 1;
            if (!strcmp(tok, "file")) snprintf(file, sizeof file, "%s", v);
            else if (!strcmp(tok, "lib")) s->saved_lib = atoi(v);
            else if (!strcmp(tok, "tsh")) { int t = atoi(v); s->tshift = t < -2 ? -2 : t > 2 ? 2 : t; }
            else if (!strcmp(tok, "pad")) { int p = atoi(v); s->last_pad = p >= 0 && p < NPADS ? p : 0; }
            else if (!strcmp(tok, "cues")) {
                char *sv2 = NULL;
                for (char *t = strtok_r((char *)v, ",", &sv2); t && nc < NPADS; t = strtok_r(NULL, ",", &sv2)) c[nc++] = atol(t);
            } else {
                int i = find(tok);
                if (i >= 0 && !is_trigger(i)) s->p[i] = (float)atof(v);
            }
        }
        free(tmp);
        if (file[0]) {
            pthread_mutex_lock(&s->mx);
            snprintf(s->restore_path, sizeof s->restore_path, "%s", file);
            if (nc == NPADS) memcpy(s->restore_cue, c, sizeof c);
            s->restore_ncue = nc;
            s->rq_restore = 1;
            pthread_cond_signal(&s->cv);
            pthread_mutex_unlock(&s->mx);
        }
        return;
    }
    if (!strncmp(key, "pick_", 5)) {   /* a list row: any tap (the row toggles either way) loads that song */
        int row = atoi(key + 5) - 1;
        if (row < 0 || row >= NROWS || time(NULL) < s->quiet_until) return;
        pthread_mutex_lock(&s->lib_mx);
        int first = lib_first(s, s->cur_lib), cnt = s->cur_lib < s->libs.nlib ? s->libs.lib[s->cur_lib].count : 0;
        int k = s->bpage * NROWS + row, idx = (first >= 0 && k < cnt) ? first + k : -1;
        pthread_mutex_unlock(&s->lib_mx);
        if (idx >= 0) wake(s, &s->rq_pick, idx + 1);
        return;
    }
    int i = find(key);
    if (i < 0) return;
    float v = (float)atof(val);
    int press = v > 0.5f;
    switch (i) {
    case P_RANDOM: if (press) wake(s, &s->rq_random, 1); return;
    case P_PREV: if (press) wake(s, &s->rq_prev, 1); return;
    case P_NEXT: if (press) wake(s, &s->rq_next, 1); return;
    case P_LIB_NEXT: if (press) wake(s, &s->rq_lib, 1); return;
    case P_FIND_CUES: if (press) wake(s, &s->rq_cues, 2); return;
    case P_NUDGE_BACK: if (press) nudge(s, -1); return;
    case P_NUDGE_FWD: if (press) nudge(s, 1); return;
    case P_PLAY_SONG:
        if (press && s->cur) {
            for (int k = 0; k < NVOICES; k++) if (s->v[k].on) release_voice(&s->v[k]);
            voice_t *vv = &s->v[alloc_voice(s)];
            voice_setup(s, vv, s->cur, 0, s->cur->frames, 0, PLAY_TO_NEXT, 0);
            vv->pad = -1;
            set_text(s, s->status, sizeof s->status, "PLAYING THE WHOLE SONG - TAP STOP");
        }
        return;
    case P_STOP:
        if (press) { for (int k = 0; k < NVOICES; k++) release_voice(&s->v[k]); set_text(s, s->status, sizeof s->status, "STOPPED"); }
        return;
    case P_SAVE_PADS: if (press) wake(s, &s->rq_save, 1); return;
    case P_LEARN:
        if (press) {
            s->learn_armed = 1;
            set_text(s, s->status, sizeof s->status, (int)lroundf(s->p[P_MODE]) == MODE_KEYS ?
                     "LEARN: PLAY THE NOTE YOU WANT AS THE ROOT" : "LEARN: HIT THE PAD YOU WANT AS PAD 1");
        }
        return;
    case P_RESCAN: if (press) wake(s, &s->rq_rescan, 1); return;
    case P_TEMPO_X2: case P_TEMPO_HALF:
        if (press) {
            int t = s->tshift + (i == P_TEMPO_X2 ? 1 : -1);
            if (t < -2 || t > 2) { set_text(s, s->status, sizeof s->status, "TEMPO CAN'T GO FURTHER"); return; }
            s->tshift = t;
            song_t *S = s->cur;
            if (S) {
                show_info(s, S);
                set_text(s, s->status, sizeof s->status, "TEMPO NOW %.1f BPM%s", song_bpm(s, S), t ? "  (TAP THE OTHER ONE TO GO BACK)" : "");
            }
        }
        return;
    case P_BROWSE_LIB:
        if (press) { pthread_mutex_lock(&s->mx); s->rq_lib_browse = 1; pthread_mutex_unlock(&s->mx); wake(s, &s->rq_lib, 1); }
        return;
    case P_PAGE_UP: case P_PAGE_DOWN:
        if (press) {
            pthread_mutex_lock(&s->lib_mx);
            int lib = s->cur_lib, cnt = lib >= 0 && lib < s->libs.nlib ? s->libs.lib[lib].count : 0;
            int pages = cnt > 0 ? (cnt + NROWS - 1) / NROWS : 1;
            s->bpage = (s->bpage + (i == P_PAGE_DOWN ? 1 : -1) + pages) % pages;   /* wraps round */
            pthread_mutex_unlock(&s->lib_mx);
            pthread_mutex_lock(&s->text_mx); s->display_rev++; pthread_mutex_unlock(&s->text_mx);
        }
        return;
    case P_LIB_NAME: case P_TRACK_NAME: case P_INFO: case P_STATUS: case P_BROWSE_INFO: return;
    case P_CUE_MODE: {
        int changed = (int)lroundf(s->p[i]) != (int)lroundf(v);
        s->p[i] = v;
        if (changed) wake(s, &s->rq_cues, 1);
        return;
    }
    case P_KEY:
        s->p[i] = v;
        if (s->cur) show_info(s, s->cur);
        return;
    case P_MODE:
        s->p[i] = v;
        if ((int)lroundf(v) == MODE_KEYS) set_text(s, s->status, sizeof s->status, "KEYS: PAD %d ACROSS THE KEYBOARD", s->last_pad + 1);
        else set_text(s, s->status, sizeof s->status, "PADS: 16 CUES");
        return;
    default: s->p[i] = v; return;
    }
}

static int get_param(void *inst, const char *key, char *buf, int buf_len) {
    pw_t *s = inst;
    if (!strcmp(key, "display_rev")) {
        pthread_mutex_lock(&s->text_mx);
        int n = snprintf(buf, buf_len, "%u", s->display_rev);
        pthread_mutex_unlock(&s->text_mx);
        return n;
    }
    if (!strncmp(key, "pick_", 5)) {   /* "pick_3": the 3rd name on the page; "pick_3_on": is it the song loaded */
        int row = atoi(key + 5) - 1, on = strstr(key, "_on") != NULL;
        if (row < 0 || row >= NROWS) return 0;
        if (pthread_mutex_trylock(&s->lib_mx)) return snprintf(buf, buf_len, "%s", on ? "0" : " ");   /* rescanning */
        int first = lib_first(s, s->cur_lib), cnt = s->cur_lib < s->libs.nlib ? s->libs.lib[s->cur_lib].count : 0;
        int k = s->bpage * NROWS + row, idx = (first >= 0 && k < cnt) ? first + k : -1;
        int n;
        if (on) n = snprintf(buf, buf_len, "%d", idx >= 0 && idx == s->cur_entry);
        else if (idx < 0) n = snprintf(buf, buf_len, " ");
        else {
            const char *nm = s->libs.e[idx].name, *dot = strrchr(nm, '.');
            n = snprintf(buf, buf_len, "%.*s", (int)(dot ? dot - nm : (long)strlen(nm)), nm);
        }
        pthread_mutex_unlock(&s->lib_mx);
        return n;
    }
    if (!strcmp(key, "browse_info")) {
        if (pthread_mutex_trylock(&s->lib_mx)) return snprintf(buf, buf_len, "LOOKING FOR SONGS...");
        int n;
        if (!s->libs.n) n = snprintf(buf, buf_len, "NO SONGS FOUND - SEE THE WAX PAGE");
        else {
            const lib_t *l = &s->libs.lib[s->cur_lib < s->libs.nlib ? s->cur_lib : 0];
            int pages = l->count > 0 ? (l->count + NROWS - 1) / NROWS : 1;
            n = snprintf(buf, buf_len, "%s  (%d)   PAGE %d / %d", l->name, l->count, s->bpage + 1, pages);
        }
        pthread_mutex_unlock(&s->lib_mx);
        return n;
    }
    if (!strcmp(key, "state")) {
        int n = 0;
        static const int keep[] = {P_MODE, P_CUE_MODE, P_PLAY_MODE, P_TEMPO, P_KEY, P_PITCH, P_PITCH_MODE, P_CHOKE, P_VOLUME,
                                   P_RELEASE, P_BASE_NOTE, P_ROOT_NOTE};
        for (size_t k = 0; k < sizeof keep / sizeof *keep && n < buf_len; k++)
            n += snprintf(buf + n, buf_len - n, "%s=%g;", PKEY[keep[k]], (double)s->p[keep[k]]);
        pthread_mutex_lock(&s->mx);
        if (n < buf_len) n += snprintf(buf + n, buf_len - n, "tsh=%d;lib=%d;file=%s;pad=%d;cues=", s->tshift, s->cur_lib, s->saved_path, s->last_pad);
        pthread_mutex_unlock(&s->mx);
        for (int k = 0; k < NPADS && n < buf_len; k++) n += snprintf(buf + n, buf_len - n, "%ld%s", s->cue[k], k < NPADS - 1 ? "," : "");
        return n < buf_len ? n : buf_len - 1;
    }
    int i = find(key);
    if (i < 0) return 0;
    const char *txt = NULL;
    pthread_mutex_lock(&s->text_mx);
    char t[200];
    switch (i) {
    case P_LIB_NAME: txt = s->lib_name; break;
    case P_TRACK_NAME: txt = s->track_name; break;
    case P_INFO: txt = s->info; break;
    case P_STATUS:
        if (s->last_note >= 0 && !strncmp(s->status, "READY", 5)) {
            snprintf(t, sizeof t, "READY  |  PAD %d  |  LAST NOTE %d", s->last_pad + 1, s->last_note);
            txt = t;
        } else txt = s->status;
        break;
    default: break;
    }
    int n;
    if (txt) n = snprintf(buf, buf_len, "%s", txt);
    else if (is_trigger(i)) n = snprintf(buf, buf_len, "0");
    else n = snprintf(buf, buf_len, "%g", (double)s->p[i]);
    pthread_mutex_unlock(&s->text_mx);
    return n;
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, NULL, NULL };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
