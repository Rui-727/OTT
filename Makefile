CC ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g -fPIC
PREFIX ?= /usr/local
LV2_DIR ?= $(HOME)/.lv2

PLUGIN = ott.lv2

# Probe for pugl + cairo via pkg-config. The UI is optional: if pugl is
# not installed, the plugin .so still builds and works with the host's
# generic parameter sheet.
PUGL_PKGS  := pugl-0 pugl-cairo-0 cairo
UI_CFLAGS  := $(shell pkg-config --cflags $(PUGL_PKGS) 2>/dev/null)
UI_LIBS    := $(shell pkg-config --libs $(PUGL_PKGS) 2>/dev/null)
HAVE_PUGL  := $(shell pkg-config --exists $(PUGL_PKGS) && echo yes || echo no)

ifeq ($(HAVE_PUGL),yes)
ALL_TARGETS := $(PLUGIN)/ott.so $(PLUGIN)/ott_ui.so
else
ALL_TARGETS := $(PLUGIN)/ott.so
$(info WARNING: pugl-0 / pugl-cairo-0 not found via pkg-config; skipping ott_ui.so)
$(info WARNING: install libpugl-dev (or build pugl from git) and cairo to enable the UI)
endif

all: $(ALL_TARGETS)

$(PLUGIN)/ott.so: ott_dsp.o ott_lv2.o
	mkdir -p $(PLUGIN)
	$(CC) $(CFLAGS) -shared -o $@ $^ -lm

$(PLUGIN)/ott_ui.so: ott_ui.o lv2/ui.h
	mkdir -p $(PLUGIN)
	$(CC) $(CFLAGS) $(UI_CFLAGS) -shared -o $@ ott_ui.o \
	    $(UI_LIBS) -lm -Wl,-z,nodelete

ott_dsp.o: ott_dsp.c ott_dsp.h
	$(CC) $(CFLAGS) -c -o $@ $<

ott_lv2.o: ott_lv2.c ott_dsp.h lv2/lv2.h
	$(CC) $(CFLAGS) -c -o $@ $<

ott_ui.o: ott_ui.c lv2/ui.h lv2/lv2.h
	$(CC) $(CFLAGS) $(UI_CFLAGS) -c -o $@ $<

test: test_ott
	./test_ott

test_ott: tests/test_ott.o ott_dsp.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

tests/test_ott.o: tests/test_ott.c ott_dsp.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	mkdir -p $(LV2_DIR)/$(PLUGIN)
	cp $(PLUGIN)/ott.so $(LV2_DIR)/$(PLUGIN)/
	cp manifest.ttl $(LV2_DIR)/$(PLUGIN)/
	cp ott.ttl $(LV2_DIR)/$(PLUGIN)/
	if [ -f $(PLUGIN)/ott_ui.so ]; then cp $(PLUGIN)/ott_ui.so $(LV2_DIR)/$(PLUGIN)/; fi

uninstall:
	rm -rf $(LV2_DIR)/$(PLUGIN)

clean:
	rm -f ott_dsp.o ott_lv2.o ott_ui.o tests/test_ott.o \
	      $(PLUGIN)/ott.so $(PLUGIN)/ott_ui.so test_ott

.PHONY: all test install uninstall clean
