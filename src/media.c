#include "media.h"
#include "logs.h"
#include "paths.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

void media_thumb_path(const char *video_path, char *out, size_t n)
{
    char name[300];
    snprintf(name, sizeof name, "%s.jpg", base_name(video_path));
    mw_path(out, n, "thumbs", name);
}

static int copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0)
        return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0)
    {
        close(in);
        return -1;
    }

    const size_t BUF = 1 << 20;
    char *buf = malloc(BUF);
    int rc = buf ? 0 : -1;

    while (rc == 0)
    {
        ssize_t r = read(in, buf, BUF);
        if (r == 0)
            break;
        if (r < 0)
        {
            rc = -1;
            break;
        }
        for (ssize_t off = 0; off < r;)
        {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w <= 0)
            {
                rc = -1;
                break;
            }
            off += w;
        }
    }

    free(buf);
    close(in);
    if (close(out) != 0)
        rc = -1;
    if (rc != 0)
        unlink(dst);
    return rc;
}

int media_import(const char *src, char *dest, size_t n)
{
    struct stat ss;
    if (stat(src, &ss) != 0 || !S_ISREG(ss.st_mode))
    {
        log_info(6, NULL, src);
        return -1;
    }

    char dir[MW_PATH_MAX];
    mw_path(dir, sizeof dir, "wallpapers", NULL);

    const char *name = base_name(src);
    const char *dot = strrchr(name, '.');
    int has_ext = dot && dot != name;

    char stem[200], ext[48] = "";
    size_t sl = has_ext ? (size_t)(dot - name) : strlen(name);
    if (sl >= sizeof stem) sl = sizeof stem - 1;
    memcpy(stem, name, sl);
    stem[sl] = 0;
    if (has_ext)
        snprintf(ext, sizeof ext, "%s", dot);

    char cand[MW_PATH_MAX];
    int found_free = 0;

    for (int i = 0; i < 1000; i++)
    {
        if (i == 0)
            snprintf(cand, sizeof cand, "%s/%s%s", dir, stem, ext);
        else
            snprintf(cand, sizeof cand, "%s/%s-%d%s", dir, stem, i, ext);

        struct stat ds;
        if (stat(cand, &ds) != 0)
        {
            found_free = 1;
            break;
        }
        if (ds.st_size == ss.st_size)      /* already imported */
        {
            snprintf(dest, n, "%s", cand);
            return 0;
        }
    }
    if (!found_free)
    {
        log_info(6, NULL, src);
        return -1;
    }

    char part[MW_PATH_MAX + 8];
    snprintf(part, sizeof part, "%s.part", cand);

    if (copy_file(src, part) != 0 || rename(part, cand) != 0)
    {
        unlink(part);
        log_info(6, NULL, src);
        return -1;
    }

    snprintf(dest, n, "%s", cand);

    char msg[MW_PATH_MAX + 32];
    snprintf(msg, sizeof msg, "imported %s", cand);
    log_info(0, "import", msg);
    return 0;
}

static int run_cmd(char *const argv[])
{
    pid_t p = fork();
    if (p < 0)
        return -1;
    if (p == 0)
    {
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0)
        {
            dup2(nul, 0);
            dup2(nul, 1);
            dup2(nul, 2);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(p, &st, 0) < 0)
        return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int file_nonempty(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

int media_make_thumb(const char *video_path)
{
    char thumb[MW_PATH_MAX];
    media_thumb_path(video_path, thumb, sizeof thumb);

    if (file_nonempty(thumb))
        return 0;

    /* try 1s in (skips black intros); very short clips fall back to frame 0 */
    const char *seeks[] = { "1", "0" };
    for (int i = 0; i < 2; i++)
    {
        char *argv[] = { "ffmpeg", "-y", "-loglevel", "error",
                         "-ss", (char *)seeks[i], "-i", (char *)video_path,
                         "-frames:v", "1", "-vf", "scale=320:-2", "-q:v", "3",
                         thumb, NULL };
        run_cmd(argv);
        if (file_nonempty(thumb))
            return 0;
        unlink(thumb);
    }

    log_info(5, NULL, video_path);
    return -1;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

char **media_list(int *count)
{
    *count = 0;

    char dir[MW_PATH_MAX];
    mw_path(dir, sizeof dir, "wallpapers", NULL);
    DIR *d = opendir(dir);
    if (!d)
        return NULL;

    int cap = 16;
    char **list = malloc((size_t)cap * sizeof *list);
    if (!list)
    {
        closedir(d);
        return NULL;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.')
            continue;
        if (l > 5 && strcmp(e->d_name + l - 5, ".part") == 0)
            continue;

        if (*count == cap)
        {
            cap *= 2;
            char **n = realloc(list, (size_t)cap * sizeof *list);
            if (!n)
                break;
            list = n;
        }
        char *dup = strdup(e->d_name);
        if (!dup)
            break;
        list[(*count)++] = dup;
    }
    closedir(d);

    if (*count > 1)
        qsort(list, (size_t)*count, sizeof *list, cmp_str);
    return list;
}

void media_list_free(char **list, int count)
{
    for (int i = 0; i < count; i++)
        free(list[i]);
    free(list);
}

int media_remove(const char *video_path)
{
    char thumb[MW_PATH_MAX];
    media_thumb_path(video_path, thumb, sizeof thumb);
    unlink(thumb);
    return unlink(video_path);
}
