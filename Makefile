CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-format-truncation
CORE     = src/paths.c src/logs.c src/config.c src/monitors.c src/runner.c src/media.c src/main.c

GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk4 2>/dev/null)

all: magicwallpaper

magicwallpaper: $(CORE) src/gui.c
	$(CC) -std=gnu11 $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(GTK_LIBS)

# command-line only build (no GTK needed)
nogui: $(CORE)
	$(CC) -std=gnu11 $(CFLAGS) -DMW_NO_GUI -o magicwallpaper $(CORE)

clean:
	rm -f magicwallpaper

.PHONY: all nogui clean
