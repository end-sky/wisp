#pragma once
#include <glib.h>
#define WISP_VERSION "1.0.0"

typedef struct { const char *name, *bg, *fg, *dim, *accent, *border, *sel_bg, *sel_fg, *error; } WispTheme;
typedef struct { const char *label, *id; } WispGenre;               /* SoundCloud genre id */
typedef struct { const char *label, *base; } WispCopyTarget;        /* base + /artist/track ; "{instance}" = chosen instance */
typedef struct { const char *action, *key; } WispKey;               /* key: "j", "Enter", "Space", "Tab", "Esc", "Up", "Down"... */
typedef struct {
    const char *title, *logo, *font;
    int font_size, width, height;
    const char *prompt, *now_playing, *bar_full, *bar_empty;
    int bar_width;
    const char *default_theme, *download_dir, *custom_css;
} WispUI;

/* arrays below are terminated by an entry whose first field is NULL */
extern const WispUI wisp_ui;
extern const WispTheme wisp_themes[];
extern const WispGenre wisp_genres[];
extern const WispCopyTarget wisp_copy_targets[];
extern const WispKey wisp_keys[];
