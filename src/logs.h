#ifndef LOGS_H
#define LOGS_H

/**
 * args: int code, const char *name, const char *msg
 * code > 0  logs a warning
 * code < 0  logs a critical error
 * code = 0  logs a standard operation (name + msg)
 *
 * For warnings/errors, msg (may be NULL) is appended as extra detail.
 *
 * Warnings: 1 wrong image on PID, 2 wallpaper not found, 3 config dir missing,
 *           4 no config files, 5 thumbnail failed, 6 video copy failed
 * Errors:  -1 config tampered, -2 mpvpaper missing, -3 invalid arguments,
 *          -4 too many arguments, -5 failed to start process,
 *          -6 failed to write PID file, -7 failed to save config
 */
void log_info(int code, const char *name, const char *msg);

#endif
