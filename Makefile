CC ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g -fPIC
PREFIX ?= /usr/local
LV2_DIR ?= $(HOME)/.lv2

PLUGIN = ott.lv2

all: $(PLUGIN)/ott.so

$(PLUGIN)/ott.so: ott_dsp.o ott_lv2.o
	mkdir -p $(PLUGIN)
	$(CC) $(CFLAGS) -shared -o $@ $^ -lm

ott_dsp.o: ott_dsp.c ott_dsp.h
	$(CC) $(CFLAGS) -c -o $@ $<

ott_lv2.o: ott_lv2.c ott_dsp.h lv2/lv2.h
	$(CC) $(CFLAGS) -c -o $@ $<

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

uninstall:
	rm -rf $(LV2_DIR)/$(PLUGIN)

clean:
	rm -f ott_dsp.o ott_lv2.o tests/test_ott.o $(PLUGIN)/ott.so test_ott

.PHONY: all test install uninstall clean
