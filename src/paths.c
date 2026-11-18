#include "paths.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char base[MW_PATH_MAX];

const char *mw_base_dir(void)
{
    if (base[0])
        return base;

    const char *xdg  = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");

    if (xdg && *xdg)
        snprintf(base, sizeof base, "%s/MagicWallpaper", xdg);
    else if (home && *home)
        snprintf(base, sizeof base, "%s/.local/share/MagicWallpaper", home);
    else
        snprintf(base, sizeof base, "/tmp/MagicWallpaper");

    return base;
}

void mw_path(char *out, size_t n, const char *sub, const char *name)
{
    if (sub && name)
        snprintf(out, n, "%s/%s/%s", mw_base_dir(), sub, name);
    else if (sub)
        snprintf(out, n, "%s/%s", mw_base_dir(), sub);
    else if (name)
        snprintf(out, n, "%s/%s", mw_base_dir(), name);
    else
        snprintf(out, n, "%s", mw_base_dir());
}

static int mkdir_p(const char *path)
{
    char tmp[MW_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

int mw_ensure_dirs(void)
{
    static const char *subs[] = { NULL, "wallpapers", "thumbs", "config", "run" };
    char p[MW_PATH_MAX];

    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++)
    {
        mw_path(p, sizeof p, subs[i], NULL);
        if (mkdir_p(p) != 0)
            return -1;
    }
    return 0;
}
