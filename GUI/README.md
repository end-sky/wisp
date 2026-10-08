# Wisp v2.0.0

Lightweight, unofficial SoundCloud client for Linux.

- `ui/`   — the GUI, written in C (GTK4 + GStreamer). Theme/keys/genres/copy targets: `ui/config.c`
- `core/` — the scraper, written in Go (stdlib only). The GUI starts it automatically on a random localhost port.

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
