# Wisp
Beautiful and modern unofficial SoundCloud client for Linux. Go stdlib only, one binary.

# Change logs
Revamped UI.

Added download and share links.

Share using either SC own domains or SoundCloak instances.

Added themes feature.

Added a **config.go** file where you can add your own theme. Needs to recompile everytime you add/change a theme.

Made a GUI version.

## Screenshots

![wisp main page](https://raw.githubusercontent.com/end-sky/wisp/refs/heads/main/imgs/wisp.png)
# Instances
Instance mode: pick ss.2kool4u.net / sc.kuuro.net or a custom Soundcloak-style URL in Settings.
If SoundCloud rotates its client_id and auto-detection ever fails, set "client_id" in config.json.

Soundcloak instances with EnableAPI (see maid.zone/soundcloak/instances.json) are auto-detected via /_/info and used
through their /_/api/v2 proxy + /_/api/{progressive,restream,hls} streams; others fall back to HTML scraping.

## Build (Nix)
    nix build            # builds both; result/bin/wisp finds wisp-core itself
    nix run
    nix develop          # dev shell with go, gcc, gtk4, gstreamer…

## Build (manual)
    cd core && go build -ldflags="-s -w" -o ../ui/wisp-core .
    cd ../ui && make && ./wisp          # needs gtk4, libsoup3, json-glib, gstreamer (+ good/ugly/libav plugins)

Keys: j/k move · Enter open · h back · g home · / search · Space pause · n/p next/prev · [ ] seek · + - volume ·
c copy link · d download · t theme · s settings · ? help   (all rebindable in ui/config.c)

Settings live in ~/.config/wisp/config.json (mode, instance, theme, cached client_id).
Downloads go to your XDG Downloads folder (or `download_dir` in config.c).

# Run:
```bash
./result/bin/wisp
```
