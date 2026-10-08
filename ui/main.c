/* Wisp — lightweight unofficial SoundCloud client. GTK4 + GStreamer front-end;
 * the Go "wisp-core" sidecar does all the scraping and serves JSON/audio on localhost. */
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "config.h"

typedef struct { char *kind, *id, *title, *sub, *stream, *path; int dur; } Item;
typedef struct { char *path, *title; } View;

static struct {
    GtkWidget *win, *entry, *list, *title, *np, *seek, *vol, *cur, *dur, *pp, *msg, *lk, *genres, *types, *pop, *menu_box, *help;
    GtkCssProvider *css;
    SoupSession *soup;
    char *base;
    GSubprocess *core;
    GPtrArray *items, *queue, *hist;
    View view;
    int qi, sel;
    char *nowid, *type, *genre, *mode, *instance, *theme;
    guint gen, msg_src;
    double dur_s;
    GstElement *play;
} A;

/* ───────────── small helpers ───────────── */
static void item_free(gpointer p) { Item *i = p; g_free(i->kind); g_free(i->id); g_free(i->title); g_free(i->sub); g_free(i->stream); g_free(i->path); g_free(i); }
static Item *item_dup(const Item *s) {
    Item *i = g_new0(Item, 1);
    i->kind = g_strdup(s->kind); i->id = g_strdup(s->id); i->title = g_strdup(s->title); i->sub = g_strdup(s->sub);
    i->stream = g_strdup(s->stream); i->path = g_strdup(s->path); i->dur = s->dur;
    return i;
}
static void view_free(gpointer p) { View *v = p; g_free(v->path); g_free(v->title); g_free(v); }
static const char *key_of(const char *action) { for (const WispKey *k = wisp_keys; k->action; k++) if (!strcmp(k->action, action)) return k->key; return "?"; }
static char *fmt_t(double s) { int t = s > 0 ? (int)s : 0; return g_strdup_printf("%02d:%02d", t / 60, t % 60); }
static char *mkbar(double v, int w, const char *pre) {
    GString *g = g_string_new(pre);
    int f = (int)(CLAMP(v, 0, 1) * w + .5);
    g_string_append_c(g, '[');
    for (int i = 0; i < w; i++) g_string_append(g, i < f ? wisp_ui.bar_full : wisp_ui.bar_empty);
    g_string_append_c(g, ']');
    return g_string_free(g, FALSE);
}
static GtkWidget *lbl(const char *t, const char *cls) {
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}
static GtkWidget *btn(const char *t, const char *cls, GCallback cb, gpointer d) {
    GtkWidget *b = gtk_button_new_with_label(t);
    gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
    gtk_widget_set_focusable(b, FALSE);
    if (cls) gtk_widget_add_css_class(b, cls);
    if (cb) g_signal_connect(b, "clicked", cb, d);
    return b;
}
static GtkWidget *box(GtkOrientation o, int sp) { return gtk_box_new(o, sp); }
static GtkWidget *titled(const char *title, GtkWidget **inner_out) {
    GtkWidget *b = box(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(b, "box");
    char *t = g_strdup_printf("┤ %s ├", title);
    GtkWidget *l = lbl(t, "ac"); g_free(t);
    gtk_box_append(GTK_BOX(b), l);
    if (inner_out) *inner_out = l;
    return b;
}
static gboolean clr_msg(gpointer d) { gtk_label_set_text(GTK_LABEL(A.msg), ""); A.msg_src = 0; return G_SOURCE_REMOVE; }
static void say(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); char *s = g_strdup_vprintf(fmt, ap); va_end(ap);
    gtk_label_set_text(GTK_LABEL(A.msg), s); g_free(s);
    if (A.msg_src) g_source_remove(A.msg_src);
    A.msg_src = g_timeout_add(4000, clr_msg, NULL);
}

/* ───────────── theme → CSS ───────────── */
static void apply_theme(const char *name) {
    const WispTheme *t = wisp_themes;
    for (const WispTheme *x = wisp_themes; x->name; x++) if (name && !strcmp(x->name, name)) { t = x; break; }
    g_free(A.theme); A.theme = g_strdup(t->name);
    GString *g = g_string_new(NULL);
    g_string_append_printf(g, "* { font-family: %s; font-size: %dpx; border-radius: 0; box-shadow: none; text-shadow: none; outline: none; }\n", wisp_ui.font, wisp_ui.font_size);
    g_string_append_printf(g, "window, dialog, .background { background: %s; color: %s; }\n", t->bg, t->fg);
    g_string_append_printf(g, "label { color: %s; } label.dim { color: %s; } label.ac { color: %s; } label.err { color: %s; }\n", t->fg, t->dim, t->accent, t->error);
    g_string_append_printf(g, ".box { border: 1px solid %s; padding: 8px 12px; background: %s; }\n", t->border, t->bg);
    g_string_append_printf(g, "button { background: none; border: none; color: %s; padding: 0 5px; min-height: 0; min-width: 0; } button label { color: %s; }\n", t->fg, t->fg);
    g_string_append_printf(g, "button.nav label { color: %s; } button:hover, button.on { background: %s; } button:hover label, button.on label, button.nav.on label { color: %s; }\n", t->dim, t->accent, t->bg);
    g_string_append_printf(g, "entry { background: none; border: none; padding: 0 4px; min-height: 0; color: %s; caret-color: %s; }\n", t->fg, t->accent);
    g_string_append_printf(g, "list, scrolledwindow, viewport { background: %s; } row { background: none; padding: 1px 6px; min-height: 0; }\n", t->bg);
    g_string_append_printf(g, "row:selected { background: %s; } row:selected label { color: %s; } row.now .ttl { color: %s; } row.now:selected .ttl { color: %s; }\n", t->sel_bg, t->sel_fg, t->accent, t->sel_fg);
    g_string_append_printf(g, "scrollbar { background: none; } scrollbar slider { background: %s; min-width: 4px; min-height: 24px; }\n", t->accent);
    g_string_append_printf(g, "popover contents { background: %s; border: 1px solid %s; padding: 6px; } popover listview, popover list { background: %s; }\n", t->bg, t->accent, t->bg);
    g_string_append_printf(g, "popover row:selected, popover listview row:selected { background: %s; } popover row:selected label { color: %s; }\n", t->accent, t->bg);
    g_string_append_printf(g, "dropdown > button { border: 1px solid %s; padding: 2px 6px; } check, radio { background: none; border: 1px solid %s; min-width: 14px; min-height: 14px; color: %s; } check:checked, radio:checked { background: %s; }\n", t->border, t->accent, t->bg, t->accent);
    g_string_append(g, wisp_ui.custom_css);
    gtk_css_provider_load_from_data(A.css, g->str, -1);
    g_string_free(g, TRUE);
}

/* ───────────── core sidecar + HTTP ───────────── */
static gboolean start_core(void) {
    char *exe = g_strdup(g_getenv("WISP_CORE"));
    if (!exe) exe = g_find_program_in_path("wisp-core");
    if (!exe) {
        char *self = g_file_read_link("/proc/self/exe", NULL);
        if (self) {
            char *d = g_path_get_dirname(self);
            exe = g_build_filename(d, "wisp-core", NULL);
            if (!g_file_test(exe, G_FILE_TEST_IS_EXECUTABLE)) { g_free(exe); exe = NULL; }
            g_free(d); g_free(self);
        }
    }
    if (!exe) return FALSE;
    GError *e = NULL;
    A.core = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE, &e, exe, "-port", "0", "-watch-stdin", NULL);
    g_free(exe);
    if (!A.core) { g_clear_error(&e); return FALSE; }
    GDataInputStream *in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(A.core));
    g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(in), FALSE);
    char *line = g_data_input_stream_read_line(in, NULL, NULL, NULL);
    gboolean ok = line && g_str_has_prefix(line, "LISTEN ");
    if (ok) A.base = g_strdup_printf("http://%s", line + 7);
    g_free(line); g_object_unref(in);
    return ok;
}

typedef void (*Done)(JsonNode *root, const char *err, gpointer ud);
typedef struct { Done cb; gpointer ud; } Req;

static void on_http(GObject *src, GAsyncResult *res, gpointer p) {
    Req *r = p; GError *e = NULL;
    GBytes *b = soup_session_send_and_read_finish(SOUP_SESSION(src), res, &e);
    if (!b) { r->cb(NULL, e ? e->message : "request failed", r->ud); g_clear_error(&e); }
    else {
        SoupMessage *m = soup_session_get_async_result_message(SOUP_SESSION(src), res);
        gsize n; const char *d = g_bytes_get_data(b, &n);
        if (m && soup_message_get_status(m) >= 400) { char *t = g_strndup(d, n); r->cb(NULL, t, r->ud); g_free(t); }
        else {
            JsonParser *jp = json_parser_new();
            if (json_parser_load_from_data(jp, d ? d : "", n, &e)) r->cb(json_parser_get_root(jp), NULL, r->ud);
            else { r->cb(NULL, "bad response from core", r->ud); g_clear_error(&e); }
            g_object_unref(jp);
        }
        g_bytes_unref(b);
    }
    g_free(r);
}
static void http(const char *method, const char *path, const char *body, Done cb, gpointer ud) {
    char *u = g_strconcat(A.base, path, NULL);
    SoupMessage *m = soup_message_new(method, u); g_free(u);
    if (body) { GBytes *b = g_bytes_new(body, strlen(body)); soup_message_set_request_body_from_bytes(m, "application/json", b); g_bytes_unref(b); }
    Req *r = g_new(Req, 1); r->cb = cb; r->ud = ud;
    soup_session_send_and_read_async(A.soup, m, G_PRIORITY_DEFAULT, NULL, on_http, r);
    g_object_unref(m);
}
static void noop(JsonNode *r, const char *e, gpointer u) {}
static void post_cfg(void) {
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "mode"); json_builder_add_string_value(b, A.mode);
    json_builder_set_member_name(b, "instance"); json_builder_add_string_value(b, A.instance ? A.instance : "");
    json_builder_set_member_name(b, "theme"); json_builder_add_string_value(b, A.theme);
    json_builder_end_object(b);
    JsonGenerator *g = json_generator_new();
    JsonNode *root = json_builder_get_root(b);
    json_generator_set_root(g, root);
    char *s = json_generator_to_data(g, NULL);
    http("POST", "/api/settings", s, noop, NULL);
    g_free(s); json_node_unref(root); g_object_unref(g); g_object_unref(b);
}

/* ───────────── list view ───────────── */
static void refresh_markers(void) {
    for (guint i = 0; i < A.items->len; i++) {
        GtkListBoxRow *r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(A.list), i);
        if (!r) continue;
        GtkWidget *first = gtk_widget_get_first_child(gtk_list_box_row_get_child(r));
        if (first && GTK_IS_LABEL(first)) gtk_label_set_text(GTK_LABEL(first), (int)i == A.sel ? wisp_ui.prompt : "");
    }
}
static void select_idx(int i, gboolean focus) {
    if (!A.items->len) return;
    A.sel = CLAMP(i, 0, (int)A.items->len - 1);
    GtkListBoxRow *r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(A.list), A.sel);
    if (!r) return;
    gtk_list_box_select_row(GTK_LIST_BOX(A.list), r);
    if (focus) gtk_widget_grab_focus(GTK_WIDGET(r));
    refresh_markers();
}
static void clear_list(void) { GtkWidget *c; while ((c = gtk_widget_get_first_child(A.list))) gtk_list_box_remove(GTK_LIST_BOX(A.list), c); }
static void list_message(const char *text, gboolean err) {
    clear_list();
    gtk_list_box_append(GTK_LIST_BOX(A.list), lbl(text, err ? "err" : "dim"));
    GtkListBoxRow *r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(A.list), 0);
    if (r) { gtk_list_box_row_set_selectable(r, FALSE); gtk_list_box_row_set_activatable(r, FALSE); }
}
static void render_list(void) {
    clear_list();
    if (!A.items->len) { list_message("nothing found", FALSE); return; }
    for (guint i = 0; i < A.items->len; i++) {
        Item *it = A.items->pdata[i];
        gboolean playing = A.nowid && !strcmp(A.nowid, it->id);
        char num[16]; g_snprintf(num, sizeof num, "%02u", i + 1);
        const char *pfx = !strcmp(it->kind, "user") ? "@ " : !strcmp(it->kind, "playlist") ? "≡ " : "";
        char *tt = g_strconcat(pfx, it->title, NULL), *d = it->dur ? fmt_t(it->dur / 1000.0) : g_strdup("");
        GtkWidget *b = box(GTK_ORIENTATION_HORIZONTAL, 14);
        GtkWidget *mk = lbl("", "dim"), *n = lbl(playing ? wisp_ui.now_playing : num, "dim"), *t = lbl(tt, "ttl"), *s = lbl(it->sub, "dim"), *du = lbl(d, "dim");
        gtk_label_set_width_chars(GTK_LABEL(mk), 1); gtk_label_set_width_chars(GTK_LABEL(n), 3);
        gtk_widget_set_hexpand(t, TRUE); gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(t), 1);
        gtk_label_set_width_chars(GTK_LABEL(s), 22); gtk_label_set_max_width_chars(GTK_LABEL(s), 22); gtk_label_set_ellipsize(GTK_LABEL(s), PANGO_ELLIPSIZE_END);
        gtk_label_set_width_chars(GTK_LABEL(du), 6); gtk_label_set_xalign(GTK_LABEL(du), 1);
        gtk_box_append(GTK_BOX(b), mk); gtk_box_append(GTK_BOX(b), n); gtk_box_append(GTK_BOX(b), t); gtk_box_append(GTK_BOX(b), s); gtk_box_append(GTK_BOX(b), du);
        gtk_list_box_append(GTK_LIST_BOX(A.list), b);
        if (playing) gtk_widget_add_css_class(GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(A.list), i)), "now");
        g_free(tt); g_free(d);
    }
    select_idx(A.sel, FALSE);
}
static const char *js(JsonObject *o, const char *k) { return json_object_get_string_member_with_default(o, k, ""); }
static void on_items(JsonNode *root, const char *err, gpointer ud) {
    if (GPOINTER_TO_UINT(ud) != A.gen) return;
    g_ptr_array_set_size(A.items, 0); A.sel = 0;
    if (err) { list_message(err, TRUE); return; }
    if (root && JSON_NODE_HOLDS_ARRAY(root)) {
        JsonArray *arr = json_node_get_array(root);
        for (guint i = 0; i < json_array_get_length(arr); i++) {
            JsonObject *o = json_array_get_object_element(arr, i);
            if (!o) continue;
            Item *it = g_new0(Item, 1);
            it->kind = g_strdup(js(o, "kind")); it->id = g_strdup(js(o, "id")); it->title = g_strdup(js(o, "title")); it->sub = g_strdup(js(o, "sub"));
            it->stream = g_strdup(js(o, "stream")); it->path = g_strdup(js(o, "path"));
            it->dur = (int)json_object_get_int_member_with_default(o, "dur", 0);
            g_ptr_array_add(A.items, it);
        }
    }
    render_list();
}
static void load_view(const char *path, const char *title, gboolean push) {
    View old = A.view;
    A.view.path = g_strdup(path); A.view.title = g_strdup(title);
    if (push && old.path) { View *v = g_new(View, 1); *v = old; g_ptr_array_add(A.hist, v); }
    else { g_free(old.path); g_free(old.title); }
    char *tt = g_strdup_printf("┤ %s ├", title);
    gtk_label_set_text(GTK_LABEL(A.title), tt); g_free(tt);
    g_ptr_array_set_size(A.items, 0);
    list_message("loading…", FALSE);
    http("GET", path, NULL, on_items, GUINT_TO_POINTER(++A.gen));
}
static void go_home(void) {
    g_ptr_array_set_size(A.hist, 0);
    char *p = g_strdup_printf("/api/trending?genre=%s", A.genre);
    load_view(p, "trending", FALSE); g_free(p);
}
static void go_back(void) {
    if (!A.hist->len) return;
    View *v = A.hist->pdata[A.hist->len - 1];
    char *p = g_strdup(v->path), *t = g_strdup(v->title);
    g_ptr_array_remove_index(A.hist, A.hist->len - 1);
    load_view(p, t, FALSE); g_free(p); g_free(t);
}
static void do_search(void) {
    const char *q = gtk_editable_get_text(GTK_EDITABLE(A.entry));
    if (!*q) return;
    char *e = g_uri_escape_string(q, NULL, FALSE);
    char *p = g_strdup_printf("/api/search?type=%s&q=%s", A.type, e), *t = g_strdup_printf("%s · %s", A.type, q);
    load_view(p, t, TRUE);
    g_free(e); g_free(p); g_free(t);
}

/* ───────────── playback (GStreamer playbin) ───────────── */
static void play_idx(int i) {
    if (i < 0 || i >= (int)A.queue->len) return;
    A.qi = i;
    Item *t = A.queue->pdata[i];
    g_free(A.nowid); A.nowid = g_strdup(t->id);
    char *u = g_strconcat(A.base, t->stream, NULL);
    gst_element_set_state(A.play, GST_STATE_NULL);
    g_object_set(A.play, "uri", u, NULL);
    gst_element_set_state(A.play, GST_STATE_PLAYING);
    g_free(u);
    char *np = g_strdup_printf("%s %s — %s", wisp_ui.now_playing, t->title, t->sub);
    gtk_label_set_text(GTK_LABEL(A.np), np); g_free(np);
    render_list();
}
static void toggle_play(void) {
    GstState st; gst_element_get_state(A.play, &st, NULL, 0);
    if (st == GST_STATE_PLAYING) gst_element_set_state(A.play, GST_STATE_PAUSED);
    else if (st == GST_STATE_PAUSED) gst_element_set_state(A.play, GST_STATE_PLAYING);
}
static void seek_rel(double secs) {
    gint64 pos;
    if (gst_element_query_position(A.play, GST_FORMAT_TIME, &pos))
        gst_element_seek_simple(A.play, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, MAX(0, pos + (gint64)(secs * GST_SECOND)));
}
static void add_vol(double d) { double v; g_object_get(A.play, "volume", &v, NULL); g_object_set(A.play, "volume", CLAMP(v + d, 0.0, 1.0), NULL); }
static gboolean on_bus(GstBus *b, GstMessage *m, gpointer d) {
    switch (GST_MESSAGE_TYPE(m)) {
    case GST_MESSAGE_EOS: play_idx(A.qi + 1); break;
    case GST_MESSAGE_ERROR: { GError *e; gst_message_parse_error(m, &e, NULL); say("could not play: %s", e->message); g_error_free(e); gst_element_set_state(A.play, GST_STATE_NULL); break; }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(m) == GST_OBJECT(A.play)) { GstState o, n; gst_message_parse_state_changed(m, &o, &n, NULL); gtk_button_set_label(GTK_BUTTON(A.pp), n == GST_STATE_PLAYING ? "[⏸]" : "[▶]"); }
        break;
    default: break;
    }
    return TRUE;
}
static gboolean tick(gpointer d) {
    gint64 pos = 0, dur = 0; double v = 0;
    gst_element_query_position(A.play, GST_FORMAT_TIME, &pos);
    if (gst_element_query_duration(A.play, GST_FORMAT_TIME, &dur) && dur > 0) A.dur_s = dur / (double)GST_SECOND;
    else A.dur_s = (A.qi >= 0 && A.qi < (int)A.queue->len) ? ((Item *)A.queue->pdata[A.qi])->dur / 1000.0 : 0;
    double p = pos / (double)GST_SECOND;
    char *s = mkbar(A.dur_s > 0 ? p / A.dur_s : 0, wisp_ui.bar_width, ""), *c = fmt_t(p), *du = fmt_t(A.dur_s);
    g_object_get(A.play, "volume", &v, NULL);
    char *vb = mkbar(v, 10, "vol ");
    gtk_label_set_text(GTK_LABEL(A.seek), s); gtk_label_set_text(GTK_LABEL(A.cur), c); gtk_label_set_text(GTK_LABEL(A.dur), du); gtk_label_set_text(GTK_LABEL(A.vol), vb);
    g_free(s); g_free(c); g_free(du); g_free(vb);
    return G_SOURCE_CONTINUE;
}
static void on_seek_click(GtkGestureClick *g, int n, double x, double y, gpointer d) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    double f = CLAMP(x / MAX(1, gtk_widget_get_width(w)), 0, 1);
    if (A.dur_s > 0) gst_element_seek_simple(A.play, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, (gint64)(f * A.dur_s * GST_SECOND));
}
static void on_vol_click(GtkGestureClick *g, int n, double x, double y, gpointer d) {
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    g_object_set(A.play, "volume", CLAMP(x / MAX(1, gtk_widget_get_width(w)), 0.0, 1.0), NULL);
}

static void activate_item(int i) {
    if (i < 0 || i >= (int)A.items->len) return;
    Item *it = A.items->pdata[i];
    if (!strcmp(it->kind, "user") || !strcmp(it->kind, "playlist")) {
        gboolean u = !strcmp(it->kind, "user");
        char *e = g_uri_escape_string(it->id, NULL, FALSE);
        char *p = g_strdup_printf("/api/%s?id=%s", u ? "user" : "playlist", e), *t = g_strdup_printf("%s %s", u ? "@" : "≡", it->title);
        load_view(p, t, TRUE);
        g_free(e); g_free(p); g_free(t);
        return;
    }
    g_ptr_array_set_size(A.queue, 0);
    int qi = 0;
    for (guint k = 0; k < A.items->len; k++) {
        Item *x = A.items->pdata[k];
        if (strcmp(x->kind, "track")) continue;
        if ((int)k == i) qi = A.queue->len;
        g_ptr_array_add(A.queue, item_dup(x));
    }
    play_idx(qi);
}

/* ───────────── copy link / download ───────────── */
static void copy_url(const char *url) {
    gdk_clipboard_set_text(gtk_widget_get_clipboard(A.win), url);
    say("copied %s", url);
}
static void on_copy_click(GtkButton *b, gpointer d) { copy_url(g_object_get_data(G_OBJECT(b), "url")); gtk_popover_popdown(GTK_POPOVER(A.pop)); }
static void open_menu(void) {
    if (A.qi < 0 || A.qi >= (int)A.queue->len) { say("nothing playing"); return; }
    Item *t = A.queue->pdata[A.qi];
    if (!t->path || !*t->path) { say("no link for this track"); return; }
    A.menu_box = box(GTK_ORIENTATION_VERTICAL, 2);
    char *inst = g_strdup(A.instance ? A.instance : "");
    g_strchomp(inst); while (*inst && inst[strlen(inst) - 1] == '/') inst[strlen(inst) - 1] = 0;
    int i = 1;
    for (const WispCopyTarget *c = wisp_copy_targets; c->label; c++, i++) {
        char **parts = g_strsplit(c->base, "{instance}", -1);
        char *base = g_strjoinv(inst, parts), *url = g_strconcat(base, t->path, NULL);
        const char *sh = strstr(url, "://"); sh = sh ? sh + 3 : url;
        char *l = g_strdup_printf("[%d] Copy (%s) %s", i, c->label, sh);
        GtkWidget *b = btn(l, NULL, G_CALLBACK(on_copy_click), NULL);
        gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(b))), 0);
        g_object_set_data_full(G_OBJECT(b), "url", url, g_free);
        gtk_box_append(GTK_BOX(A.menu_box), b);
        g_free(l); g_free(base); g_strfreev(parts);
    }
    g_free(inst);
    gtk_popover_set_child(GTK_POPOVER(A.pop), A.menu_box);
    gtk_popover_popup(GTK_POPOVER(A.pop));
}
static gboolean on_pop_key(GtkEventControllerKey *c, guint kv, guint kc, GdkModifierType st, gpointer d) {
    if (kv >= GDK_KEY_1 && kv <= GDK_KEY_9 && A.menu_box) {
        GtkWidget *b = gtk_widget_get_first_child(A.menu_box);
        for (guint n = kv - GDK_KEY_1; n && b; n--) b = gtk_widget_get_next_sibling(b);
        if (b) g_signal_emit_by_name(b, "clicked");
        return TRUE;
    }
    return FALSE;
}

typedef struct { char *url, *name; } Dl;
static void dl_free(gpointer p) { Dl *d = p; g_free(d->url); g_free(d->name); g_free(d); }
static void dl_thread(GTask *task, gpointer src, gpointer data, GCancellable *c) {
    Dl *d = data; GError *e = NULL;
    SoupSession *s = soup_session_new();
    SoupMessage *m = soup_message_new("GET", d->url);
    GInputStream *in = soup_session_send(s, m, NULL, &e);
    if (!in) { g_task_return_error(task, e); goto out; }
    const char *ct = soup_message_headers_get_one(soup_message_get_response_headers(m), "Content-Type");
    const char *ext = !ct ? ".mp3" : (strstr(ct, "ogg") || strstr(ct, "opus")) ? ".opus" : (strstr(ct, "mp4") || strstr(ct, "aac")) ? ".m4a" : ".mp3";
    char *nm = g_strdup(d->name);
    for (char *p = nm; *p; p++) if (strchr("/\\:*?\"<>|", *p) || (guchar)*p < 32) *p = '_';
    const char *dir = wisp_ui.download_dir ? wisp_ui.download_dir : g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!dir) dir = g_get_home_dir();
    g_mkdir_with_parents(dir, 0755);
    char *fn = g_strconcat(nm, ext, NULL), *path = g_build_filename(dir, fn, NULL);
    GFile *f = g_file_new_for_path(path);
    GFileOutputStream *out = g_file_replace(f, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, &e);
    if (out && g_output_stream_splice(G_OUTPUT_STREAM(out), in, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, NULL, &e) >= 0)
        g_task_return_pointer(task, g_strdup(path), g_free);
    else g_task_return_error(task, e);
    g_clear_object(&out); g_object_unref(f); g_free(fn); g_free(path); g_free(nm); g_object_unref(in);
out:
    g_object_unref(m); g_object_unref(s);
}
static void dl_done(GObject *o, GAsyncResult *r, gpointer d) {
    GError *e = NULL; char *p = g_task_propagate_pointer(G_TASK(r), &e);
    if (p) { say("saved %s", p); g_free(p); } else { say("download failed: %s", e ? e->message : "?"); g_clear_error(&e); }
}
static void start_download(void) {
    if (A.qi < 0 || A.qi >= (int)A.queue->len) { say("nothing playing"); return; }
    Item *t = A.queue->pdata[A.qi];
    Dl *d = g_new0(Dl, 1);
    d->url = g_strconcat(A.base, t->stream, NULL);
    d->name = *t->sub ? g_strdup_printf("%s - %s", t->sub, t->title) : g_strdup(t->title);
    say("downloading %s …", d->name);
    GTask *task = g_task_new(NULL, NULL, dl_done, NULL);
    g_task_set_task_data(task, d, dl_free);
    g_task_run_in_thread(task, dl_thread);
    g_object_unref(task);
}

/* ───────────── settings window ───────────── */
static struct { GtkWidget *win, *theme, *rd, *ri, *dd, *entry; GPtrArray *urls; char *orig; } S;
static void sync_set(void) {
    gboolean direct = gtk_check_button_get_active(GTK_CHECK_BUTTON(S.rd));
    gboolean custom = S.urls && gtk_drop_down_get_selected(GTK_DROP_DOWN(S.dd)) == S.urls->len;
    gtk_widget_set_sensitive(S.dd, !direct);
    gtk_widget_set_visible(S.entry, custom && !direct);
}
static void on_theme_sel(GObject *o, GParamSpec *p, gpointer d) {
    GtkStringObject *so = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(S.theme));
    if (so) apply_theme(gtk_string_object_get_string(so));
}
static void on_instances(JsonNode *root, const char *err, gpointer ud) {
    if (!S.win) return;
    GtkStringList *sl = gtk_string_list_new(NULL);
    gboolean have_ss = FALSE;
    for (int pass = 0; pass < 2 && root && JSON_NODE_HOLDS_ARRAY(root); pass++) {
        JsonArray *a = json_node_get_array(root);
        for (guint i = 0; i < json_array_get_length(a); i++) {
            JsonObject *o = json_array_get_object_element(a, i);
            if (!o) continue;
            gboolean api = json_object_get_boolean_member_with_default(o, "api", FALSE);
            if (api != (pass == 0)) continue;
            const char *u = js(o, "url");
            if (strstr(u, "ss.2kool4u.net")) have_ss = TRUE;
            char *l = g_strdup_printf("%s · %s", strstr(u, "://") ? strstr(u, "://") + 3 : u, api ? "api" : "html");
            gtk_string_list_append(sl, l); g_free(l);
            g_ptr_array_add(S.urls, g_strdup(u));
        }
    }
    if (!have_ss) { gtk_string_list_append(sl, "ss.2kool4u.net · html"); g_ptr_array_add(S.urls, g_strdup("https://ss.2kool4u.net")); }
    gtk_string_list_append(sl, "custom…");
    gtk_drop_down_set_model(GTK_DROP_DOWN(S.dd), G_LIST_MODEL(sl)); g_object_unref(sl);
    guint sel = S.urls->len;
    for (guint i = 0; i < S.urls->len; i++) if (A.instance && !strcmp(A.instance, S.urls->pdata[i])) sel = i;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(S.dd), sel);
    if (sel == S.urls->len && A.instance) gtk_editable_set_text(GTK_EDITABLE(S.entry), A.instance);
    sync_set();
}
static void close_set(GtkButton *b, gpointer save) {
    if (save) {
        g_free(A.mode); A.mode = g_strdup(gtk_check_button_get_active(GTK_CHECK_BUTTON(S.rd)) ? "direct" : "instance");
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(S.dd));
        g_free(A.instance);
        A.instance = sel < S.urls->len ? g_strdup(S.urls->pdata[sel]) : g_strdup(gtk_editable_get_text(GTK_EDITABLE(S.entry)));
        post_cfg();
        gtk_window_destroy(GTK_WINDOW(S.win));
        if (*gtk_editable_get_text(GTK_EDITABLE(A.entry))) do_search(); else go_home();
    } else { apply_theme(S.orig); gtk_window_destroy(GTK_WINDOW(S.win)); }
}
static void on_set_destroy(GtkWidget *w, gpointer d) { g_ptr_array_unref(S.urls); g_free(S.orig); memset(&S, 0, sizeof S); }
static void open_settings(void) {
    if (S.win) { gtk_window_present(GTK_WINDOW(S.win)); return; }
    S.urls = g_ptr_array_new_with_free_func(g_free); S.orig = g_strdup(A.theme);
    S.win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(S.win), "settings"); gtk_window_set_modal(GTK_WINDOW(S.win), TRUE); gtk_window_set_transient_for(GTK_WINDOW(S.win), GTK_WINDOW(A.win));
    gtk_window_set_default_size(GTK_WINDOW(S.win), 520, 1);
    g_signal_connect(S.win, "destroy", G_CALLBACK(on_set_destroy), NULL);
    GtkWidget *v = box(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(v, 18); gtk_widget_set_margin_bottom(v, 18); gtk_widget_set_margin_start(v, 22); gtk_widget_set_margin_end(v, 22);
    gtk_box_append(GTK_BOX(v), lbl("┤ settings ├", "ac"));
    gtk_box_append(GTK_BOX(v), lbl("theme", "dim"));
    GtkStringList *tl = gtk_string_list_new(NULL); guint tsel = 0, n = 0;
    for (const WispTheme *t = wisp_themes; t->name; t++, n++) { gtk_string_list_append(tl, t->name); if (!strcmp(t->name, A.theme)) tsel = n; }
    S.theme = gtk_drop_down_new(G_LIST_MODEL(tl), NULL); gtk_drop_down_set_selected(GTK_DROP_DOWN(S.theme), tsel);
    g_signal_connect(S.theme, "notify::selected", G_CALLBACK(on_theme_sel), NULL);
    gtk_box_append(GTK_BOX(v), S.theme);
    S.rd = gtk_check_button_new_with_label("direct (soundcloud.com)"); S.ri = gtk_check_button_new_with_label("soundcloak instance");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(S.ri), GTK_CHECK_BUTTON(S.rd));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(!strcmp(A.mode, "direct") ? S.rd : S.ri), TRUE);
    g_signal_connect(S.rd, "toggled", G_CALLBACK(sync_set), NULL);
    gtk_box_append(GTK_BOX(v), S.rd); gtk_box_append(GTK_BOX(v), S.ri);
    gtk_box_append(GTK_BOX(v), lbl("instance", "dim"));
    S.dd = gtk_drop_down_new(NULL, NULL);
    g_signal_connect(S.dd, "notify::selected", G_CALLBACK(sync_set), NULL);
    S.entry = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(S.entry), "https://your-instance.example");
    gtk_box_append(GTK_BOX(v), S.dd); gtk_box_append(GTK_BOX(v), S.entry);
    GtkWidget *acts = box(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_halign(acts, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(acts), btn("[cancel]", NULL, G_CALLBACK(close_set), NULL));
    gtk_box_append(GTK_BOX(acts), btn("[save]", NULL, G_CALLBACK(close_set), GINT_TO_POINTER(1)));
    gtk_box_append(GTK_BOX(v), acts);
    gtk_window_set_child(GTK_WINDOW(S.win), v);
    gtk_window_present(GTK_WINDOW(S.win));
    sync_set();
    http("GET", "/api/instances", NULL, on_instances, NULL);
}
static void on_settings_btn(GtkButton *b, gpointer d) { open_settings(); }

static void on_help_destroy(GtkWidget *w, gpointer d) { A.help = NULL; }
static void toggle_help(void) {
    if (A.help) { gtk_window_destroy(GTK_WINDOW(A.help)); return; }
    GString *g = g_string_new("┤ keys ├\n\n");
    for (const WispKey *k = wisp_keys; k->action; k++) g_string_append_printf(g, "%-10s  %s\n", k->action, k->key);
    g_string_append(g, "\nWisp " WISP_VERSION " — unofficial SoundCloud client");
    A.help = gtk_window_new(); gtk_window_set_title(GTK_WINDOW(A.help), "keys");
    gtk_window_set_transient_for(GTK_WINDOW(A.help), GTK_WINDOW(A.win));
    GtkWidget *l = lbl(g->str, "ac"); g_string_free(g, TRUE);
    gtk_widget_set_margin_top(l, 18); gtk_widget_set_margin_bottom(l, 18); gtk_widget_set_margin_start(l, 22); gtk_widget_set_margin_end(l, 22);
    gtk_window_set_child(GTK_WINDOW(A.help), l);
    g_signal_connect(A.help, "destroy", G_CALLBACK(on_help_destroy), NULL);
    gtk_window_present(GTK_WINDOW(A.help));
}

/* ───────────── keyboard ───────────── */
static void cycle_type(void) {
    GtkWidget *b = gtk_widget_get_first_child(A.types), *on = NULL, *first = b;
    for (; b; b = gtk_widget_get_next_sibling(b)) if (gtk_widget_has_css_class(b, "on")) on = b;
    GtkWidget *nx = on ? gtk_widget_get_next_sibling(on) : first;
    g_signal_emit_by_name(nx ? nx : first, "clicked");
}
static void cycle_theme(void) {
    const WispTheme *t = wisp_themes; int i = 0, cur = 0, n = 0;
    for (; t->name; t++, n++) if (!strcmp(t->name, A.theme)) cur = n;
    (void)i; apply_theme(wisp_themes[(cur + 1) % n].name);
    post_cfg(); say("theme: %s", A.theme);
}
static void run_action(const char *a) {
    if (!strcmp(a, "down")) select_idx(A.sel + 1, TRUE);
    else if (!strcmp(a, "up")) select_idx(A.sel - 1, TRUE);
    else if (!strcmp(a, "select")) activate_item(A.sel);
    else if (!strcmp(a, "back")) go_back();
    else if (!strcmp(a, "home")) go_home();
    else if (!strcmp(a, "search")) gtk_widget_grab_focus(A.entry);
    else if (!strcmp(a, "tab")) cycle_type();
    else if (!strcmp(a, "playPause")) toggle_play();
    else if (!strcmp(a, "next")) play_idx(A.qi + 1);
    else if (!strcmp(a, "prev")) play_idx(A.qi - 1);
    else if (!strcmp(a, "seekFwd")) seek_rel(10);
    else if (!strcmp(a, "seekBack")) seek_rel(-10);
    else if (!strcmp(a, "volUp")) add_vol(.05);
    else if (!strcmp(a, "volDown")) add_vol(-.05);
    else if (!strcmp(a, "copy")) open_menu();
    else if (!strcmp(a, "download")) start_download();
    else if (!strcmp(a, "theme")) cycle_theme();
    else if (!strcmp(a, "settings")) open_settings();
    else if (!strcmp(a, "help")) toggle_help();
}
static gboolean on_key(GtkEventControllerKey *c, guint kv, guint kc, GdkModifierType st, gpointer d) {
    if (st & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) return FALSE;
    GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(A.win));
    if (f && GTK_IS_EDITABLE(f)) {
        if (kv == GDK_KEY_Escape) { select_idx(A.sel, TRUE); return TRUE; }
        return FALSE;
    }
    char buf[8] = {0}; const char *name = NULL;
    switch (kv) {
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: name = "Enter"; break;
    case GDK_KEY_space: name = "Space"; break;
    case GDK_KEY_Tab: name = "Tab"; break;
    case GDK_KEY_Down: run_action("down"); return TRUE;
    case GDK_KEY_Up: run_action("up"); return TRUE;
    case GDK_KEY_Escape: if (A.help) toggle_help(); return FALSE;
    default: { gunichar u = gdk_keyval_to_unicode(kv); if (!u) return FALSE; buf[g_unichar_to_utf8(u, buf)] = 0; name = buf; }
    }
    for (const WispKey *k = wisp_keys; k->action; k++) if (!strcmp(k->key, name)) { run_action(k->action); return TRUE; }
    return FALSE;
}

/* ───────────── UI construction ───────────── */
static void set_on(GtkWidget *parent, GtkWidget *b) {
    for (GtkWidget *c = gtk_widget_get_first_child(parent); c; c = gtk_widget_get_next_sibling(c)) gtk_widget_remove_css_class(c, "on");
    gtk_widget_add_css_class(b, "on");
}
static void on_genre(GtkButton *b, gpointer d) {
    set_on(A.genres, GTK_WIDGET(b));
    g_free(A.genre); A.genre = g_strdup(g_object_get_data(G_OBJECT(b), "id"));
    gtk_editable_set_text(GTK_EDITABLE(A.entry), "");
    char *p = g_strdup_printf("/api/trending?genre=%s", A.genre), *t = g_strdup_printf("trending · %s", gtk_button_get_label(b));
    load_view(p, t, TRUE); g_free(p); g_free(t);
}
static void on_type(GtkButton *b, gpointer d) {
    set_on(A.types, GTK_WIDGET(b));
    g_free(A.type); A.type = g_strdup(g_object_get_data(G_OBJECT(b), "type"));
    do_search();
}
static void on_row_selected(GtkListBox *l, GtkListBoxRow *r, gpointer d) { if (r && gtk_list_box_row_get_selectable(r)) { A.sel = gtk_list_box_row_get_index(r); refresh_markers(); } }
static void on_row_activated(GtkListBox *l, GtkListBoxRow *r, gpointer d) { activate_item(gtk_list_box_row_get_index(r)); }
static void on_entry_activate(GtkEntry *e, gpointer d) { do_search(); gtk_widget_grab_focus(A.list); }
static void cb_prev(GtkButton *b, gpointer d) { play_idx(A.qi - 1); }
static void cb_next(GtkButton *b, gpointer d) { play_idx(A.qi + 1); }
static void cb_pp(GtkButton *b, gpointer d) { toggle_play(); }
static void cb_lk(GtkButton *b, gpointer d) { open_menu(); }
static void cb_dl(GtkButton *b, gpointer d) { start_download(); }

static void on_settings(JsonNode *root, const char *err, gpointer ud) {
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject *o = json_node_get_object(root);
        g_free(A.mode); A.mode = g_strdup(*js(o, "mode") ? js(o, "mode") : "direct");
        g_free(A.instance); A.instance = g_strdup(js(o, "instance"));
        if (*js(o, "theme")) apply_theme(js(o, "theme"));
    }
    go_home();
}
static void activate_app(GtkApplication *app, gpointer d) {
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    A.css = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(A.css), GTK_STYLE_PROVIDER_PRIORITY_USER);
    apply_theme(wisp_ui.default_theme);

    A.items = g_ptr_array_new_with_free_func(item_free); A.queue = g_ptr_array_new_with_free_func(item_free); A.hist = g_ptr_array_new_with_free_func(view_free);
    A.qi = -1; A.type = g_strdup("tracks"); A.genre = g_strdup(wisp_genres[0].id); A.mode = g_strdup("direct");
    A.soup = soup_session_new();

    A.win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(A.win), wisp_ui.title);
    gtk_window_set_default_size(GTK_WINDOW(A.win), wisp_ui.width, wisp_ui.height);
    GtkWidget *root = box(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(root, 12); gtk_widget_set_margin_bottom(root, 12); gtk_widget_set_margin_start(root, 14); gtk_widget_set_margin_end(root, 14);

    /* header: logo + search */
    GtkWidget *hdr = box(GTK_ORIENTATION_HORIZONTAL, 24);
    if (wisp_ui.logo && *wisp_ui.logo) { GtkWidget *lg = lbl(wisp_ui.logo, "ac"); gtk_widget_set_valign(lg, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(hdr), lg); }
    GtkWidget *sb = titled("search", NULL), *row = box(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(sb, TRUE);
    A.entry = gtk_entry_new(); gtk_widget_set_hexpand(A.entry, TRUE); gtk_entry_set_placeholder_text(GTK_ENTRY(A.entry), "artists, tracks, playlists…");
    g_signal_connect(A.entry, "activate", G_CALLBACK(on_entry_activate), NULL);
    gtk_box_append(GTK_BOX(row), lbl("❯", "ac")); gtk_box_append(GTK_BOX(row), A.entry);
    gtk_box_append(GTK_BOX(row), btn("[s]ettings", NULL, G_CALLBACK(on_settings_btn), NULL));
    gtk_box_append(GTK_BOX(sb), row); gtk_box_append(GTK_BOX(hdr), sb);
    gtk_box_append(GTK_BOX(root), hdr);

    /* nav: genres + types */
    GtkWidget *nav = box(GTK_ORIENTATION_HORIZONTAL, 24);
    A.genres = box(GTK_ORIENTATION_HORIZONTAL, 6); A.types = box(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(A.genres, TRUE);
    for (const WispGenre *g = wisp_genres; g->label; g++) {
        GtkWidget *b = btn(g->label, "nav", G_CALLBACK(on_genre), NULL);
        g_object_set_data(G_OBJECT(b), "id", (gpointer)g->id);
        if (g == wisp_genres) gtk_widget_add_css_class(b, "on");
        gtk_box_append(GTK_BOX(A.genres), b);
    }
    const char *types[][2] = {{"tracks", "tracks"}, {"users", "artists"}, {"playlists", "playlists"}};
    for (int i = 0; i < 3; i++) {
        GtkWidget *b = btn(types[i][1], "nav", G_CALLBACK(on_type), NULL);
        g_object_set_data(G_OBJECT(b), "type", (gpointer)types[i][0]);
        if (!i) gtk_widget_add_css_class(b, "on");
        gtk_box_append(GTK_BOX(A.types), b);
    }
    gtk_box_append(GTK_BOX(nav), A.genres); gtk_box_append(GTK_BOX(nav), A.types);
    gtk_box_append(GTK_BOX(root), nav);

    /* main list */
    GtkWidget *mb = titled("trending", &A.title);
    gtk_widget_set_vexpand(mb, TRUE);
    A.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(A.list), GTK_SELECTION_SINGLE);
    g_signal_connect(A.list, "row-selected", G_CALLBACK(on_row_selected), NULL);
    g_signal_connect(A.list, "row-activated", G_CALLBACK(on_row_activated), NULL);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sw, TRUE); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), A.list);
    gtk_box_append(GTK_BOX(mb), sw); gtk_box_append(GTK_BOX(root), mb);

    /* footer: player */
    GtkWidget *fb = titled("now playing", NULL), *r1 = box(GTK_ORIENTATION_HORIZONTAL, 14), *r2 = box(GTK_ORIENTATION_HORIZONTAL, 14), *r3 = box(GTK_ORIENTATION_HORIZONTAL, 14);
    A.np = lbl("—", NULL); gtk_widget_set_hexpand(A.np, TRUE); gtk_label_set_ellipsize(GTK_LABEL(A.np), PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(A.np), 1);
    A.pp = btn("[▶]", NULL, G_CALLBACK(cb_pp), NULL);
    gtk_box_append(GTK_BOX(r1), A.np); gtk_box_append(GTK_BOX(r1), btn("[⏮]", NULL, G_CALLBACK(cb_prev), NULL)); gtk_box_append(GTK_BOX(r1), A.pp); gtk_box_append(GTK_BOX(r1), btn("[⏭]", NULL, G_CALLBACK(cb_next), NULL));
    A.cur = lbl("00:00", "dim"); A.dur = lbl("00:00", "dim"); A.seek = lbl("", "ac"); A.vol = lbl("", "dim");
    GtkGesture *g1 = gtk_gesture_click_new(), *g2 = gtk_gesture_click_new();
    g_signal_connect(g1, "pressed", G_CALLBACK(on_seek_click), NULL); gtk_widget_add_controller(A.seek, GTK_EVENT_CONTROLLER(g1));
    g_signal_connect(g2, "pressed", G_CALLBACK(on_vol_click), NULL); gtk_widget_add_controller(A.vol, GTK_EVENT_CONTROLLER(g2));
    gtk_widget_set_hexpand(r2, TRUE);
    A.lk = btn("[c]opy link", NULL, G_CALLBACK(cb_lk), NULL);
    gtk_box_append(GTK_BOX(r2), A.cur); gtk_box_append(GTK_BOX(r2), A.seek); gtk_box_append(GTK_BOX(r2), A.dur); gtk_box_append(GTK_BOX(r2), A.vol);
    GtkWidget *sp = gtk_label_new(""); gtk_widget_set_hexpand(sp, TRUE); gtk_box_append(GTK_BOX(r2), sp);
    gtk_box_append(GTK_BOX(r2), A.lk); gtk_box_append(GTK_BOX(r2), btn("[d]ownload", NULL, G_CALLBACK(cb_dl), NULL));
    A.msg = lbl("", "ac"); gtk_widget_set_hexpand(A.msg, TRUE);
    char *hint = g_strdup_printf("%s/%s move · %s open · %s back · %s search · %s copy · %s dl · %s help", key_of("down"), key_of("up"), key_of("select"), key_of("back"), key_of("search"), key_of("copy"), key_of("download"), key_of("help"));
    gtk_box_append(GTK_BOX(r3), A.msg); gtk_box_append(GTK_BOX(r3), lbl(hint, "dim")); g_free(hint);
    gtk_box_append(GTK_BOX(fb), r1); gtk_box_append(GTK_BOX(fb), r2); gtk_box_append(GTK_BOX(fb), r3);
    gtk_box_append(GTK_BOX(root), fb);
    gtk_window_set_child(GTK_WINDOW(A.win), root);

    A.pop = gtk_popover_new(); gtk_widget_set_parent(A.pop, A.lk); gtk_popover_set_position(GTK_POPOVER(A.pop), GTK_POS_TOP);
    GtkEventController *pk = gtk_event_controller_key_new();
    g_signal_connect(pk, "key-pressed", G_CALLBACK(on_pop_key), NULL); gtk_widget_add_controller(A.pop, pk);
    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL); gtk_widget_add_controller(A.win, kc);

    /* audio */
    A.play = gst_element_factory_make("playbin", "player");
    if (A.play) {
        g_object_set(A.play, "flags", 0x02 | 0x10, "volume", 0.8, NULL); /* audio + soft volume */
        GstBus *bus = gst_element_get_bus(A.play); gst_bus_add_watch(bus, on_bus, NULL); gst_object_unref(bus);
    }
    g_timeout_add(250, tick, NULL);

    gtk_window_present(GTK_WINDOW(A.win));
    if (!A.play) { say("GStreamer 'playbin' not found — install gst-plugins-base"); return; }
    if (!start_core()) { list_message("could not start wisp-core (set WISP_CORE or put it in PATH)", TRUE); return; }
    http("GET", "/api/settings", NULL, on_settings, NULL);
}
static void on_shutdown(GApplication *app, gpointer d) {
    if (A.play) gst_element_set_state(A.play, GST_STATE_NULL);
    if (A.pop) gtk_widget_unparent(A.pop);
    if (A.core) g_subprocess_force_exit(A.core);
}
int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    GtkApplication *app = gtk_application_new("io.github.wisp", (GApplicationFlags)0);
    g_signal_connect(app, "activate", G_CALLBACK(activate_app), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    int r = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return r;
}
