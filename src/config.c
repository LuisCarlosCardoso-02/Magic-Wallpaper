#include "config.h"
#include "logs.h"
#include "monitors.h"
#include "paths.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LEN(s) (sizeof(s) - 1)

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void copy_str(char *dst, size_t n, const char *src)
{
    size_t l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

void config_set_defaults(config *cfg, int id)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->ID = id;
    snprintf(cfg->name, sizeof cfg->name, "Config %d", id);
    copy_str(cfg->mpv_args, sizeof cfg->mpv_args, "no-audio loop");

    monitor_list ml;
    if (monitors_detect(&ml) > 0)
        config_sync_monitors(cfg, &ml);
}

int config_load_file(const char *path, config *cfg)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    memset(cfg, 0, sizeof *cfg);

    char line[640];
    int monitor_idx = 0;
    int in_monitor_block = 0;

    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '#' || line[0] == '\0')
            continue;

        if (strcmp(line, "///") == 0) { in_monitor_block = !in_monitor_block; continue; }
        if (strcmp(line, "---") == 0) { monitor_idx++; continue; }
        if (line[0] != '*')
            continue;

        const char *c = line + 1;

        if (starts_with(c, "config>>"))
            sscanf(c + LEN("config>>"), "config%d", &cfg->ID);
        else if (starts_with(c, "name>>"))
            copy_str(cfg->name, sizeof cfg->name, c + LEN("name>>"));
        else if (starts_with(c, "path>>"))
            copy_str(cfg->wallpaper_path, sizeof cfg->wallpaper_path, c + LEN("path>>"));
        else if (starts_with(c, "mpv>>"))
            copy_str(cfg->mpv_args, sizeof cfg->mpv_args, c + LEN("mpv>>"));
        else if (starts_with(c, "auto>>"))
            cfg->auto_start = atoi(c + LEN("auto>>"));
        else if (in_monitor_block && monitor_idx < MW_MAX_MONITORS)
        {
            if (starts_with(c, "id>>"))
            {
                copy_str(cfg->monitors[monitor_idx].id,
                         sizeof cfg->monitors[monitor_idx].id, c + LEN("id>>"));
                if (monitor_idx + 1 > cfg->monitor_count)
                    cfg->monitor_count = monitor_idx + 1;
            }
            else if (starts_with(c, "enabled>>"))
            {
                cfg->monitors[monitor_idx].enabled = atoi(c + LEN("enabled>>"));
                if (monitor_idx + 1 > cfg->monitor_count)
                    cfg->monitor_count = monitor_idx + 1;
            }
        }
    }

    fclose(f);
    return cfg->ID > 0 ? 0 : -1;
}

static int cmp_id(const void *a, const void *b)
{
    return ((const config *)a)->ID - ((const config *)b)->ID;
}

int config_load_all(all_configs *out)
{
    out->num = 0;
    out->configs = NULL;

    char dir[MW_PATH_MAX];
    mw_path(dir, sizeof dir, "config", NULL);

    DIR *d = opendir(dir);
    if (!d)
    {
        log_info(3, NULL, dir);
        return -1;
    }

    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcmp(e->d_name + l - 4, ".cfg") != 0)
            continue;

        char p[MW_PATH_MAX];
        mw_path(p, sizeof p, "config", e->d_name);

        config c;
        if (config_load_file(p, &c) != 0)
        {
            log_info(-1, NULL, p);
            continue;
        }

        if (out->num == cap)
        {
            cap = cap ? cap * 2 : 4;
            config *n = realloc(out->configs, (size_t)cap * sizeof(config));
            if (!n)
            {
                closedir(d);
                config_free_all(out);
                return -1;
            }
            out->configs = n;
        }
        out->configs[out->num++] = c;
    }
    closedir(d);

    if (out->num > 1)
        qsort(out->configs, (size_t)out->num, sizeof(config), cmp_id);

    return out->num;
}

void config_free_all(all_configs *a)
{
    free(a->configs);
    a->configs = NULL;
    a->num = 0;
}

int config_save(const config *cfg)
{
    if (mw_ensure_dirs() != 0)
        return -1;

    char fname[64], path[MW_PATH_MAX], tmp[MW_PATH_MAX + 8];
    snprintf(fname, sizeof fname, "config%d.cfg", cfg->ID);
    mw_path(path, sizeof path, "config", fname);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f)
    {
        log_info(-7, NULL, tmp);
        return -1;
    }

    fprintf(f, "##configID and name\n");
    fprintf(f, "*config>>config%d\n", cfg->ID);
    fprintf(f, "*name>>%s\n", cfg->name);
    fprintf(f, "##path to wallpaper (copy inside MagicWallpaper/wallpapers)\n");
    fprintf(f, "*path>>%s\n", cfg->wallpaper_path);
    fprintf(f, "##options passed to mpvpaper -o\n");
    fprintf(f, "*mpv>>%s\n", cfg->mpv_args);
    fprintf(f, "##autostart 0=false\n");
    fprintf(f, "*auto>>%d\n", cfg->auto_start);
    fprintf(f, "##monitors data, monitor Id and a flag for turn on and off\n");
    fprintf(f, "///\n");
    for (int i = 0; i < cfg->monitor_count; i++)
    {
        fprintf(f, "*id>>%s\n", cfg->monitors[i].id);
        fprintf(f, "*enabled>>%d\n", cfg->monitors[i].enabled);
        if (i != cfg->monitor_count - 1)
            fprintf(f, "---\n");
    }
    fprintf(f, "///\n");

    if (fclose(f) != 0 || rename(tmp, path) != 0)
    {
        unlink(tmp);
        log_info(-7, NULL, path);
        return -1;
    }
    return 0;
}

int config_delete(int id)
{
    char fname[64], path[MW_PATH_MAX];
    snprintf(fname, sizeof fname, "config%d.cfg", id);
    mw_path(path, sizeof path, "config", fname);
    return unlink(path);
}

int config_next_id(const all_configs *a)
{
    int max = 0;
    for (int i = 0; i < a->num; i++)
        if (a->configs[i].ID > max)
            max = a->configs[i].ID;
    return max + 1;
}
