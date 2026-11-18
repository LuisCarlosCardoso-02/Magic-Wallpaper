#ifndef MEDIA_H
#define MEDIA_H

#include <stddef.h>

/* Copies src into <base>/wallpapers (adds -1, -2... if the name is taken by a
 * different file; reuses the existing copy if it is the same size).
 * dest receives the path of the copy. Returns 0 on success. */
int media_import(const char *src, char *dest, size_t n);

/* Path of the thumbnail for a video copy (does not check it exists). */
void media_thumb_path(const char *video_path, char *out, size_t n);

/* Creates the thumbnail with ffmpeg if missing. Returns 0 on success. */
int media_make_thumb(const char *video_path);

/* Lists file names in the wallpapers dir, sorted. Free with media_list_free. */
char **media_list(int *count);
void media_list_free(char **list, int count);

/* Deletes the copy and its thumbnail. */
int media_remove(const char *video_path);

#endif
