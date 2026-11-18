#include <stdio.h>
#include <string.h>

#include "config.h"
#include "logs.h"
#include "media.h"
#include "monitors.h"
#include "paths.h"
#include "runner.h"

#ifdef MW_NO_GUI
static int gui_run(int argc, char **argv)
{
    (void)argc; (void)argv;
    fprintf(stderr, "This build has no GUI. Use -h for the command line options.\n");
    return 1;
}
#else
int gui_run(int argc, char **argv);
#endif

static void usage(const char *prog)
{
    printf("MagicWallpaper - mpvpaper front-end\n\n"
           "usage: %s            open the GUI\n"
           "       %s -auto      start every config marked as autostart, then exit\n"
           "       %s -stop      stop all running wallpapers\n"
           "       %s -status    list running wallpapers\n"
           "       %s -add FILE  copy a video into the library (+ thumbnail)\n"
           "       %s -h         this help\n",
           prog, prog, prog, prog, prog, prog);
}

static int cmd_auto(void)
{
    all_configs all;
    if (config_load_all(&all) < 0)
        return 1;

    monitor_list ml;
    int have_monitors = monitors_detect(&ml) > 0;

    int started = 0;
    for (int i = 0; i < all.num; i++)
    {
        config *c = &all.configs[i];
        if (!c->auto_start)
            continue;
        if (have_monitors)
            config_sync_monitors(c, &ml);
        int n = runner_start(c);
        if (n > 0)
            started += n;
    }

    printf("Started %d wallpaper(s)\n", started);
    config_free_all(&all);
    return 0;
}

static int cmd_status(void)
{
    run_state rs;
    if (runner_load(&rs) == 0)
    {
        printf("No wallpapers running\n");
        return 0;
    }
    for (int i = 0; i < rs.count; i++)
        printf("%-12s PID %d (config%d)\n", rs.e[i].monitor, (int)rs.e[i].pid, rs.e[i].config_id);
    return 0;
}

static int cmd_add(const char *src)
{
    char dest[MW_PATH_MAX];
    if (media_import(src, dest, sizeof dest) != 0)
    {
        fprintf(stderr, "Couldn't copy %s\n", src);
        return 1;
    }
    if (media_make_thumb(dest) != 0)
        fprintf(stderr, "Added, but the thumbnail failed (is ffmpeg installed?)\n");
    printf("%s\n", dest);
    return 0;
}

int main(int argc, char **argv)
{
    if (mw_ensure_dirs() != 0)
    {
        fprintf(stderr, "Couldn't create %s\n", mw_base_dir());
        return 1;
    }

    if (argc == 1)
        return gui_run(argc, argv);

    if (argc > 3)
    {
        log_info(-4, NULL, NULL);
        usage(argv[0]);
        return 1;
    }

    if (argc == 2)
    {
        if (!strcmp(argv[1], "-auto"))   return cmd_auto();
        if (!strcmp(argv[1], "-stop"))   { printf("Stopped %d\n", runner_stop_all()); return 0; }
        if (!strcmp(argv[1], "-status")) return cmd_status();
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { usage(argv[0]); return 0; }
    }
    else if (!strcmp(argv[1], "-add"))
        return cmd_add(argv[2]);

    log_info(-3, NULL, argv[1]);
    usage(argv[0]);
    return 1;
}
