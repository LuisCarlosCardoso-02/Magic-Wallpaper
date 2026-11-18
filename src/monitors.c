#include "monitors.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int monitors_detect(monitor_list *ml)
{
    ml->count = 0;

    DIR *d = opendir("/sys/class/drm");
    if (!d)
        return -1;

    struct dirent *e;
    while ((e = readdir(d)) != NULL && ml->count < MW_MAX_MONITORS)
    {
        /* entries look like "card0-HDMI-A-1", "card1-DP-2", "card0-eDP-1" */
        if (strncmp(e->d_name, "card", 4) != 0)
            continue;
        const char *dash = strchr(e->d_name, '-');
        if (!dash || !dash[1])
            continue;

        char status_path[512];
        snprintf(status_path, sizeof status_path, "/sys/class/drm/%s/status", e->d_name);

        FILE *f = fopen(status_path, "r");
        if (!f)
            continue;

        char status[32] = "";
        if (fgets(status, sizeof status, f))
            status[strcspn(status, "\n")] = 0;
        fclose(f);

        if (strcmp(status, "connected") != 0)
            continue;

        snprintf(ml->names[ml->count], sizeof ml->names[0], "%s", dash + 1);
        ml->count++;
    }
    closedir(d);

    qsort(ml->names, (size_t)ml->count, sizeof ml->names[0], cmp_names);
    return ml->count;
}

void config_sync_monitors(config *cfg, const monitor_list *ml)
{
    if (ml->count <= 0)
        return;

    monitor fresh[MW_MAX_MONITORS];
    memset(fresh, 0, sizeof fresh);

    for (int i = 0; i < ml->count; i++)
    {
        snprintf(fresh[i].id, sizeof fresh[i].id, "%s", ml->names[i]);
        fresh[i].enabled = 1;
        for (int j = 0; j < cfg->monitor_count; j++)
            if (strcmp(cfg->monitors[j].id, ml->names[i]) == 0)
                fresh[i].enabled = cfg->monitors[j].enabled;
    }

    memcpy(cfg->monitors, fresh, sizeof fresh);
    cfg->monitor_count = ml->count;
}
