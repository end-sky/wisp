/* ─────────────────────────────────────────────────────────────────────────────
 *  WISP UI CONFIG — hack me!
 *  Edit anything below, then rebuild (`nix build` or `make`). Colours, font,
 *  glyphs, genres, keybinds, copy-link targets and raw CSS all live here.
 * ───────────────────────────────────────────────────────────────────────────── */
#include "config.h"

const WispUI wisp_ui = {
    .title = "wisp",
    .logo = "░█░█░▀█▀░█▀▀░█▀█\n░█▄█░░█░░▀▀█░█▀▀\n░▀░▀░▀▀▀░▀▀▀░▀░░",
    .font = "\"JetBrains Mono\", \"Fira Code\", \"DejaVu Sans Mono\", monospace",
    .font_size = 14,
    .width = 980, .height = 720,
    .prompt = ">",
    .now_playing = "♪",
    .bar_full = "█", .bar_empty = "░", .bar_width = 36,
    .default_theme = "amoled-purple",
    .download_dir = NULL,          /* NULL = your XDG Downloads folder; or e.g. "/home/me/Music/wisp" */
    .custom_css = "",              /* e.g. "row:selected { text-decoration: underline; }" */
};

/* name, bg, fg, dim, accent, border, sel_bg, sel_fg, error — add your own! */
const WispTheme wisp_themes[] = {
    {"amoled-purple", "#000000", "#d8d8e0", "#6e6a80", "#b266ff", "#3a2a55", "#b266ff", "#000000", "#ff5f6d"},
    {"phosphor",      "#000000", "#33ff66", "#1c9a3e", "#66ff99", "#0f4d20", "#33ff66", "#000000", "#ff5555"},
    {"amber",         "#000000", "#ffb000", "#8a6000", "#ffd060", "#4d3500", "#ffb000", "#000000", "#ff5555"},
    {"mono",          "#000000", "#e6e6e6", "#777777", "#ffffff", "#444444", "#e6e6e6", "#000000", "#ff5555"},
    {"cyber",         "#000000", "#00e5ff", "#237a88", "#ff2bd6", "#1b3a44", "#ff2bd6", "#000000", "#ff5555"},
    {NULL}
};

const WispGenre wisp_genres[] = {
    {"trending", "all-music"}, {"hip-hop", "hiphoprap"}, {"electronic", "electronic"},
    {"dance", "danceedm"}, {"pop", "pop"}, {"r&b", "rbsoul"},
    {NULL}
};

const WispCopyTarget wisp_copy_targets[] = {
    {"SoundCloud", "https://soundcloud.com"},
    {"Soundcloak", "{instance}"},
    {NULL}
};

/* Arrow keys always work too. */
const WispKey wisp_keys[] = {
    {"down", "j"}, {"up", "k"}, {"select", "Enter"}, {"back", "h"}, {"home", "g"},
    {"search", "/"}, {"tab", "Tab"}, {"playPause", "Space"}, {"next", "n"}, {"prev", "p"},
    {"seekFwd", "]"}, {"seekBack", "["}, {"volUp", "+"}, {"volDown", "-"},
    {"copy", "c"}, {"download", "d"}, {"theme", "t"}, {"settings", "s"}, {"help", "?"},
    {NULL}
};
