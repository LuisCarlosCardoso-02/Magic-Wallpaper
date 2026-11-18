/* GTK4 front-end (needs GTK >= 4.10 for GtkFileDialog / GtkAlertDialog). */
#include <gtk/gtk.h>

#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "logs.h"
#include "media.h"
#include "monitors.h"
#include "paths.h"
#include "runner.h"

typedef struct {
    GtkApplication *gapp;
    GtkWidget *win;

    GtkWidget *flow;                 /* wallpaper library */
    GtkWidget *drop;                 /* config selector */
    GtkWidget *name_entry, *mpv_entry, *auto_check;
    GtkWidget *monitor_box;
    GtkWidget *sel_label, *msg_label, *running_label;

    GtkWidget *mon_checks[MW_MAX_MONITORS];
    int mon_count;

    GtkStringList *cfg_names;        /* owned by the dropdown */
    all_configs configs;
    int cur;                         /* index of the config being edited */
    monitor_list mons;
    gboolean updating;               /* true while we change widgets ourselves */
} App;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static config *cur_cfg(App *a)
{
    if (a->cur >= 0 && a->cur < a->configs.num)
        return &a->configs.configs[a->cur];
    return NULL;
}

static void set_msg(App *a, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void set_msg(App *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(a->msg_label), s);
    g_free(s);
}

static void set_margins(GtkWidget *w, int m)
{
    gtk_widget_set_margin_top(w, m);
    gtk_widget_set_margin_bottom(w, m);
    gtk_widget_set_margin_start(w, m);
    gtk_widget_set_margin_end(w, m);
}

static GtkWidget *heading(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_add_css_class(l, "heading");
    return l;
}

static GtkWidget *make_button(const char *label, GCallback cb, App *a)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", cb, a);
    return b;
}

static gboolean refresh_running(gpointer data)
{
    App *a = data;
    run_state rs;
    runner_load(&rs);

    GString *s = g_string_new(NULL);
    if (rs.count == 0)
        g_string_append(s, "No wallpapers running");
    else
    {
        g_string_append(s, "Running:");
        for (int i = 0; i < rs.count; i++)
            g_string_append_printf(s, "\n  %s  (PID %d)", rs.e[i].monitor, (int)rs.e[i].pid);
    }
    gtk_label_set_text(GTK_LABEL(a->running_label), s->str);
    g_string_free(s, TRUE);
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* wallpaper library (thumbnails)                                      */
/* ------------------------------------------------------------------ */

static void update_sel_label(App *a)
{
    config *c = cur_cfg(a);
    if (c && c->wallpaper_path[0])
    {
        char *base = g_path_get_basename(c->wallpaper_path);
        char *txt = g_strdup_printf("Selected: %s", base);
        gtk_label_set_text(GTK_LABEL(a->sel_label), txt);
        g_free(txt);
        g_free(base);
    }
    else
        gtk_label_set_text(GTK_LABEL(a->sel_label), "No wallpaper selected");
}

/* highlight the thumbnail that matches the current config */
static void sync_selection(App *a)
{
    config *c = cur_cfg(a);

    a->updating = TRUE;
    gtk_flow_box_unselect_all(GTK_FLOW_BOX(a->flow));

    if (c && c->wallpaper_path[0])
    {
        for (GtkWidget *ch = gtk_widget_get_first_child(a->flow); ch;
             ch = gtk_widget_get_next_sibling(ch))
        {
            GtkWidget *box = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(ch));
            const char *p = g_object_get_data(G_OBJECT(box), "wp-path");
            if (p && strcmp(p, c->wallpaper_path) == 0)
            {
                gtk_flow_box_select_child(GTK_FLOW_BOX(a->flow), GTK_FLOW_BOX_CHILD(ch));
                break;
            }
        }
    }
    a->updating = FALSE;
    update_sel_label(a);
}

static void on_selection_changed(GtkFlowBox *fb, gpointer data)
{
    App *a = data;
    if (a->updating)
        return;

    config *c = cur_cfg(a);
    if (!c)
        return;

    GList *sel = gtk_flow_box_get_selected_children(fb);
    if (sel)
    {
        GtkWidget *box = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(sel->data));
        const char *p = g_object_get_data(G_OBJECT(box), "wp-path");
        if (p)
            g_strlcpy(c->wallpaper_path, p, sizeof c->wallpaper_path);
    }
    g_list_free(sel);
    update_sel_label(a);
}

/* ---- thumbnail generation in a worker thread ---- */

typedef struct {
    char video[MW_PATH_MAX];
    GtkPicture *pic;
} ThumbJob;

static void thumb_job_free(gpointer p)
{
    ThumbJob *j = p;
    g_object_unref(j->pic);
    g_free(j);
}

static void thumb_thread(GTask *task, gpointer src, gpointer task_data, GCancellable *cancel)
{
    (void)src;
    (void)cancel;
    ThumbJob *j = task_data;
    g_task_return_boolean(task, media_make_thumb(j->video) == 0);
}

static void thumb_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    (void)src;
    (void)user_data;
    GTask *t = G_TASK(res);
    ThumbJob *j = g_task_get_task_data(t);

    if (g_task_propagate_boolean(t, NULL))
    {
        char tp[MW_PATH_MAX];
        media_thumb_path(j->video, tp, sizeof tp);
        gtk_picture_set_filename(j->pic, tp);
    }
}

static void queue_thumb(GtkPicture *pic, const char *video)
{
    ThumbJob *j = g_new0(ThumbJob, 1);
    g_strlcpy(j->video, video, sizeof j->video);
    j->pic = g_object_ref(pic);

    GTask *t = g_task_new(NULL, NULL, thumb_done, NULL);
    g_task_set_task_data(t, j, thumb_job_free);
    g_task_run_in_thread(t, thumb_thread);
    g_object_unref(t);
}

static GtkWidget *make_item(const char *name)
{
    char video[MW_PATH_MAX], thumb[MW_PATH_MAX];
    mw_path(video, sizeof video, "wallpapers", name);
    media_thumb_path(video, thumb, sizeof thumb);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    set_margins(box, 6);

    GtkWidget *pic = gtk_picture_new();
    gtk_widget_set_size_request(pic, 200, 112);
    gtk_widget_set_halign(pic, GTK_ALIGN_CENTER);
    gtk_widget_set_overflow(pic, GTK_OVERFLOW_HIDDEN);
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_COVER);

    if (access(thumb, R_OK) == 0)
        gtk_picture_set_filename(GTK_PICTURE(pic), thumb);
    else
        queue_thumb(GTK_PICTURE(pic), video);

    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 24);

    gtk_box_append(GTK_BOX(box), pic);
    gtk_box_append(GTK_BOX(box), label);
    g_object_set_data_full(G_OBJECT(box), "wp-path", g_strdup(video), g_free);
    return box;
}

static void rebuild_library(App *a)
{
    a->updating = TRUE;

    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(a->flow)) != NULL)
        gtk_flow_box_remove(GTK_FLOW_BOX(a->flow), c);

    int n = 0;
    char **names = media_list(&n);
    for (int i = 0; i < n; i++)
        gtk_flow_box_append(GTK_FLOW_BOX(a->flow), make_item(names[i]));
    media_list_free(names, n);

    a->updating = FALSE;
    sync_selection(a);
}

/* ------------------------------------------------------------------ */
/* config <-> widgets                                                  */
/* ------------------------------------------------------------------ */

static void load_cfg_into_ui(App *a)
{
    config *c = cur_cfg(a);
    if (!c)
        return;

    a->updating = TRUE;

    monitors_detect(&a->mons);
    config_sync_monitors(c, &a->mons);

    gtk_editable_set_text(GTK_EDITABLE(a->name_entry), c->name);
    gtk_editable_set_text(GTK_EDITABLE(a->mpv_entry), c->mpv_args);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(a->auto_check), c->auto_start != 0);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(a->monitor_box)) != NULL)
        gtk_box_remove(GTK_BOX(a->monitor_box), child);

    a->mon_count = 0;
    for (int i = 0; i < c->monitor_count; i++)
    {
        GtkWidget *chk = gtk_check_button_new_with_label(c->monitors[i].id);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(chk), c->monitors[i].enabled != 0);
        gtk_box_append(GTK_BOX(a->monitor_box), chk);
        a->mon_checks[i] = chk;
        a->mon_count++;
    }
    if (c->monitor_count == 0)
        gtk_box_append(GTK_BOX(a->monitor_box), gtk_label_new("No monitors detected"));

    a->updating = FALSE;
    sync_selection(a);
}

static void read_ui_into_cfg(App *a)
{
    config *c = cur_cfg(a);
    if (!c)
        return;

    const char *name = gtk_editable_get_text(GTK_EDITABLE(a->name_entry));
    if (name[0])
        g_strlcpy(c->name, name, sizeof c->name);
    else
        g_snprintf(c->name, sizeof c->name, "Config %d", c->ID);

    g_strlcpy(c->mpv_args, gtk_editable_get_text(GTK_EDITABLE(a->mpv_entry)), sizeof c->mpv_args);
    c->auto_start = gtk_check_button_get_active(GTK_CHECK_BUTTON(a->auto_check)) ? 1 : 0;

    for (int i = 0; i < a->mon_count && i < c->monitor_count; i++)
        c->monitors[i].enabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(a->mon_checks[i])) ? 1 : 0;
}

static void refresh_config_list(App *a, int select_idx)
{
    a->updating = TRUE;

    guint n = g_list_model_get_n_items(G_LIST_MODEL(a->cfg_names));
    gtk_string_list_splice(a->cfg_names, 0, n, NULL);

    for (int i = 0; i < a->configs.num; i++)
    {
        const config *c = &a->configs.configs[i];
        char *label = c->name[0] ? g_strdup(c->name) : g_strdup_printf("Config %d", c->ID);
        gtk_string_list_append(a->cfg_names, label);
        g_free(label);
    }

    a->cur = select_idx;
    if (a->configs.num > 0)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(a->drop), (guint)select_idx);

    a->updating = FALSE;
}

static void reload_configs(App *a, int select_id)
{
    config_free_all(&a->configs);

    if (config_load_all(&a->configs) <= 0)
    {
        config_free_all(&a->configs);
        config def;
        config_set_defaults(&def, 1);
        config_save(&def);
        config_load_all(&a->configs);
    }

    int idx = 0;
    for (int i = 0; i < a->configs.num; i++)
        if (a->configs.configs[i].ID == select_id)
        {
            idx = i;
            break;
        }

    refresh_config_list(a, idx);
    load_cfg_into_ui(a);
}

static void on_drop_changed(GObject *obj, GParamSpec *ps, gpointer data)
{
    (void)obj;
    (void)ps;
    App *a = data;
    if (a->updating)
        return;

    /* switching configs auto-saves the one we are leaving */
    config *old = cur_cfg(a);
    if (old)
    {
        read_ui_into_cfg(a);
        config_save(old);
    }

    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->drop));
    if (sel == GTK_INVALID_LIST_POSITION || (int)sel >= a->configs.num)
        return;

    a->cur = (int)sel;
    load_cfg_into_ui(a);
}

/* ------------------------------------------------------------------ */
/* buttons                                                             */
/* ------------------------------------------------------------------ */

static void on_save(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;
    config *c = cur_cfg(a);
    if (!c)
        return;

    read_ui_into_cfg(a);
    if (config_save(c) == 0)
    {
        refresh_config_list(a, a->cur);
        set_msg(a, "Saved \"%s\"", c->name);
    }
    else
        set_msg(a, "Couldn't save the config (see log.txt)");
}

static void on_apply(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;
    config *c = cur_cfg(a);
    if (!c)
        return;

    read_ui_into_cfg(a);
    if (!c->wallpaper_path[0])
    {
        set_msg(a, "Pick a wallpaper from the library first");
        return;
    }
    if (config_save(c) != 0)
    {
        set_msg(a, "Couldn't save the config (see log.txt)");
        return;
    }
    refresh_config_list(a, a->cur);

    int n = runner_start(c);
    if (n == -1)
        set_msg(a, "The wallpaper file is missing");
    else if (n == -2)
        set_msg(a, "mpvpaper was not found in your PATH");
    else if (n == 0)
        set_msg(a, "No monitor is enabled");
    else
        set_msg(a, "Started on %d monitor(s)", n);

    refresh_running(a);
}

static void on_stop(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;
    int n = runner_stop_all();
    set_msg(a, "Stopped %d wallpaper process(es)", n);
    refresh_running(a);
}

static void on_new(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;

    config *c = cur_cfg(a);
    if (c)
    {
        read_ui_into_cfg(a);
        config_save(c);
    }

    config n;
    config_set_defaults(&n, config_next_id(&a->configs));
    if (config_save(&n) != 0)
    {
        set_msg(a, "Couldn't create the config (see log.txt)");
        return;
    }
    reload_configs(a, n.ID);
    set_msg(a, "Created \"%s\"", n.name);
}

static void on_delete(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;
    config *c = cur_cfg(a);
    if (!c)
        return;

    if (a->configs.num <= 1)
    {
        set_msg(a, "You need at least one config");
        return;
    }

    int neighbour = a->configs.configs[a->cur == 0 ? 1 : 0].ID;
    config_delete(c->ID);
    reload_configs(a, neighbour);
    set_msg(a, "Config deleted");
}

/* ---- add videos (copy into the library) ---- */

typedef struct {
    App *a;
    char src[MW_PATH_MAX];
    char dest[MW_PATH_MAX];
    int rc;
} ImportJob;

static void import_thread(GTask *task, gpointer src, gpointer task_data, GCancellable *cancel)
{
    (void)src;
    (void)cancel;
    ImportJob *j = task_data;
    j->rc = media_import(j->src, j->dest, sizeof j->dest);
    if (j->rc == 0)
        media_make_thumb(j->dest);
    g_task_return_boolean(task, j->rc == 0);
}

static void import_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    (void)src;
    (void)user_data;
    GTask *t = G_TASK(res);
    ImportJob *j = g_task_get_task_data(t);
    App *a = j->a;

    if (g_task_propagate_boolean(t, NULL))
    {
        config *c = cur_cfg(a);
        if (c)
            g_strlcpy(c->wallpaper_path, j->dest, sizeof c->wallpaper_path);
        rebuild_library(a);
        set_msg(a, "Added to the library");
    }
    else
        set_msg(a, "Couldn't copy that video (see log.txt)");
}

static void start_import(App *a, const char *path)
{
    ImportJob *j = g_new0(ImportJob, 1);
    j->a = a;
    g_strlcpy(j->src, path, sizeof j->src);

    GTask *t = g_task_new(NULL, NULL, import_done, NULL);
    g_task_set_task_data(t, j, g_free);
    g_task_run_in_thread(t, import_thread);
    g_object_unref(t);
    set_msg(a, "Copying video…");
}

static void on_files_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    App *a = data;
    GError *err = NULL;
    GListModel *files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src), res, &err);
    if (!files)
    {
        g_clear_error(&err);   /* dialog dismissed */
        return;
    }

    guint n = g_list_model_get_n_items(files);
    for (guint i = 0; i < n; i++)
    {
        GFile *f = g_list_model_get_item(files, i);
        char *path = g_file_get_path(f);
        if (path)
            start_import(a, path);
        g_free(path);
        g_object_unref(f);
    }
    g_object_unref(files);
}

static void on_add_video(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;

    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Choose video(s) for your wallpaper library");

    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "Videos");
    gtk_file_filter_add_mime_type(f, "video/*");
    gtk_file_filter_add_mime_type(f, "image/gif");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, f);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(filters));
    g_object_unref(f);
    g_object_unref(filters);

    gtk_file_dialog_open_multiple(d, GTK_WINDOW(a->win), NULL, on_files_chosen, a);
    g_object_unref(d);
}

/* ---- remove a video from the library ---- */

static gboolean config_is_running(int id)
{
    run_state rs;
    runner_load(&rs);
    for (int i = 0; i < rs.count; i++)
        if (rs.e[i].config_id == id)
            return TRUE;
    return FALSE;
}

static void on_remove_confirmed(GObject *src, GAsyncResult *res, gpointer data)
{
    App *a = data;
    GError *err = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &err);
    if (err)
    {
        g_clear_error(&err);
        return;
    }
    if (choice != 1)
        return;

    config *c = cur_cfg(a);
    if (!c || !c->wallpaper_path[0])
        return;

    media_remove(c->wallpaper_path);
    c->wallpaper_path[0] = 0;
    config_save(c);
    rebuild_library(a);
    set_msg(a, "Removed from the library");
}

static void on_remove_video(GtkButton *b, gpointer data)
{
    (void)b;
    App *a = data;
    config *c = cur_cfg(a);
    if (!c || !c->wallpaper_path[0])
    {
        set_msg(a, "Select a wallpaper in the library first");
        return;
    }

    for (int i = 0; i < a->configs.num; i++)
    {
        const config *o = &a->configs.configs[i];
        if (i != a->cur && strcmp(o->wallpaper_path, c->wallpaper_path) == 0)
        {
            set_msg(a, "That video is also used by \"%s\"", o->name);
            return;
        }
    }
    if (config_is_running(c->ID))
    {
        set_msg(a, "That wallpaper is running – stop it first");
        return;
    }

    GtkAlertDialog *d = gtk_alert_dialog_new("Remove this wallpaper from the library?");
    gtk_alert_dialog_set_detail(d, "The copy inside MagicWallpaper is deleted. "
                                   "Your original video file is not touched.");
    const char *buttons[] = { "Cancel", "Remove", NULL };
    gtk_alert_dialog_set_buttons(d, buttons);
    gtk_alert_dialog_set_cancel_button(d, 0);
    gtk_alert_dialog_set_default_button(d, 0);
    gtk_alert_dialog_choose(d, GTK_WINDOW(a->win), NULL, on_remove_confirmed, a);
    g_object_unref(d);
}

/* ------------------------------------------------------------------ */
/* window                                                              */
/* ------------------------------------------------------------------ */

static void activate(GtkApplication *gapp, gpointer data)
{
    App *a = data;
    if (a->win)
    {
        gtk_window_present(GTK_WINDOW(a->win));
        return;
    }

    mw_ensure_dirs();

    a->win = gtk_application_window_new(gapp);
    gtk_window_set_title(GTK_WINDOW(a->win), "MagicWallpaper");
    gtk_window_set_default_size(GTK_WINDOW(a->win), 1020, 640);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(a->win), root);

    /* ---------- left: library ---------- */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(left, TRUE);
    set_margins(left, 12);

    GtkWidget *lhead = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title = heading("Wallpaper library");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(lhead), title);
    gtk_box_append(GTK_BOX(lhead), make_button("Add video…", G_CALLBACK(on_add_video), a));
    gtk_box_append(GTK_BOX(lhead), make_button("Remove", G_CALLBACK(on_remove_video), a));
    gtk_box_append(GTK_BOX(left), lhead);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    a->flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(a->flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(a->flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(a->flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(a->flow), 6);
    gtk_widget_set_valign(a->flow, GTK_ALIGN_START);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), a->flow);
    g_signal_connect(a->flow, "selected-children-changed", G_CALLBACK(on_selection_changed), a);
    gtk_box_append(GTK_BOX(left), scroll);

    gtk_box_append(GTK_BOX(root), left);
    gtk_box_append(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    /* ---------- right: config ---------- */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(right, 320, -1);
    set_margins(right, 12);

    gtk_box_append(GTK_BOX(right), heading("Config"));
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    a->cfg_names = gtk_string_list_new(NULL);
    a->drop = gtk_drop_down_new(G_LIST_MODEL(a->cfg_names), NULL);
    gtk_widget_set_hexpand(a->drop, TRUE);
    g_signal_connect(a->drop, "notify::selected", G_CALLBACK(on_drop_changed), a);
    gtk_box_append(GTK_BOX(row), a->drop);
    gtk_box_append(GTK_BOX(row), make_button("New", G_CALLBACK(on_new), a));
    gtk_box_append(GTK_BOX(row), make_button("Delete", G_CALLBACK(on_delete), a));
    gtk_box_append(GTK_BOX(right), row);

    gtk_box_append(GTK_BOX(right), heading("Name"));
    a->name_entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(right), a->name_entry);

    gtk_box_append(GTK_BOX(right), heading("mpvpaper options (-o)"));
    a->mpv_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->mpv_entry), "e.g. no-audio loop");
    gtk_box_append(GTK_BOX(right), a->mpv_entry);

    a->sel_label = gtk_label_new("No wallpaper selected");
    gtk_widget_set_halign(a->sel_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(a->sel_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(right), a->sel_label);

    a->auto_check = gtk_check_button_new_with_label("Start automatically (-auto)");
    gtk_box_append(GTK_BOX(right), a->auto_check);

    gtk_box_append(GTK_BOX(right), heading("Monitors"));
    a->monitor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(right), a->monitor_box);

    GtkWidget *brow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(brow), TRUE);
    GtkWidget *save = make_button("Save", G_CALLBACK(on_save), a);
    GtkWidget *apply = make_button("Apply", G_CALLBACK(on_apply), a);
    gtk_widget_add_css_class(apply, "suggested-action");
    gtk_box_append(GTK_BOX(brow), save);
    gtk_box_append(GTK_BOX(brow), apply);
    gtk_box_append(GTK_BOX(right), brow);

    GtkWidget *stop = make_button("Stop all wallpapers", G_CALLBACK(on_stop), a);
    gtk_widget_add_css_class(stop, "destructive-action");
    gtk_box_append(GTK_BOX(right), stop);

    a->running_label = gtk_label_new("");
    gtk_widget_set_halign(a->running_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(right), a->running_label);

    a->msg_label = gtk_label_new("");
    gtk_widget_set_halign(a->msg_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(a->msg_label), TRUE);
    gtk_box_append(GTK_BOX(right), a->msg_label);

    gtk_box_append(GTK_BOX(root), right);

    /* ---------- initial state ---------- */
    rebuild_library(a);
    reload_configs(a, 0);
    refresh_running(a);
    g_timeout_add_seconds(2, refresh_running, a);

    gtk_window_present(GTK_WINDOW(a->win));
}

int gui_run(int argc, char **argv)
{
    static App app;

    app.gapp = gtk_application_new("dev.magicwallpaper.App", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app.gapp, "activate", G_CALLBACK(activate), &app);

    int status = g_application_run(G_APPLICATION(app.gapp), argc, argv);
    g_object_unref(app.gapp);
    return status;
}
