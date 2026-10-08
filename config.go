package main

// ─────────────────────────────────────────────────────────────────────────────
//  WISP UI CONFIG — hack me!
//  Edit anything below, then rebuild:  go build -ldflags="-s -w" -o wisp .
//  The whole UI (colours, font, glyphs, genres, keybinds, copy-link targets,
//  even raw CSS) is driven from here. Nothing in web/index.html needs touching.
// ─────────────────────────────────────────────────────────────────────────────

type Theme struct {
	Name   string `json:"name"`
	BG     string `json:"bg"`     // window background (#000000 = AMOLED)
	FG     string `json:"fg"`     // normal text
	Dim    string `json:"dim"`    // secondary text (artist, time, hints)
	Accent string `json:"accent"` // titles, progress bar, now-playing
	Border string `json:"border"` // box borders
	SelBG  string `json:"selBg"`  // highlighted row background
	SelFG  string `json:"selFg"`  // highlighted row text
	Error  string `json:"error"`
}

// Add your own theme here; cycle through them in-app with the "theme" key or in Settings.
var Themes = []Theme{
	{"amoled-purple", "#000000", "#d8d8e0", "#6e6a80", "#b266ff", "#3a2a55", "#b266ff", "#000000", "#ff5f6d"},
	{"phosphor", "#000000", "#33ff66", "#1c9a3e", "#66ff99", "#0f4d20", "#33ff66", "#000000", "#ff5555"},
	{"amber", "#000000", "#ffb000", "#8a6000", "#ffd060", "#4d3500", "#ffb000", "#000000", "#ff5555"},
	{"mono", "#000000", "#e6e6e6", "#777777", "#ffffff", "#444444", "#e6e6e6", "#000000", "#ff5555"},
	{"cyber", "#000000", "#00e5ff", "#237a88", "#ff2bd6", "#1b3a44", "#ff2bd6", "#000000", "#ff5555"},
}

type Genre struct {
	Label string `json:"label"`
	ID    string `json:"id"` // SoundCloud genre id (e.g. hiphoprap, electronic)
}

// CopyTarget: Base + /artist/track. "{instance}" is replaced by the instance chosen in Settings.
type CopyTarget struct {
	Label string `json:"label"`
	Base  string `json:"base"`
}

type UIConfig struct {
	Title        string            `json:"title"`
	Logo         string            `json:"logo"`     // multi-line ASCII; empty = just Title
	Font         string            `json:"font"`     // CSS font-family (monospace recommended)
	FontSize     int               `json:"fontSize"` // px
	Prompt       string            `json:"prompt"`   // marker drawn in front of the selected row
	NowPlaying   string            `json:"nowPlaying"`
	BarFull      string            `json:"barFull"` // progress bar glyphs
	BarEmpty     string            `json:"barEmpty"`
	BarWidth     int               `json:"barWidth"`
	BorderStyle  string            `json:"borderStyle"` // solid | double | dashed | dotted
	ShowArt      bool              `json:"showArt"`     // tiny pixelated cover thumbnails
	Scanlines    bool              `json:"scanlines"`   // CRT scanline overlay
	Animations   bool              `json:"animations"`  // row reveal, blinking cursor
	DefaultTheme string            `json:"defaultTheme"`
	Genres       []Genre           `json:"genres"`
	CopyTargets  []CopyTarget      `json:"copyTargets"`
	Keys         map[string]string `json:"keys"` // action -> key (KeyboardEvent.key). Arrow keys always work too.
	CustomCSS    string            `json:"customCss"`
}

var UI = UIConfig{
	Title:        "wisp",
	Logo:         "░█░█░▀█▀░█▀▀░█▀█\n░█▄█░░█░░▀▀█░█▀▀\n░▀░▀░▀▀▀░▀▀▀░▀░░",
	Font:         `"JetBrains Mono","Fira Code","DejaVu Sans Mono",ui-monospace,monospace`,
	FontSize:     14,
	Prompt:       ">",
	NowPlaying:   "♪",
	BarFull:      "█",
	BarEmpty:     "░",
	BarWidth:     36,
	BorderStyle:  "solid",
	ShowArt:      false,
	Scanlines:    false,
	Animations:   true,
	DefaultTheme: "amoled-purple",
	Genres: []Genre{
		{"trending", "all-music"}, {"hip-hop", "hiphoprap"}, {"electronic", "electronic"},
		{"dance", "danceedm"}, {"pop", "pop"}, {"r&b", "rbsoul"},
	},
	CopyTargets: []CopyTarget{
		{"SoundCloud", "https://soundcloud.com"},
		{"Soundcloak", "{instance}"},
	},
	Keys: map[string]string{
		"down": "j", "up": "k", "select": "Enter", "back": "h", "home": "g",
		"search": "/", "tab": "Tab", "playPause": " ", "next": "n", "prev": "p",
		"seekFwd": "]", "seekBack": "[", "volUp": "+", "volDown": "-",
		"copy": "c", "download": "d", "theme": "t", "settings": "s", "help": "?",
	},
	CustomCSS: ``, // e.g. `.row.sel{text-decoration:underline}`
}
