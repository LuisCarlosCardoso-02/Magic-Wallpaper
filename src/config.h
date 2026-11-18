#ifndef CONFIG_H
#define CONFIG_H

#define MW_MAX_MONITORS 16

typedef struct {
    char id[32];        /* output name as mpvpaper expects it, e.g. "HDMI-A-1" */
    int  enabled;
} monitor;

typedef struct
{
    int  ID;
    char name[32];
    char wallpaper_path[256];   /* path of the COPY inside MagicWallpaper/wallpapers */
    char mpv_args[128];         /* passed to mpvpaper -o, e.g. "no-audio loop" */
    int  auto_start;
    int  monitor_count;
    monitor monitors[MW_MAX_MONITORS];
} config;

typedef struct
{
    int num;
    config *configs;
} all_configs;

void config_set_defaults(config *cfg, int id);
int  config_load_file(const char *path, config *cfg);   /* 0 ok */
int  config_load_all(all_configs *out);                 /* count, or -1 */
void config_free_all(all_configs *a);
int  config_save(const config *cfg);                    /* 0 ok */
int  config_delete(int id);
int  config_next_id(const all_configs *a);

#endif
