# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Off-device host tests for diskOS pure-logic modules (arch#5 harness).
# Native x86-64 build with sanitizers + CFG_* paths pointed at a temp dir. Does NOT touch the
# device build (separate dir, separate Makefile). Run: make -C tests run
CC ?= cc
CFLAGS := -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TDIR := /tmp/diskos_cfgtest
DEFS := -DCFG_PATH='"$(TDIR)/diskos.conf"' -DCFG_DIR='"$(TDIR)"' \
        -DLEGACY_CONF='"$(TDIR)/nolegacy1"' -DLEGACY_SWIPE='"$(TDIR)/nolegacy2"'

config_test: ../config.c config_test.c ../config.h
	$(CC) $(CFLAGS) $(DEFS) -I.. ../config.c config_test.c -o $@

playstate_test: ../playstate.c playstate_test.c ../playstate.h
	$(CC) $(CFLAGS) -I.. ../playstate.c playstate_test.c -o $@

fileops_test: fileops_test.c ../fileops.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -DLV_CONF_INCLUDE_SIMPLE -ffunction-sections -fdata-sections -I.. $< -Wl,--gc-sections -pthread -o $@

apps_shortcut_test: apps_shortcut_test.c ../apps.c ../config.c ../md5.c
	$(CC) $(CFLAGS) $(subst diskos_cfgtest,diskos_shortcuttest,$(DEFS)) -D_GNU_SOURCE -DLV_CONF_INCLUDE_SIMPLE -ffunction-sections -fdata-sections -I.. $< ../config.c ../md5.c -Wl,--gc-sections -o $@

.PHONY: review
review: fileops_test apps_shortcut_test
	./fileops_test
	./apps_shortcut_test

.PHONY: run clean
run: config_test playstate_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test

clean:
	rm -f config_test playstate_test fileops_test apps_shortcut_test
