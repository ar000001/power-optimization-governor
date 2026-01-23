/*
 * governor.c - Minimal closed-loop governor (pure C) for Pipe-All AlexNet on Khadas VIM3
 *
 * Goals:
 *  1) Start simple: fixed order=G-B-L, tune DVFS to hit performance target (FPS>=T and/or Lat<=T).
 *  2) If DVFS saturates and target not met: adjust partition points (pp1, pp2) to reduce bottleneck.
 *  3) Once target is met: greedily reduce DVFS to save power while keeping margin (constraint satisfaction).
 *
 * HOW IT WORKS (high level):
 *  - Each iteration:
 *      (a) apply config (freqs, partition points)
 *      (b) run inference command (N frames)
 *      (c) parse output.txt for FPS and latency
 *      (d) decision: UP / DOWN / HOLD on DVFS, or adjust partition if stuck
 *
 * IMPORTANT:
 *  - This file is PURE C (no C++ std::string).
 *  - It uses system() for simplicity. You can later replace with fork/exec for better control.
 *
 * Build on-board:
 *   gcc -O2 -Wall -Wextra -o governor governor.c
 *
 * Example usage (on-board):
 *   ./governor --mode onboard --fps_target 13.0 --lat_target_ms 120 --n_frames 200
 *
 * Example usage (host using ADB):
 *   ./governor --mode adb --fps_target 13.0 --lat_target_ms 120 --n_frames 200
 *
 * You must run as root (or have permissions) to write cpu freq sysfs.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------- USER CONFIG (EDIT ME) ---------------------------- */

// Paths on the board (used in both modes)
static const char *BOARD_WORKDIR      = "/data/local/Working_dir";
static const char *BOARD_BINARY       = "/data/local/Working_dir/graph_alexnet_all_pipe_sync";
static const char *BOARD_OUTPUT_TXT   = "/data/local/Working_dir/output.txt";

// Sysfs paths (common pattern: little policy at cpu0, big policy at cpu2)
static const char *LITTLE_MIN_PATH    = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq";
static const char *LITTLE_MAX_PATH    = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
static const char *BIG_MIN_PATH       = "/sys/devices/system/cpu/cpu2/cpufreq/scaling_min_freq";
static const char *BIG_MAX_PATH       = "/sys/devices/system/cpu/cpu2/cpufreq/scaling_max_freq";

static const char *LITTLE_AVAIL_PATH  = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies";
static const char *BIG_AVAIL_PATH     = "/sys/devices/system/cpu/cpu2/cpufreq/scaling_available_frequencies";

// Fixed order for now (you can extend later)
static const char *ORDER_FIXED        = "G-B-L";

/* ---------------------------- Helpers: file I/O ---------------------------- */

static int write_ulong_to_file(const char *path, unsigned long v) {
    FILE *f = fopen(path, "w");
    if (!f) return -errno;
    if (fprintf(f, "%lu", v) < 0) { fclose(f); return -EIO; }
    fclose(f);
    return 0;
}

static char *read_file_alloc(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)calloc((size_t)n + 1, 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = '\0';
    return buf;
}

/* ---------------------------- Parsing available freqs ---------------------------- */

typedef struct {
    unsigned long little[64]; size_t little_n;
    unsigned long big[64];    size_t big_n;
} freq_table_t;

static size_t parse_freq_list(const char *text, unsigned long *out, size_t cap) {
    size_t n = 0;
    const char *p = text;
    while (*p && n < cap) {
        while (*p == ' ' || *p == '\n' || *p == '\t') p++;
        if (!*p) break;
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

static int load_freq_tables(freq_table_t *t) {
    memset(t, 0, sizeof(*t));
    char *l = read_file_alloc(LITTLE_AVAIL_PATH);
    char *b = read_file_alloc(BIG_AVAIL_PATH);
    if (!l || !b) {
        free(l); free(b);
        return -ENOENT;
    }
    t->little_n = parse_freq_list(l, t->little, 64);
    t->big_n    = parse_freq_list(b, t->big, 64);
    free(l); free(b);
    if (t->little_n == 0 || t->big_n == 0) return -EINVAL;
    return 0;
}

static unsigned long step_freq(const unsigned long *f, size_t n, unsigned long cur, int dir) {
    // dir: +1 => up, -1 => down
    if (n == 0) return cur;
    // find nearest index >= cur
    size_t i = 0;
    while (i + 1 < n && f[i] < cur) i++;
    if (dir > 0) {
        if (f[i] < cur && i + 1 < n) return f[i + 1];
        if (i + 1 < n) return f[i + 1];
        return f[n - 1];
    } else {
        if (f[i] > cur && i > 0) return f[i - 1];
        if (i > 0) return f[i - 1];
        return f[0];
    }
}

/* ---------------------------- Pipe-All output parsing ---------------------------- */

typedef struct {
    double fps;
    double latency_ms;
    bool has_fps;
    bool has_latency;
} stats_t;

/*
 * Parses lines like:
 *   Frame rate is: 13.4213 FPS
 *   Frame latency is: 119.201 ms
 */
static stats_t parse_pipeall_output_text(const char *text) {
    stats_t s; memset(&s, 0, sizeof(s));

    // Find FPS
    const char *p = strstr(text, "Frame rate is:");
    if (p) {
        // move to first digit
        while (*p && !((*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) p++;
        if (*p) {
            s.fps = strtod(p, NULL);
            s.has_fps = true;
        }
    }

    // Find latency
    p = strstr(text, "Frame latency is:");
    if (p) {
        while (*p && !((*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) p++;
        if (*p) {
            s.latency_ms = strtod(p, NULL);
            s.has_latency = true;
        }
    }

    return s;
}

/* ---------------------------- Execution mode ---------------------------- */

typedef enum { MODE_ONBOARD, MODE_ADB } mode_t;

typedef struct {
    mode_t mode;
    char   adb_serial[128];  // optional: "-s SERIAL"
} exec_ctx_t;

static void adb_prefix(const exec_ctx_t *ctx, char *out, size_t out_sz) {
    if (ctx->mode != MODE_ADB) { out[0] = '\0'; return; }
    if (ctx->adb_serial[0]) snprintf(out, out_sz, "adb -s %s ", ctx->adb_serial);
    else snprintf(out, out_sz, "adb ");
}

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Command failed (%d): %s\n", rc, cmd);
        return -1;
    }
    return 0;
}

/* ---------------------------- Apply DVFS (lock min=max) ---------------------------- */

static int set_cluster_freqs_onboard(unsigned long little_khz, unsigned long big_khz) {
    // Lock min=max for stability (works across governors)
    int rc;
    rc = write_ulong_to_file(LITTLE_MIN_PATH, little_khz); if (rc) return rc;
    rc = write_ulong_to_file(LITTLE_MAX_PATH, little_khz); if (rc) return rc;
    rc = write_ulong_to_file(BIG_MIN_PATH, big_khz);       if (rc) return rc;
    rc = write_ulong_to_file(BIG_MAX_PATH, big_khz);       if (rc) return rc;
    return 0;
}

static int set_cluster_freqs(const exec_ctx_t *ctx, unsigned long little_khz, unsigned long big_khz) {
    if (ctx->mode == MODE_ONBOARD) {
        return set_cluster_freqs_onboard(little_khz, big_khz);
    }
    // MODE_ADB: write via adb shell "echo"
    char adb[256]; adb_prefix(ctx, adb, sizeof(adb));
    char cmd[1024];

    snprintf(cmd, sizeof(cmd),
        "%sshell \"su -c 'echo %lu > %s; echo %lu > %s; echo %lu > %s; echo %lu > %s'\"",
        adb,
        little_khz, LITTLE_MIN_PATH,
        little_khz, LITTLE_MAX_PATH,
        big_khz,    BIG_MIN_PATH,
        big_khz,    BIG_MAX_PATH
    );
    return run_cmd(cmd);
}

/* ---------------------------- Run inference and fetch output ---------------------------- */

typedef struct {
    // pipe-all settings
    int threads_big;
    int threads_little;
    int n_frames;
    int pp1;
    int pp2;
    char order[6];     // "G-B-L"
    char target[8];    // "CL" or "NEON"
} pipe_cfg_t;

static int run_inference_once(const exec_ctx_t *ctx, const pipe_cfg_t *cfg) {
    char adb[256]; adb_prefix(ctx, adb, sizeof(adb));
    char cmd[2048];

    if (ctx->mode == MODE_ONBOARD) {
        // Run on board, redirect output to output.txt
        snprintf(cmd, sizeof(cmd),
            "cd %s && %s --threads=%d --threads2=%d --target=%s --n=%d "
            "--partition_point=%d --partition_point2=%d --order=%s > output.txt 2>&1",
            BOARD_WORKDIR, BOARD_BINARY,
            cfg->threads_big, cfg->threads_little, cfg->target, cfg->n_frames,
            cfg->pp1, cfg->pp2, cfg->order
        );
        return run_cmd(cmd);
    }

    // ADB mode: run remotely, redirect on-board output file
    snprintf(cmd, sizeof(cmd),
        "%sshell \"su -c 'cd %s && %s --threads=%d --threads2=%d --target=%s --n=%d "
        "--partition_point=%d --partition_point2=%d --order=%s > output.txt 2>&1'\"",
        adb,
        BOARD_WORKDIR, BOARD_BINARY,
        cfg->threads_big, cfg->threads_little, cfg->target, cfg->n_frames,
        cfg->pp1, cfg->pp2, cfg->order
    );
    return run_cmd(cmd);
}

static int read_output_text(const exec_ctx_t *ctx, char **out_text) {
    *out_text = NULL;

    if (ctx->mode == MODE_ONBOARD) {
        char path[512];
        snprintf(path, sizeof(path), "%s/output.txt", BOARD_WORKDIR);
        char *txt = read_file_alloc(path);
        if (!txt) return -ENOENT;
        *out_text = txt;
        return 0;
    }

    // ADB mode: pull to local temp file then read
    char adb[256]; adb_prefix(ctx, adb, sizeof(adb));
    char cmd[1024];

    const char *local_tmp = "./output_tmp.txt";
    snprintf(cmd, sizeof(cmd),
        "%spull %s %s > /dev/null",
        adb, BOARD_OUTPUT_TXT, local_tmp
    );
    if (run_cmd(cmd) != 0) return -1;

    char *txt = read_file_alloc(local_tmp);
    if (!txt) return -ENOENT;
    *out_text = txt;
    return 0;
}

/* ---------------------------- Governor policy ---------------------------- */

typedef struct {
    double fps_target;
    double lat_target_ms;

    // margins create hysteresis and reduce oscillations
    double fps_margin;      // e.g., 0.08 => 8%
    double lat_margin_ms;   // e.g., 5 ms

    int confirm_up;         // require condition N times before changing
    int confirm_down;

    int max_iters;
} gov_policy_t;

typedef struct {
    int up_hits;
    int down_hits;
} gov_state_t;

static bool meets_target(const gov_policy_t *p, const stats_t *s) {
    if (!s->has_fps || !s->has_latency) return false;
    return (s->fps >= p->fps_target) && (s->latency_ms <= p->lat_target_ms);
}

static bool comfortably_above_target(const gov_policy_t *p, const stats_t *s) {
    if (!s->has_fps || !s->has_latency) return false;
    return (s->fps >= p->fps_target * (1.0 + p->fps_margin)) &&
           (s->latency_ms <= p->lat_target_ms - p->lat_margin_ms);
}

/*
 * Decide DVFS action:
 *  +1 => step up
 *  -1 => step down
 *   0 => hold
 */
static int decide_dvfs_action(const gov_policy_t *p, gov_state_t *st, const stats_t *s) {
    bool need_up = false;
    bool can_down = false;

    if (s->has_fps && s->fps < p->fps_target) need_up = true;
    if (s->has_latency && s->latency_ms > p->lat_target_ms) need_up = true;

    if (comfortably_above_target(p, s)) can_down = true;

    if (need_up) {
        st->up_hits++;
        st->down_hits = 0;
        if (st->up_hits >= p->confirm_up) { st->up_hits = 0; return +1; }
        return 0;
    }
    if (can_down) {
        st->down_hits++;
        st->up_hits = 0;
        if (st->down_hits >= p->confirm_down) { st->down_hits = 0; return -1; }
        return 0;
    }

    st->up_hits = 0;
    st->down_hits = 0;
    return 0;
}

/* ---------------------------- Partition tuning (order = G-B-L) ---------------------------- */

/*
 * For 3-stage pipeline with order G-B-L:
 *   Stage1: GPU handles layers [0 .. pp1)
 *   Stage2: Big handles [pp1 .. pp2)
 *   Stage3: Little handles [pp2 .. end)
 *
 * Without stage timing, we use a simple rule:
 *   - If we're missing target and DVFS is maxed:
 *       try shifting work away from Little (slow) by decreasing pp2 (smaller Little stage),
 *       but must keep pp2 > pp1.
 *   - If still failing: shift work away from Big by decreasing pp1 (smaller GPU stage means more for Big),
 *       OR increasing pp1 (more for GPU) depending on what you empirically saw.
 *
 * Since we lack per-stage timings in your output, we implement a conservative “shrink Little stage first”.
 *
 * You can upgrade later by parsing stage timings if you print them.
 */
static bool tune_partition_gbl(pipe_cfg_t *cfg) {
    // returns true if partition changed
    // Prefer to reduce Little stage size by moving pp2 towards the end? Actually:
    // Little stage is [pp2..end). To make it smaller, INCREASE pp2 (closer to end).
    // But pp2 max is total_parts (here AlexNet parts=8).
    // In your example pp2=8 => Little stage empty (2-stage GPU+Big).
    // So “shrink Little stage” means pushing pp2 up toward 8.

    const int TOTAL_PARTS = 8;

    if (cfg->pp2 < TOTAL_PARTS) {
        cfg->pp2 += 1;
        if (cfg->pp2 <= cfg->pp1) cfg->pp2 = cfg->pp1 + 1;
        if (cfg->pp2 > TOTAL_PARTS) cfg->pp2 = TOTAL_PARTS;
        return true;
    }

    // If Little already removed (pp2==8), try giving more to GPU by increasing pp1 (bigger GPU stage)
    // This helps if GPU is more efficient for early conv layers.
    if (cfg->pp1 < cfg->pp2 - 1) {
        cfg->pp1 += 1;
        if (cfg->pp1 >= cfg->pp2) cfg->pp1 = cfg->pp2 - 1;
        return true;
    }

    return false;
}

/* ---------------------------- Main ---------------------------- */

static void usage(const char *a0) {
    fprintf(stderr,
        "Usage: %s --mode onboard|adb [--serial SERIAL]\n"
        "          --fps_target X --lat_target_ms Y\n"
        "          [--n_frames N] [--threads_big N] [--threads_little N]\n"
        "          [--pp1 A --pp2 B] [--target CL|NEON]\n",
        a0);
}

int main(int argc, char **argv) {
    exec_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = MODE_ONBOARD;

    gov_policy_t pol = {
        .fps_target = 13.0,
        .lat_target_ms = 120.0,
        .fps_margin = 0.08,      // 8%
        .lat_margin_ms = 5.0,
        .confirm_up = 2,
        .confirm_down = 2,
        .max_iters = 30
    };

    pipe_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads_big = 4;
    cfg.threads_little = 2;
    cfg.n_frames = 200;
    cfg.pp1 = 5;
    cfg.pp2 = 8;
    snprintf(cfg.order, sizeof(cfg.order), "%s", ORDER_FIXED);
    snprintf(cfg.target, sizeof(cfg.target), "CL");

    // Parse args
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (!strcmp(m, "onboard")) ctx.mode = MODE_ONBOARD;
            else if (!strcmp(m, "adb")) ctx.mode = MODE_ADB;
            else { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--serial") && i + 1 < argc) {
            snprintf(ctx.adb_serial, sizeof(ctx.adb_serial), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--fps_target") && i + 1 < argc) {
            pol.fps_target = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--lat_target_ms") && i + 1 < argc) {
            pol.lat_target_ms = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--n_frames") && i + 1 < argc) {
            cfg.n_frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--threads_big") && i + 1 < argc) {
            cfg.threads_big = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--threads_little") && i + 1 < argc) {
            cfg.threads_little = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--pp1") && i + 1 < argc) {
            cfg.pp1 = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--pp2") && i + 1 < argc) {
            cfg.pp2 = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--target") && i + 1 < argc) {
            snprintf(cfg.target, sizeof(cfg.target), "%s", argv[++i]);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    // Load available frequency tables (must run on board for sysfs reading).
    // If MODE_ADB and you want host-only, you can hardcode freqs; simplest is: run governor on-board.
    freq_table_t ft;
    int rc = load_freq_tables(&ft);
    if (rc != 0) {
        fprintf(stderr, "Failed to load available frequencies from sysfs (%d). "
                        "Run on-board or hardcode frequency lists.\n", rc);
        return 1;
    }

    // Start at mid frequencies (good default)
    unsigned long little_khz = ft.little[ft.little_n / 2];
    unsigned long big_khz    = ft.big[ft.big_n / 2];

    gov_state_t st = {0};

    fprintf(stderr,
        "Governor start: mode=%s order=%s pp1=%d pp2=%d target=%s\n"
        "Targets: fps>=%.2f, lat<=%.1fms. Start freqs: little=%lu kHz big=%lu kHz\n",
        (ctx.mode == MODE_ONBOARD ? "onboard" : "adb"),
        cfg.order, cfg.pp1, cfg.pp2, cfg.target,
        pol.fps_target, pol.lat_target_ms,
        little_khz, big_khz);

    for (int iter = 0; iter < pol.max_iters; iter++) {
        // 1) Apply DVFS
        rc = set_cluster_freqs(&ctx, little_khz, big_khz);
        if (rc != 0) fprintf(stderr, "Warning: DVFS set returned %d\n", rc);

        // 2) Run inference
        if (run_inference_once(&ctx, &cfg) != 0) {
            fprintf(stderr, "Inference failed. Check binary path and permissions.\n");
            return 1;
        }

        // 3) Read and parse output
        char *txt = NULL;
        if (read_output_text(&ctx, &txt) != 0) {
            fprintf(stderr, "Failed to read output.txt\n");
            return 1;
        }
        stats_t s = parse_pipeall_output_text(txt);
        free(txt);

        if (!s.has_fps || !s.has_latency) {
            fprintf(stderr, "Could not parse FPS/latency. Check output format.\n");
            return 1;
        }

        // Print iteration summary (log this to CSV later)
        fprintf(stderr,
            "[%02d] pp=%d-%d little=%lu big=%lu  FPS=%.3f  Lat=%.3fms\n",
            iter, cfg.pp1, cfg.pp2, little_khz, big_khz, s.fps, s.latency_ms);

        // 4) Decision: DVFS action
        int act = decide_dvfs_action(&pol, &st, &s);

        // Identify bounds
        unsigned long little_min = ft.little[0], little_max = ft.little[ft.little_n - 1];
        unsigned long big_min    = ft.big[0],    big_max    = ft.big[ft.big_n - 1];

        bool at_max = (little_khz == little_max) && (big_khz == big_max);
        bool at_min = (little_khz == little_min) && (big_khz == big_min);

        if (act > 0) {
            // STEP UP: Little first, then Big (cheaper)
            unsigned long next_l = step_freq(ft.little, ft.little_n, little_khz, +1);
            if (next_l != little_khz) little_khz = next_l;
            else big_khz = step_freq(ft.big, ft.big_n, big_khz, +1);
            continue;
        }

        if (act < 0) {
            // STEP DOWN: Big first, then Little (saves more power)
            unsigned long next_b = step_freq(ft.big, ft.big_n, big_khz, -1);
            if (next_b != big_khz) big_khz = next_b;
            else little_khz = step_freq(ft.little, ft.little_n, little_khz, -1);
            continue;
        }

        // HOLD: if we meet target, try power trimming occasionally (greedy)
        if (meets_target(&pol, &s)) {
            if (!at_min) {
                // Try one-step-down trial next loop (only if comfortably above target)
                if (comfortably_above_target(&pol, &s)) {
                    unsigned long trial_b = step_freq(ft.big, ft.big_n, big_khz, -1);
                    if (trial_b != big_khz) { big_khz = trial_b; continue; }
                    unsigned long trial_l = step_freq(ft.little, ft.little_n, little_khz, -1);
                    if (trial_l != little_khz) { little_khz = trial_l; continue; }
                }
            }
            fprintf(stderr, "Target met and stable. (Stop or keep trimming)\n");
            // You can break here if you want "find config then exit"
            // break;
        } else {
            // Not meeting target AND act==0 means we’re within hysteresis window
            // If we’re fully maxed and still failing, tune partition.
            if (at_max) {
                bool changed = tune_partition_gbl(&cfg);
                if (changed) {
                    fprintf(stderr, "DVFS maxed; tuning partition => pp=%d-%d\n", cfg.pp1, cfg.pp2);
                    // after partition change, reset DVFS to midrange to avoid wasting power
                    little_khz = ft.little[ft.little_n / 2];
                    big_khz    = ft.big[ft.big_n / 2];
                    continue;
                } else {
                    fprintf(stderr, "DVFS maxed and no partition moves left. Cannot meet target.\n");
                    break;
                }
            } else {
                // If not at max, just wait for confirm windows to trigger step-up
            }
        }

        // small sleep to avoid spamming (optional)
        // usleep(100 * 1000);
    }

    return 0;
}
