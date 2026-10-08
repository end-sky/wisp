PREFIX ?= /usr/local
PKGS    = gtk4 gstreamer-1.0 libsoup-3.0 json-glib-1.0
override CFLAGS += -O2 -Wall -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

wisp: main.o config.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

core: # dev convenience: build the Go scraper next to the binary
	cd ../core && go build -ldflags="-s -w" -o ../ui/wisp-core .

install: wisp
	install -Dm755 wisp $(DESTDIR)$(PREFIX)/bin/wisp
	install -Dm644 wisp.desktop $(DESTDIR)$(PREFIX)/share/applications/wisp.desktop

clean:
	rm -f wisp wisp-core *.o
