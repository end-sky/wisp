# Wisp
Beautiful and modern unofficial SoundCloud client for Linux. Go stdlib only, one binary.

# Change logs
Revamped UI.

Added download and share links.

Share using either SC own domains or SoundCloak instances.

Added themes feature.

Added a **config.go** file where you can add your own theme. Needs to recompile everytime you add/change a theme.

## Screenshots

![alt text]([https://github.com/[username]/[reponame]/blob/[branch]/image.jpg?raw=true](https://raw.githubusercontent.com/end-sky/wisp/refs/heads/main/imgs/wisp.png))

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
