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
	"net/http/cookiejar"
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
	Mode     string `json:"mode"` // "direct" or "instance"
	Instance string `json:"instance"`
	ClientID string `json:"client_id,omitempty"` // cached (or manually set) SoundCloud client_id
}

func newClient() *http.Client {
	jar, _ := cookiejar.New(nil)
	return &http.Client{Jar: jar, Transport: &http.Transport{Proxy: http.ProxyFromEnvironment, ResponseHeaderTimeout: 20 * time.Second}}
}

var (
	cfg   = Config{Mode: "direct", Instance: "https://sc.kuuro.net"}
	cfgMu sync.Mutex
	cid   string
	cidMu sync.Mutex
	hc    = newClient()
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
	req, err := http.NewRequest("GET", u, nil)
	if err != nil {
		return nil, err
	}
	if ctx != nil {
		req = req.WithContext(ctx.Context())
	}
	req.Header.Set("User-Agent", ua)
	req.Header.Set("Accept", "text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8")
	req.Header.Set("Accept-Language", "en-US,en;q=0.9")
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

// ---- direct mode: several independent ways to find SoundCloud's public client_id ----

var (
	reScript = regexp.MustCompile(`<script[^>]+src=["'](https://[a-z0-9.-]*sndcdn\.com/[^"']+\.js)["']`)
	reHydr   = regexp.MustCompile(`"hydratable":"apiClient","data":\{"id":"([a-zA-Z0-9]{32})"`)
	reCIDs   = []*regexp.Regexp{
		regexp.MustCompile(`client_id\s*[:=]\s*"([a-zA-Z0-9]{32})"`),
		regexp.MustCompile(`client_id=([a-zA-Z0-9]{32})`),
		regexp.MustCompile(`"client_id"\s*,\s*"([a-zA-Z0-9]{32})"`),
		regexp.MustCompile(`clientId\s*[:=]\s*"([a-zA-Z0-9]{32})"`),
	}
)

func findCID(b []byte) string {
	for _, re := range reCIDs {
		if m := re.FindSubmatch(b); m != nil {
			return string(m[1])
		}
	}
	return ""
}

func clientID(force bool) (string, error) {
	cidMu.Lock()
	defer cidMu.Unlock()
	if !force {
		if cid == "" {
			cfgMu.Lock()
			cid = cfg.ClientID
			cfgMu.Unlock()
		}
		if cid != "" {
			return cid, nil
		}
	}
	b, err := getBody("https://soundcloud.com")
	if err != nil {
		return "", err
	}
	found := ""
	if m := reHydr.FindSubmatch(b); m != nil { // 1) hydration JSON on the home page
		found = string(m[1])
	}
	if found == "" { // 2) inline scripts / page source
		found = findCID(b)
	}
	if found == "" { // 3) the JS bundles (newest first)
		ms := reScript.FindAllStringSubmatch(string(b), -1)
		for i := len(ms) - 1; i >= 0 && found == ""; i-- {
			if js, err := getBody(ms[i][1]); err == nil {
				found = findCID(js)
			}
		}
	}
	if found == "" {
		return "", fmt.Errorf("could not find a client_id (you can set \"client_id\" manually in config.json)")
	}
	cid = found
	cfgMu.Lock()
	cfg.ClientID = found
	saveCfg()
	cfgMu.Unlock()
	return cid, nil
}

func scGet(path string, q url.Values) ([]byte, error) {
	var err error
	for try := 0; try < 3; try++ {
		var id string
		if id, err = clientID(try == 1); err != nil {
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
		e := err.Error()
		switch {
		case strings.Contains(e, "HTTP 401"), strings.Contains(e, "HTTP 403"): // stale id: refreshed on next loop
		case strings.Contains(e, "HTTP 429"), strings.Contains(e, "HTTP 5"):
			time.Sleep(700 * time.Millisecond)
		default:
			return nil, err
		}
	}
	return nil, err
}

type flexID string // ids may arrive as numbers or strings

func (f *flexID) UnmarshalJSON(b []byte) error { *f = flexID(strings.Trim(string(b), `"`)); return nil }

type scObj struct {
	Kind           string `json:"kind"`
	ID             flexID  `json:"id"`
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
	m := map[flexID]scObj{}
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

// ---- instance mode: tolerant scraping of Soundcloak-style HTML ----

var (
	reA     = regexp.MustCompile(`(?is)<a\b[^>]*?href\s*=\s*["']([^"']+)["'][^>]*>(.*?)</a>`)
	reImg   = regexp.MustCompile(`(?is)<img\b[^>]*?src\s*=\s*["']([^"']+)["']`)
	reTag   = regexp.MustCompile(`(?s)<[^>]*>`)
	reMedia = regexp.MustCompile(`(?is)<(?:audio|source|video)\b[^>]*?src\s*=\s*["']([^"']+)["']`)
	reMap   = regexp.MustCompile(`URI="([^"]+)"`)
	reNum   = regexp.MustCompile(`^[\d:.,KMkm\s]*$`)
	skipSeg = map[string]bool{"search": true, "login": true, "about": true, "settings": true, "static": true, "discover": true, "charts": true, "you": true, "upload": true, "feed": true, "pages": true, "privacy": true, "terms-of-use": true, "tags": true, "download": true, "go": true, "stations": true}
	subPage = map[string]bool{"sets": true, "tracks": true, "likes": true, "reposts": true, "albums": true, "popular-tracks": true, "followers": true, "following": true, "comments": true, "recommended": true}
)

func scrapeAny(inst, kind string, paths ...string) ([]Item, error) {
	var err error
	for _, p := range paths {
		var out []Item
		if out, err = scrape(inst, p, kind); err == nil && len(out) > 0 {
			return out, nil
		}
	}
	if err == nil {
		err = fmt.Errorf("nothing could be parsed from %s (layout changed, or the instance blocks automated requests)", inst)
	}
	return nil, err
}

// scrape needs no fixed markup: it collects every link that looks like /user, /user/track or
// /user/sets/name (relative or absolute), then merges titles and artwork found around them.
func scrape(inst, path, kind string) ([]Item, error) {
	base, err := url.Parse(inst)
	if err != nil {
		return nil, err
	}
	b, err := getBody(inst + path)
	if err != nil {
		return nil, err
	}
	out, idx := []Item{}, map[string]int{}
	for _, m := range reA.FindAllStringSubmatch(string(b), -1) {
		u, err := base.Parse(html.UnescapeString(m[1]))
		if err != nil || (u.Host != base.Host && !strings.HasSuffix(u.Host, "soundcloud.com")) {
			continue
		}
		p := strings.TrimRight(u.Path, "/")
		segs := strings.Split(strings.Trim(p, "/"), "/")
		if p == "" || skipSeg[segs[0]] || strings.HasPrefix(segs[0], "_") {
			continue
		}
		ok := false
		switch kind {
		case "user":
			ok = len(segs) == 1
		case "playlist":
			ok = len(segs) == 3 && segs[1] == "sets"
		default:
			ok = len(segs) == 2 && !subPage[segs[1]]
		}
		if !ok {
			continue
		}
		t := strings.Join(strings.Fields(html.UnescapeString(reTag.ReplaceAllString(m[2], " "))), " ")
		art := ""
		if im := reImg.FindStringSubmatch(m[2]); im != nil {
			if au, err := base.Parse(html.UnescapeString(im[1])); err == nil {
				art = au.String()
			}
		}
		i, seen := idx[p]
		if !seen {
			it := Item{Kind: kind, ID: p, Sub: segs[0]}
			if kind == "track" {
				it.Stream = "/stream?p=" + url.QueryEscape(p)
			}
			out = append(out, it)
			i = len(out) - 1
			idx[p] = i
		}
		if out[i].Title == "" && !reNum.MatchString(t) {
			out[i].Title = t
		}
		if out[i].Art == "" {
			out[i].Art = art
		}
	}
	for i := range out {
		if out[i].Title == "" {
			out[i].Title = strings.ReplaceAll(out[i].ID[strings.LastIndex(out[i].ID, "/")+1:], "-", " ")
		}
	}
	return out, nil
}

func instStream(w http.ResponseWriter, r *http.Request, p string) {
	_, inst := mode()
	cands := []string{inst + "/_/restream" + p}
	if base, err := url.Parse(inst); err == nil {
		if b, err := getBody(inst + p); err == nil { // media URLs declared by the track page win
			var found []string
			for _, m := range reMedia.FindAllStringSubmatch(string(b), -1) {
				if u, err := base.Parse(html.UnescapeString(m[1])); err == nil {
					found = append(found, u.String())
				}
			}
			cands = append(found, cands...)
		}
	}
	for _, c := range cands {
		if pipeAudio(w, r, c, "") {
			return
		}
	}
	http.Error(w, "the instance did not provide a playable stream", 502)
}

// ---- audio proxy ----

type trans struct {
	URL    string `json:"url"`
	Format struct {
		Protocol string `json:"protocol"`
		Mime     string `json:"mime_type"`
	} `json:"format"`
}

func rank(x trans) int {
	p, m := x.Format.Protocol, x.Format.Mime
	switch {
	case x.URL == "" || strings.Contains(p, "encrypted"):
		return 99
	case p == "progressive":
		return 0
	case strings.Contains(m, "mpeg"):
		return 1
	case strings.Contains(m, "ogg") || strings.Contains(m, "opus"):
		return 2
	}
	return 3
}

func resolveTrans(x trans, auth string) string {
	id, _ := clientID(false)
	sep := "?"
	if strings.Contains(x.URL, "?") {
		sep = "&"
	}
	rb, err := getBody(x.URL + sep + "client_id=" + id + "&track_authorization=" + url.QueryEscape(auth))
	if err != nil {
		return ""
	}
	var res struct {
		URL string `json:"url"`
	}
	json.Unmarshal(rb, &res)
	return res.URL
}

func stream(w http.ResponseWriter, r *http.Request) {
	if p := r.URL.Query().Get("p"); p != "" {
		instStream(w, r, p)
		return
	}
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
	for rk := 0; rk < 4; rk++ { // progressive mp3, HLS mp3, HLS opus, anything else; skip DRM
		for _, x := range t.Media.T {
			if rank(x) != rk {
				continue
			}
			if u := resolveTrans(x, t.Auth); u != "" && pipeAudio(w, r, u, x.Format.Mime) {
				return
			}
		}
	}
	http.Error(w, "no playable stream", 404)
}

// pipeAudio proxies u (keeping Range for seeking); HLS playlists are stitched into one stream.
func pipeAudio(w http.ResponseWriter, r *http.Request, u, mime string) bool {
	hls := strings.Contains(strings.Split(u, "?")[0], ".m3u8")
	hdr := map[string]string{}
	if rg := r.Header.Get("Range"); rg != "" && !hls {
		hdr["Range"] = rg
	}
	resp, err := get(r, u, hdr)
	if err != nil {
		return false
	}
	defer resp.Body.Close()
	ct := resp.Header.Get("Content-Type")
	if resp.StatusCode >= 400 || strings.Contains(ct, "text/html") {
		return false
	}
	if hls || strings.Contains(ct, "mpegurl") {
		pl, _ := io.ReadAll(resp.Body)
		hlsStitch(w, r, u, string(pl), mime)
		return true
	}
	for _, h := range []string{"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"} {
		if v := resp.Header.Get(h); v != "" {
			w.Header().Set(h, v)
		}
	}
	w.WriteHeader(resp.StatusCode)
	io.Copy(w, resp.Body)
	return true
}

func hlsStitch(w http.ResponseWriter, r *http.Request, base, pl, mime string) {
	bu, _ := url.Parse(base)
	if mime == "" {
		mime = "audio/mpeg"
	}
	w.Header().Set("Content-Type", strings.Split(mime, ";")[0])
	fl, _ := w.(http.Flusher)
	send := func(ref string) bool {
		u, err := bu.Parse(ref)
		if err != nil {
			return true
		}
		sr, err := get(r, u.String(), nil)
		if err != nil {
			return false
		}
		defer sr.Body.Close()
		_, err = io.Copy(w, sr.Body)
		if fl != nil {
			fl.Flush()
		}
		return err == nil
	}
	for _, ln := range strings.Split(pl, "\n") {
		ln = strings.TrimSpace(ln)
		switch {
		case strings.HasPrefix(ln, "#EXT-X-MAP:"): // fMP4 init segment
			if mm := reMap.FindStringSubmatch(ln); mm != nil && !send(mm[1]) {
				return
			}
		case ln == "" || strings.HasPrefix(ln, "#"):
		case strings.Contains(strings.Split(ln, "?")[0], ".m3u8"): // master playlist: follow first variant
			u, _ := bu.Parse(ln)
			sr, err := get(r, u.String(), nil)
			if err != nil {
				return
			}
			b, _ := io.ReadAll(sr.Body)
			sr.Body.Close()
			hlsStitch(w, r, u.String(), string(b), mime)
			return
		default:
			if !send(ln) {
				return
			}
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
				c.ClientID = cfg.ClientID
				if c.Instance != "" && !strings.HasPrefix(c.Instance, "http") {
					c.Instance = "https://" + c.Instance
				}
				c.Instance = strings.TrimRight(c.Instance, "/")
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
			out, err = scrapeAny(inst, kind, "/search/"+t+"?q="+url.QueryEscape(q))
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
			out, err := scrapeAny(inst, "track", "/soundcloud/tracks", "/search/tracks?q=trending")
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
			out, err := scrapeAny(inst, "track", id+"/tracks", id)
			reply(w, out, err)
			return
		}
		b, err := scGet("/users/"+url.PathEscape(id)+"/tracks", url.Values{"limit": {"50"}})
		reply(w, items(coll(b)), err)
	})

	mux.HandleFunc("/api/playlist", func(w http.ResponseWriter, r *http.Request) {
		id := r.URL.Query().Get("id")
		if m, inst := mode(); m == "instance" {
			out, err := scrapeAny(inst, "track", id)
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
