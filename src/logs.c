#include "logs.h"
#include "paths.h"

#include <stdio.h>
#include <time.h>

static const char *warning_text(int code)
{
    switch (code)
    {
    case 1:  return "Wrong image on PID (PID belongs to another program, entry dropped)";
    case 2:  return "Couldn't find wallpaper through path in config file";
    case 3:  return "Couldn't find config directory inside";
    case 4:  return "Can't find config files";
    case 5:  return "Failed to create video thumbnail (is ffmpeg installed?)";
    case 6:  return "Failed to copy video into the wallpapers directory";
    default: return "I feel a disturbance in the force.";
    }
}

static const char *error_text(int code)
{
    switch (code)
    {
    case 1:  return "Config file was wrongfully tampered";
    case 2:  return "Couldn't find mpvpaper or it is corrupted";
    case 3:  return "Invalid arguments on start";
    case 4:  return "Too many Arguments";
    case 5:  return "Failed to start the mpvpaper process";
    case 6:  return "Failed to write the PID file";
    case 7:  return "Failed to save config file";
    default: return "Something is terribly wrong in the force.";
    }
}

void log_info(int code, const char *name, const char *msg)
{
    char path[MW_PATH_MAX];
    mw_path(path, sizeof path, NULL, "log.txt");

    FILE *f = fopen(path, "a");
    if (!f)
    {
        mw_ensure_dirs();
        f = fopen(path, "a");
    }
    if (!f)
    {
        fprintf(stderr, "Couldn't open %s\n", path);
        return;
    }

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    if (code == 0)
    {
        fprintf(f, "Operation: %s\n", name ? name : "-");
        fprintf(f, "info: %s\n", ts);
        fprintf(f, "%s\n\n", msg ? msg : "");
    }
    else if (code < 0)
    {
        fprintf(f, "///////////////////////////////////////\n");
        fprintf(f, "ERROR!!: %s\n", ts);
        fprintf(f, "%s\n", error_text(-code));
        if (msg) fprintf(f, "details: %s\n", msg);
        fprintf(f, "///////////////////////////////////////\n\n");
    }
    else
    {
        fprintf(f, "========================================\n");
        fprintf(f, "WARNING: %s\n", ts);
        fprintf(f, "%s\n", warning_text(code));
        if (msg) fprintf(f, "details: %s\n", msg);
        fprintf(f, "========================================\n\n");
    }

    fclose(f);
}
