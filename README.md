# Wisp
Beautiful and modern unofficial SoundCloud client for Linux. Go stdlib only, one binary.

# Change logs
Improved support. Longterm front-end support. 

added new alternative instance SCScrape so you can load the homepage/music using the new instance instead of loading it locally through the SC website.

UI Improvement, the UI has been revamped.

Old binary and version still works fine. Build still the same. no dependencies.

Fixed instances. It now uses Soundcloak's API.

# Instances
Instance mode: pick ss.2kool4u.net / sc.kuuro.net or a custom Soundcloak-style URL in Settings.
If SoundCloud rotates its client_id and auto-detection ever fails, set "client_id" in config.json.

Soundcloak instances with EnableAPI (see maid.zone/soundcloak/instances.json) are auto-detected via /_/info and used
through their /_/api/v2 proxy + /_/api/{progressive,restream,hls} streams; others fall back to HTML scraping.

# Build:  
```bash
go build -ldflags="-s -w" -o wisp .
```

(needs Go 1.21+)

# Run:
```bash
./wisp
```

(opens in Chromium/Chrome/Brave app mode, else your default browser)

# Flags:  
```txt
-port 47653
-no-window
```

# Config: 

```txt
~/.config/wisp/config.json
```

(also editable via the ⚙ settings dialog)

# Note:
You may also need gcc to build this application.
