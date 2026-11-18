#ifndef MONITORS_H
#define MONITORS_H

#include "config.h"

typedef struct {
    int  count;
    char names[MW_MAX_MONITORS][32];
} monitor_list;

/* Lists connected outputs (e.g. "HDMI-A-1", "DP-1") using /sys/class/drm.
 * Returns the count, or -1 on error. */
int monitors_detect(monitor_list *ml);

/* Makes cfg->monitors match the detected outputs, keeping the enabled flag of
 * outputs already known; new outputs start enabled. Does nothing if ml is empty. */
void config_sync_monitors(config *cfg, const monitor_list *ml);

#endif
