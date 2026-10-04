# Wisp
Lightweight unofficial SoundCloud client for Linux. Go stdlib only, one binary.

Build:  
```bash
go build -ldflags="-s -w" -o wisp .
```

(needs Go 1.21+)

Run:
```bash
./wisp
```

(opens in Chromium/Chrome/Brave app mode, else your default browser)

Flags:  
```txt
-port 47653  -no-window
```

Config: 

```txt
~/.config/wisp/config.json
```

(also editable via the ⚙ settings dialog)
