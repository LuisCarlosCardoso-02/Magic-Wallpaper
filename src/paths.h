#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

#define MW_PATH_MAX 1024

/* Base data directory: $XDG_DATA_HOME/MagicWallpaper
 * (falls back to ~/.local/share/MagicWallpaper).
 * Layout:
 *   wallpapers/  copies of the videos you added
 *   thumbs/      jpg thumbnails shown in the UI
 *   config/      config<ID>.cfg files
 *   run/         pids.txt (running mpvpaper processes) + per-monitor logs
 *   log.txt      program log
 */
const char *mw_base_dir(void);

/* Builds "<base>/<sub>/<name>". sub and/or name may be NULL. */
void mw_path(char *out, size_t n, const char *sub, const char *name);

/* Creates the base directory and all subdirectories. Returns 0 on success. */
int mw_ensure_dirs(void);

#endif
