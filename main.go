// Wisp - lightweight unofficial SoundCloud client (single binary, stdlib only).
package main

import (
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"html"
	"io"
	"io/fs"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
)

//go:embed web
var webFS embed.FS

const (
	api = "https://api-v2.soundcloud.com"
	ua  = "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0"
)

type Config struct {
	Mode     string `json:"mode"` // "direct" (soundcloud.com) or "instance" (soundcloak)
	Instance string `json:"instance"`
}

var (
	cfg   = Config{Mode: "direct", Instance: "https://sc.kuuro.net"}
	cfgMu sync.Mutex
	cid   string
	cidMu sync.Mutex
	hc    = &http.Client{Transport: &http.Transport{Proxy: http.ProxyFromEnvironment, ResponseHeaderTimeout: 20 * time.Second}}
)

func cfgPath() string { d, _ := os.UserConfigDir(); return filepath.Join(d, "wisp", "config.json") }

func loadCfg() {
	if b, err := os.ReadFile(cfgPath()); err == nil {
		json.Unmarshal(b, &cfg)
	}
}

func saveCfg() {
	os.MkdirAll(filepath.Dir(cfgPath()), 0o755)
	b, _ := json.Marshal(cfg)
	os.WriteFile(cfgPath(), b, 0o644)
}

func mode() (string, string) {
	cfgMu.Lock()
	defer cfgMu.Unlock()
	return cfg.Mode, strings.TrimRight(cfg.Instance, "/")
}

func get(ctx *http.Request, u string, hdr map[string]string) (*http.Response, error) {
	req, _ := http.NewRequest("GET", u, nil)
	if ctx != nil {
		req = req.WithContext(ctx.Context())
	}
	req.Header.Set("User-Agent", ua)
	for k, v := range hdr {
		req.Header.Set(k, v)
	}
	return hc.Do(req)
}

func getBody(u string) ([]byte, error) {
	r, err := get(nil, u, nil)
	if err != nil {
		return nil, err
	}
	defer r.Body.Close()
	if r.StatusCode >= 400 {
		return nil, fmt.Errorf("HTTP %d from %s", r.StatusCode, r.Request.URL.Host)
	}
	return io.ReadAll(r.Body)
}

// ---- direct mode: scrape a public client_id out of SoundCloud's web bundles ----

var (
	reScript = regexp.MustCompile(`<script[^>]+src="(https://a-v2\.sndcdn\.com/assets/[^"]+\.js)"`)
	reCID    = regexp.MustCompile(`client_id[:=]"?([a-zA-Z0-9]{32})"?`)
)

func clientID(force bool) (string, error) {
	cidMu.Lock()
	defer cidMu.Unlock()
	if cid != "" && !force {
		return cid, nil
	}
	b, err := getBody("https://soundcloud.com")
	if err != nil {
		return "", err
	}
	ms := reScript.FindAllStringSubmatch(string(b), -1)
	for i := len(ms) - 1; i >= 0; i-- {
		js, err := getBody(ms[i][1])
		if err != nil {
			continue
		}
		if m := reCID.FindSubmatch(js); m != nil {
			cid = string(m[1])
			return cid, nil
		}
	}
	return "", fmt.Errorf("could not find a client_id")
}

func scGet(path string, q url.Values) ([]byte, error) {
	var err error
	for try := 0; try < 2; try++ {
		var id string
		if id, err = clientID(try > 0); err != nil {
			return nil, err
		}
		v := url.Values{}
		for k, x := range q {
			v[k] = x
		}
		v.Set("client_id", id)
		var b []byte
		if b, err = getBody(api + path + "?" + v.Encode()); err == nil {
			return b, nil
		}
		if !strings.Contains(err.Error(), "HTTP 401") && !strings.Contains(err.Error(), "HTTP 403") {
			break
		}
	}
	return nil, err
}

type scObj struct {
	Kind           string `json:"kind"`
	ID             int64  `json:"id"`
	Title          string `json:"title"`
	Username       string `json:"username"`
	ArtworkURL     string `json:"artwork_url"`
	AvatarURL      string `json:"avatar_url"`
	Duration       int    `json:"duration"`
	TrackCount     int    `json:"track_count"`
	FollowersCount int    `json:"followers_count"`
	User           struct {
		Username  string `json:"username"`
		AvatarURL string `json:"avatar_url"`
	} `json:"user"`
	Tracks []scObj `json:"tracks"`
}

type Item struct {
	Kind   string `json:"kind"` // track | user | playlist
	ID     string `json:"id"`
	Title  string `json:"title"`
	Sub    string `json:"sub"`
	Art    string `json:"art"`
	Dur    int    `json:"dur"`
	Stream string `json:"stream,omitempty"`
}

func toItem(o scObj) Item {
	it := Item{Kind: o.Kind, ID: fmt.Sprint(o.ID), Title: o.Title, Art: o.ArtworkURL, Dur: o.Duration}
	switch o.Kind {
	case "user":
		it.Title, it.Art = o.Username, o.AvatarURL
		it.Sub = fmt.Sprintf("%d followers", o.FollowersCount)
	case "playlist":
		it.Sub = fmt.Sprintf("%s · %d tracks", o.User.Username, o.TrackCount)
	default:
		it.Kind, it.Sub, it.Stream = "track", o.User.Username, "/stream?id="+it.ID
		if it.Art == "" {
			it.Art = o.User.AvatarURL
		}
	}
	it.Art = strings.Replace(it.Art, "-large", "-t200x200", 1)
	return it
}

func items(os []scObj) []Item {
	out := []Item{}
	for _, o := range os {
		if it := toItem(o); it.Title != "" {
			out = append(out, it)
		}
	}
	return out
}

func hydrate(ts []scObj) []scObj { // playlists return stub tracks (id only)
	var ids []string
	for _, t := range ts {
		if t.Title == "" && len(ids) < 50 {
			ids = append(ids, fmt.Sprint(t.ID))
		}
	}
	if len(ids) == 0 {
		return ts
	}
	b, err := scGet("/tracks", url.Values{"ids": {strings.Join(ids, ",")}})
	if err != nil {
		return ts
	}
	var full []scObj
	json.Unmarshal(b, &full)
	m := map[int64]scObj{}
	for _, f := range full {
		m[f.ID] = f
	}
	for i, t := range ts {
		if f, ok := m[t.ID]; ok {
			ts[i] = f
		}
	}
	return ts
}

func coll(b []byte) []scObj {
	var c struct {
		Collection []scObj `json:"collection"`
	}
	json.Unmarshal(b, &c)
	return c.Collection
}

// ---- instance mode: best-effort scraping of a soundcloak instance's HTML ----

var (
	reA   = regexp.MustCompile(`(?s)<a[^>]+href="(/[^"?#]+)"[^>]*>(.*?)</a>`)
	reTag = regexp.MustCompile(`<[^>]*>`)
)

func scrape(inst, path, kind string) ([]Item, error) {
	b, err := getBody(inst + path)
	if err != nil {
		return nil, err
	}
	out, seen := []Item{}, map[string]bool{}
	for _, m := range reA.FindAllStringSubmatch(string(b), -1) {
		p := m[1]
		segs := strings.Split(strings.Trim(p, "/"), "/")
		t := strings.TrimSpace(html.UnescapeString(reTag.ReplaceAllString(m[2], "")))
		if t == "" || seen[p] || strings.HasPrefix(p, "/_") || strings.HasPrefix(p, "/search") {
			continue
		}
		ok := false
		switch kind {
		case "user":
			ok = len(segs) == 1
		case "playlist":
			ok = len(segs) == 3 && segs[1] == "sets"
		default:
			ok = len(segs) == 2 && segs[1] != "sets" && segs[1] != "tracks" && segs[1] != "likes"
		}
		if !ok {
			continue
		}
		seen[p] = true
		it := Item{Kind: kind, ID: p, Title: t, Sub: segs[0]}
		if kind == "track" {
			it.Stream = inst + "/_/restream" + p
		}
		out = append(out, it)
	}
	return out, nil
}

// ---- audio proxy (direct mode) ----

type trans struct {
	URL    string `json:"url"`
	Format struct {
		Protocol string `json:"protocol"`
		Mime     string `json:"mime_type"`
	} `json:"format"`
}

func stream(w http.ResponseWriter, r *http.Request) {
	b, err := scGet("/tracks/"+r.URL.Query().Get("id"), nil)
	if err != nil {
		http.Error(w, err.Error(), 502)
		return
	}
	var t struct {
		Auth  string `json:"track_authorization"`
		Media struct {
			T []trans `json:"transcodings"`
		} `json:"media"`
	}
	json.Unmarshal(b, &t)
	var pick *trans
	for i, x := range t.Media.T { // prefer progressive mp3, then HLS mp3, then any HLS
		if x.Format.Protocol == "progressive" {
			pick = &t.Media.T[i]
			break
		}
	}
	for i, x := range t.Media.T {
		if pick == nil && strings.Contains(x.Format.Mime, "mpeg") {
			pick = &t.Media.T[i]
		}
	}
	if pick == nil && len(t.Media.T) > 0 {
		pick = &t.Media.T[0]
	}
	if pick == nil {
		http.Error(w, "no playable stream", 404)
		return
	}
	id, _ := clientID(false)
	rb, err := getBody(pick.URL + "?client_id=" + id + "&track_authorization=" + url.QueryEscape(t.Auth))
	var res struct{ URL string }
	if err != nil || json.Unmarshal(rb, &res) != nil || res.URL == "" {
		http.Error(w, "could not resolve stream", 502)
		return
	}
	hdr := map[string]string{}
	if rg := r.Header.Get("Range"); rg != "" && pick.Format.Protocol == "progressive" {
		hdr["Range"] = rg
	}
	resp, err := get(r, res.URL, hdr)
	if err != nil {
		http.Error(w, err.Error(), 502)
		return
	}
	defer resp.Body.Close()
	if pick.Format.Protocol == "progressive" {
		for _, h := range []string{"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"} {
			if v := resp.Header.Get(h); v != "" {
				w.Header().Set(h, v)
			}
		}
		w.WriteHeader(resp.StatusCode)
		io.Copy(w, resp.Body)
		return
	}
	// HLS: stitch the segments into one continuous stream
	pl, _ := io.ReadAll(resp.Body)
	base, _ := url.Parse(res.URL)
	w.Header().Set("Content-Type", strings.Split(pick.Format.Mime, ";")[0])
	fl, _ := w.(http.Flusher)
	for _, ln := range strings.Split(string(pl), "\n") {
		ln = strings.TrimSpace(ln)
		if ln == "" || strings.HasPrefix(ln, "#") {
			continue
		}
		ref, err := base.Parse(ln)
		if err != nil {
			continue
		}
		sr, err := get(r, ref.String(), nil)
		if err != nil {
			return
		}
		_, err = io.Copy(w, sr.Body)
		sr.Body.Close()
		if err != nil {
			return
		}
		if fl != nil {
			fl.Flush()
		}
	}
}

func reply(w http.ResponseWriter, v any, err error) {
	if err != nil {
		http.Error(w, err.Error(), 502)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(v)
}

func main() {
	port := flag.Int("port", 47653, "local port")
	nob := flag.Bool("no-window", false, "do not open a window")
	flag.Parse()
	loadCfg()

	mux := http.NewServeMux()
	sub, _ := fs.Sub(webFS, "web")
	mux.Handle("/", http.FileServer(http.FS(sub)))
	mux.HandleFunc("/stream", stream)

	mux.HandleFunc("/api/settings", func(w http.ResponseWriter, r *http.Request) {
		cfgMu.Lock()
		defer cfgMu.Unlock()
		if r.Method == "POST" {
			var c Config
			if json.NewDecoder(r.Body).Decode(&c) == nil && (c.Mode == "direct" || c.Mode == "instance") {
				cfg = c
				saveCfg()
			}
		}
		reply(w, cfg, nil)
	})

	mux.HandleFunc("/api/search", func(w http.ResponseWriter, r *http.Request) {
		t, q := r.URL.Query().Get("type"), r.URL.Query().Get("q")
		m, inst := mode()
		var out []Item
		var err error
		if m == "instance" {
			kind := map[string]string{"tracks": "track", "users": "user", "playlists": "playlist"}[t]
			out, err = scrape(inst, "/search/"+t+"?q="+url.QueryEscape(q), kind)
		} else {
			ep := map[string]string{"tracks": "tracks", "users": "users", "playlists": "playlists_without_albums"}[t]
			var b []byte
			if b, err = scGet("/search/"+ep, url.Values{"q": {q}, "limit": {"30"}}); err == nil {
				out = items(coll(b))
			}
		}
		reply(w, out, err)
	})

	mux.HandleFunc("/api/trending", func(w http.ResponseWriter, r *http.Request) {
		m, inst := mode()
		if m == "instance" { // soundcloak has no charts page; show SoundCloud's official account tracks
			out, err := scrape(inst, "/soundcloud/tracks", "track")
			reply(w, out, err)
			return
		}
		g := r.URL.Query().Get("genre")
		if g == "" {
			g = "all-music"
		}
		b, err := scGet("/charts", url.Values{"kind": {"trending"}, "genre": {"soundcloud:genres:" + g}, "limit": {"40"}})
		var c struct {
			Collection []struct {
				Track scObj `json:"track"`
			} `json:"collection"`
		}
		var out []scObj
		if err == nil {
			json.Unmarshal(b, &c)
			for _, x := range c.Collection {
				out = append(out, x.Track)
			}
		}
		reply(w, items(out), err)
	})

	mux.HandleFunc("/api/user", func(w http.ResponseWriter, r *http.Request) {
		id := r.URL.Query().Get("id")
		if m, inst := mode(); m == "instance" {
			out, err := scrape(inst, id, "track")
			reply(w, out, err)
			return
		}
		b, err := scGet("/users/"+url.PathEscape(id)+"/tracks", url.Values{"limit": {"50"}})
		reply(w, items(coll(b)), err)
	})

	mux.HandleFunc("/api/playlist", func(w http.ResponseWriter, r *http.Request) {
		id := r.URL.Query().Get("id")
		if m, inst := mode(); m == "instance" {
			out, err := scrape(inst, id, "track")
			reply(w, out, err)
			return
		}
		b, err := scGet("/playlists/"+url.PathEscape(id), nil)
		var p scObj
		if err == nil {
			json.Unmarshal(b, &p)
		}
		reply(w, items(hydrate(p.Tracks)), err)
	})

	ln, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", *port))
	if err != nil {
		ln, _ = net.Listen("tcp", "127.0.0.1:0")
	}
	u := "http://" + ln.Addr().String()
	fmt.Println("Wisp running at", u)
	if !*nob {
		go openUI(u)
	}
	http.Serve(ln, mux)
}

func openUI(u string) {
	for _, b := range []string{"chromium", "chromium-browser", "google-chrome", "google-chrome-stable", "brave-browser", "microsoft-edge"} {
		if p, err := exec.LookPath(b); err == nil {
			exec.Command(p, "--app="+u, "--class=Wisp").Start()
			return
		}
	}
	exec.Command("xdg-open", u).Start()
}
