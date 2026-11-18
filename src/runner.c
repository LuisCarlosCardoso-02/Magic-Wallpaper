#include "runner.h"
#include "logs.h"
#include "paths.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* 1 = live mpvpaper, 0 = gone/zombie, -1 = PID reused by another program */
static int pid_state(pid_t pid)
{
    char p[64], buf[512];

    snprintf(p, sizeof p, "/proc/%d/comm", (int)pid);
    FILE *f = fopen(p, "r");
    if (!f)
        return 0;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return 0; }
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;

    if (strcmp(buf, "mpvpaper") != 0)
        return -1;

    snprintf(p, sizeof p, "/proc/%d/stat", (int)pid);
    f = fopen(p, "r");
    if (!f)
        return 0;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return 0; }
    fclose(f);

    char *rp = strrchr(buf, ')');
    if (rp && rp[1] == ' ' && rp[2] == 'Z')
        return 0;
    return 1;
}

static int in_path(const char *prog)
{
    const char *path = getenv("PATH");
    if (!path)
        return 0;
    char *copy = strdup(path);
    if (!copy)
        return 0;

    int found = 0;
    for (char *tok = strtok(copy, ":"); tok && !found; tok = strtok(NULL, ":"))
    {
        char full[MW_PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", tok, prog);
        if (access(full, X_OK) == 0)
            found = 1;
    }
    free(copy);
    return found;
}

/* ---------- PID file ---------- */

static void state_path(char *out, size_t n)
{
    mw_path(out, n, "run", "pids.txt");
}

static void state_read(run_state *rs)
{
    rs->count = 0;

    char path[MW_PATH_MAX];
    state_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[160];
    while (fgets(line, sizeof line, f) && rs->count < MW_MAX_RUN)
    {
        int pid, cid;
        char mon[32];
        if (sscanf(line, "%d %d %31s", &pid, &cid, mon) == 3 && pid > 1)
        {
            run_entry *e = &rs->e[rs->count++];
            e->pid = (pid_t)pid;
            e->config_id = cid;
            snprintf(e->monitor, sizeof e->monitor, "%s", mon);
        }
    }
    fclose(f);
}

static int state_write(const run_state *rs)
{
    char path[MW_PATH_MAX], tmp[MW_PATH_MAX + 8];
    state_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f)
    {
        log_info(-6, NULL, tmp);
        return -1;
    }
    for (int i = 0; i < rs->count; i++)
        fprintf(f, "%d %d %s\n", (int)rs->e[i].pid, rs->e[i].config_id, rs->e[i].monitor);

    if (fclose(f) != 0 || rename(tmp, path) != 0)
    {
        unlink(tmp);
        log_info(-6, NULL, path);
        return -1;
    }
    return 0;
}

int runner_load(run_state *rs)
{
    run_state raw;
    state_read(&raw);

    rs->count = 0;
    int dirty = 0;

    for (int i = 0; i < raw.count; i++)
    {
        int st = pid_state(raw.e[i].pid);
        if (st == 1)
            rs->e[rs->count++] = raw.e[i];
        else
        {
            dirty = 1;
            if (st == -1)
                log_info(1, NULL, raw.e[i].monitor);
        }
    }

    if (dirty)
        state_write(rs);
    return rs->count;
}

/* ---------- process control ---------- */

static void kill_pid(pid_t pid)
{
    if (pid_state(pid) != 1)   /* never signal something that isn't our mpvpaper */
        return;

    kill(pid, SIGTERM);
    for (int i = 0; i < 50; i++)
    {
        if (pid_state(pid) != 1)
            return;
        usleep(20000);
    }
    kill(pid, SIGKILL);
}

static void rs_remove(run_state *rs, int idx)
{
    memmove(&rs->e[idx], &rs->e[idx + 1], (size_t)(rs->count - idx - 1) * sizeof(run_entry));
    rs->count--;
}

/* Starts mpvpaper fully detached (double fork + setsid) so it survives this
 * program closing and never becomes a zombie. Returns its PID or -1. */
static pid_t spawn_mpvpaper(const char *opts, const char *monitor, const char *video)
{
    /* everything that allocates/formats is prepared BEFORE forking */
    char safe[32], lname[64], lpath[MW_PATH_MAX];
    snprintf(safe, sizeof safe, "%s", monitor);
    for (char *p = safe; *p; p++)
        if (*p == '/') *p = '_';
    snprintf(lname, sizeof lname, "mpvpaper_%s.log", safe);
    mw_path(lpath, sizeof lpath, "run", lname);

    char *argv[8];
    int n = 0;
    argv[n++] = "mpvpaper";
    if (opts && *opts)
    {
        argv[n++] = "-o";
        argv[n++] = (char *)opts;
    }
    argv[n++] = (char *)monitor;
    argv[n++] = (char *)video;
    argv[n] = NULL;

    int pfd[2];
    if (pipe(pfd) != 0)
        return -1;

    pid_t c1 = fork();
    if (c1 < 0)
    {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }

    if (c1 == 0)
    {
        close(pfd[0]);
        setsid();

        pid_t c2 = fork();
        if (c2 == 0)
        {
            close(pfd[1]);
            int nul = open("/dev/null", O_RDONLY);
            if (nul >= 0) dup2(nul, 0);
            int lg = open(lpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (lg >= 0) { dup2(lg, 1); dup2(lg, 2); }
            execvp("mpvpaper", argv);
            _exit(127);
        }
        ssize_t w = write(pfd[1], &c2, sizeof c2);
        (void)w;
        _exit(0);
    }

    close(pfd[1]);
    pid_t pid = -1;
    if (read(pfd[0], &pid, sizeof pid) != (ssize_t)sizeof pid)
        pid = -1;
    close(pfd[0]);
    waitpid(c1, NULL, 0);
    return pid;
}

int runner_start(const config *cfg)
{
    if (access(cfg->wallpaper_path, R_OK) != 0)
    {
        log_info(2, NULL, cfg->wallpaper_path);
        return -1;
    }
    if (!in_path("mpvpaper"))
    {
        log_info(-2, NULL, "mpvpaper not found in PATH");
        return -2;
    }

    run_state rs;
    runner_load(&rs);

    int started = 0;
    for (int i = 0; i < cfg->monitor_count; i++)
    {
        const monitor *m = &cfg->monitors[i];
        if (!m->enabled || !m->id[0])
            continue;

        /* replace whatever is already running on this monitor */
        for (int j = rs.count - 1; j >= 0; j--)
        {
            if (strcmp(rs.e[j].monitor, m->id) == 0)
            {
                kill_pid(rs.e[j].pid);
                rs_remove(&rs, j);
            }
        }

        if (rs.count >= MW_MAX_RUN)
            break;

        pid_t pid = spawn_mpvpaper(cfg->mpv_args, m->id, cfg->wallpaper_path);
        if (pid <= 0)
        {
            log_info(-5, NULL, m->id);
            continue;
        }

        run_entry *e = &rs.e[rs.count++];
        e->pid = pid;
        e->config_id = cfg->ID;
        snprintf(e->monitor, sizeof e->monitor, "%s", m->id);
        started++;
    }

    state_write(&rs);

    char msg[MW_PATH_MAX + 128];
    snprintf(msg, sizeof msg, "config%d: started on %d monitor(s) with %s",
             cfg->ID, started, cfg->wallpaper_path);
    log_info(0, "start", msg);
    return started;
}

int runner_stop_monitor(const char *monitor)
{
    run_state rs;
    runner_load(&rs);

    int stopped = 0;
    for (int j = rs.count - 1; j >= 0; j--)
    {
        if (strcmp(rs.e[j].monitor, monitor) == 0)
        {
            kill_pid(rs.e[j].pid);
            rs_remove(&rs, j);
            stopped++;
        }
    }
    state_write(&rs);
    return stopped;
}

int runner_stop_all(void)
{
    run_state rs;
    runner_load(&rs);

    int stopped = rs.count;
    for (int i = 0; i < rs.count; i++)
        kill_pid(rs.e[i].pid);

    rs.count = 0;
    state_write(&rs);

    char msg[64];
    snprintf(msg, sizeof msg, "stopped %d wallpaper process(es)", stopped);
    log_info(0, "stop", msg);
    return stopped;
}
