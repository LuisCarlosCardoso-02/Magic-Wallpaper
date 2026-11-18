#ifndef RUNNER_H
#define RUNNER_H

#include <sys/types.h>
#include "config.h"

#define MW_MAX_RUN 32

typedef struct {
    pid_t pid;
    int   config_id;
    char  monitor[32];
} run_entry;

typedef struct {
    int       count;
    run_entry e[MW_MAX_RUN];
} run_state;

/* Reads run/pids.txt, drops entries whose process is gone (or whose PID now
 * belongs to another program) and rewrites the file. Returns entry count. */
int runner_load(run_state *rs);

/* Starts one mpvpaper per enabled monitor of cfg (replacing whatever already
 * runs on that monitor). Returns number started, -1 wallpaper missing,
 * -2 mpvpaper not installed. */
int runner_start(const config *cfg);

int runner_stop_monitor(const char *monitor);
int runner_stop_all(void);   /* returns number stopped */

#endif
