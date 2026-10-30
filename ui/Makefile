# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# diskOS native UI (LVGL 9.2.2) for the FiiO Snowsky Disc.
# Requires a static mipsel-linux-musl cross toolchain and LVGL 9.2.2 (vendored in ./lvgl).
# Override paths for your environment on the command line, in the environment, or in a local
# (gitignored) config.mk, e.g.:
#   make CROSS=/opt/x-tools/mipsel-linux-musl/bin/mipsel-linux-musl-  LVGL=/path/to/lvgl
-include config.mk
APP   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
NUI   ?= $(APP)
LVGL  ?= $(APP)/lvgl
CROSS ?= mipsel-linux-musl-
CC    := $(CROSS)gcc

# Reproducible builds: strip the build folder from __FILE__ (asserts) and debug info, so the binary does not
# change when the tree is moved or built in another place.
REPRO_FLAGS := -ffile-prefix-map=$(APP)/= -ffile-prefix-map=$(LVGL)/=lvgl/
# No fused multiply-add contraction: floating-point results stay exactly the same as the separate multiply and add
# the code is written with (the MIPS32r2 target otherwise uses madd.s/madd.d).
FP_FLAGS := -ffp-contract=off
CFLAGS := -Os -pipe -std=gnu11 -static -pthread -D_GNU_SOURCE -DLV_CONF_INCLUDE_SIMPLE \
  -ffunction-sections -fdata-sections \
  -I$(APP) -I$(NUI) -I$(APP)/vendor/libsmb2/include -Wall -MMD -MP $(REPRO_FLAGS) $(FP_FLAGS)
# Device-test builds only: make clean && make EXTRA_CFLAGS=-DDISKOS_TEST_OUTPUTS (shows SPDIF Output + USB Audio)
CFLAGS += $(EXTRA_CFLAGS)
# --gc-sections drops every provably-unreferenced function/data section at link (unused LVGL fonts,
# widgets, draw backends, sqlite fns) with NO behavior change - only unreachable symbols are removed.
LDFLAGS := -static -pthread -Wl,--gc-sections
LDLIBS  := -lm -lrt

LVGL_SRCS := $(sort $(shell find $(LVGL)/src -name "*.c"))   # sorted: find order depends on the disk, link order must not
APP_SRCS  := $(APP)/font_simple_nunito_28.c $(APP)/font_simple_nunito_32.c $(APP)/font_simple_nunito_38.c  $(APP)/font_simple_icons_36.c  $(APP)/font_simple_icons_80.c  $(APP)/font_simple_nunito_26.c $(APP)/font_simple_nunito_36.c  $(APP)/simple_theme.c  $(APP)/null_theme.c $(APP)/null_menu.c $(APP)/null_transport_icons.c $(APP)/font_null.c $(APP)/font_null_music.c $(APP)/font_null_jersey.c $(APP)/battery.c $(APP)/eqpreset.c $(APP)/eqprofile.c $(APP)/main.c $(APP)/sdio.c $(APP)/font_cjk_extra_16.c $(APP)/playstate.c $(APP)/fb_pan.c $(APP)/ipc.c $(APP)/ui.c $(APP)/art.c $(APP)/screenmgr.c $(APP)/home.c $(APP)/home_shortcuts.c $(APP)/library.c $(APP)/musicdb.c $(APP)/settings.c $(APP)/power.c $(APP)/controls.c $(APP)/config.c $(APP)/anim.c $(APP)/search.c $(APP)/saver.c $(APP)/quicksettings.c $(APP)/songinfo.c $(APP)/npmenus.c $(APP)/eqcustom.c $(APP)/apps.c $(APP)/menu_apps.c $(APP)/appshelf.c $(APP)/sphere_ui.c $(APP)/songsphere/sphere.c $(APP)/songsphere/ssdb.c $(APP)/stats.c $(APP)/kbinput.c $(APP)/playlistview.c $(APP)/folderbrowser.c $(APP)/multipick.c $(APP)/crashlog.c $(APP)/books.c $(APP)/upnext.c $(APP)/datetime.c $(APP)/i18n.c $(APP)/i18n_tables.c $(APP)/theme.c $(APP)/theme_kit.c $(APP)/theme_defs.c $(APP)/font_hifi_ui_24.c $(APP)/theme_kits.c $(APP)/theme_validate.c $(APP)/theme_store.c $(APP)/artcache.c $(APP)/colorpick.c $(APP)/albumwall.c $(APP)/debug_ui.c $(APP)/modes.c $(APP)/ma.c $(APP)/roulette.c $(APP)/roulette_spin.c $(APP)/roulette_backend.c $(APP)/azjump.c $(APP)/npshade.c $(APP)/sendspin.c $(APP)/wifi.c $(APP)/bt.c $(APP)/weather.c $(APP)/lyrics.c $(APP)/netart.c $(APP)/toast.c $(APP)/lastfm.c $(APP)/lastfm_ui.c $(APP)/splash.c $(APP)/md5.c $(APP)/scanner.c $(APP)/system.c $(APP)/ota.c $(APP)/fwcaps.c $(APP)/eqrate.c $(APP)/command_spawn.c $(APP)/ui_instance.c $(APP)/font_icons_28.c $(APP)/font_icons_20.c $(APP)/font_weather16.c $(APP)/font_weather20.c $(APP)/font_intl_14.c $(APP)/font_intl_16.c $(APP)/font_intl_18.c $(APP)/font_intl_20.c $(APP)/font_thai_14.c $(APP)/font_thai_16.c $(APP)/font_thai_18.c $(APP)/font_thai_20.c $(APP)/font_viet_14.c $(APP)/font_viet_16.c $(APP)/font_viet_18.c $(APP)/font_viet_20.c $(APP)/font_kr_16.c $(APP)/font_mono_14.c $(APP)/font_mono_16.c $(APP)/font_mono_18.c $(APP)/font_mono_20.c $(APP)/font_mono_24.c $(APP)/font_mono_bold_12.c $(APP)/font_dot_18.c $(APP)/font_dot_28.c $(APP)/font_stone_clk_64.c $(APP)/font_stone_ui_12.c $(APP)/font_stone_ui_14.c $(APP)/font_stone_ui_16.c $(APP)/font_stone_ui_18.c $(APP)/font_stone_ui_20.c $(APP)/font_stone_ui_24.c $(APP)/font_splash_light_26.c $(APP)/font_splash_bold_26.c $(APP)/font_splash_disco_72.c $(APP)/orbit.c $(APP)/tagfix.c $(APP)/vlist.c $(APP)/fileops.c $(APP)/usage.c $(APP)/queue.c $(APP)/braun.c $(APP)/braun_home.c $(APP)/braun_saver.c $(APP)/disco_saver.c $(APP)/disco.c $(APP)/onboarding.c $(APP)/disco_home.c $(APP)/font_inter_medium_12.c $(APP)/font_inter_medium_14.c $(APP)/font_inter_medium_16.c $(APP)/font_inter_bold_16.c $(APP)/font_inter_bold_18.c $(APP)/font_inter_bold_22.c $(APP)/font_inter_bold_44.c $(APP)/font_inter_medium_20.c $(APP)/songmenu.c $(APP)/curvelist.c $(APP)/imm_record.c $(APP)/artdec_inproc.c $(APP)/font_theme_20.c $(APP)/font_theme_24.c $(APP)/npfont.c $(APP)/font_squash_inter_28.c $(APP)/font_squash_inter_32.c $(APP)/font_squash_inter_36.c $(APP)/font_np_inter_30.c $(APP)/font_np_inter_22.c $(APP)/font_np_nunito_30.c $(APP)/font_np_nunito_22.c
APP_SRCS += $(APP)/discshelf/ui.c $(APP)/discshelf/landing.c $(APP)/discshelf/style.c $(APP)/discshelf/setup.c $(APP)/discshelf/lifecycle.c $(APP)/discshelf/engine.c $(APP)/discshelf/net.c $(APP)/discshelf/json.c $(APP)/discshelf/connection.c $(APP)/discshelf/smb.c
APP_SRCS += $(APP)/braun2.c $(APP)/zune_home.c $(APP)/zune_dial.c $(APP)/zune_thumbs.c $(APP)/zune_saver.c $(APP)/font_zune_light_150.c $(APP)/font_zune_light_18.c $(APP)/font_zune_light_21.c $(APP)/font_zune_light_24.c $(APP)/font_zune_light_28.c $(APP)/font_zune_light_32.c $(APP)/font_zune_light_40.c $(APP)/font_zune_light_46.c $(APP)/font_zune_light_50.c $(APP)/font_zune_light_64.c $(APP)/font_zune_light_72.c $(APP)/font_zune_light_88.c $(APP)/font_zune_light_96.c $(APP)/font_zune_regular_12.c $(APP)/font_zune_regular_13.c $(APP)/font_zune_regular_14.c $(APP)/font_zune_regular_15.c $(APP)/font_zune_regular_16.c $(APP)/font_zune_regular_17.c $(APP)/font_zune_regular_19.c $(APP)/font_zune_semibold_10.c $(APP)/font_zune_semibold_11.c $(APP)/font_zune_semibold_12.c $(APP)/font_zune_semibold_22.c $(APP)/font_zune_semibold_24.c $(APP)/font_zune_bold_14.c
# bundled SQLite amalgamation (compiled in-process; replaces popen sqlite3 CLI)
SQLITE_FLAGS := -Os -pipe -static -ffunction-sections -fdata-sections -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 -DSQLITE_OMIT_DEPRECATED $(REPRO_FLAGS) $(FP_FLAGS)
# Boomshine (MIT, ui/boomshine): the game is built into mq_ui and runs as `mq_ui --boomshine`, a child process that
# owns the screen like an installed app. Its own flags; it uses no LVGL and no theme roles.
BOOM_OBJS := $(APP)/boomshine/main.o $(APP)/boomshine/game.o
BOOM_FLAGS := -O2 -pipe -std=c11 -static -Wall -Wextra -ffunction-sections -fdata-sections -DBOOMSHINE_BUILTIN \
  -Dmain=boomshine_main $(REPRO_FLAGS) $(FP_FLAGS)
OBJS := $(LVGL_SRCS:.c=.o) $(APP_SRCS:.c=.o) $(APP)/sqlite3.o $(BOOM_OBJS) $(APP)/vendor/smb-build/libsmb2.a

# Every on-screen colour is a theme role (theme.h); theme.c is the only place RGB becomes a colour. The link fails if
# any other app source (.c, or any header in the tree) builds a colour itself: an LVGL colour constructor, palette,
# mix/lighten/darken or HSV conversion, a colour macro or struct initialiser, a literal passed to the runtime-colour
# helpers, or a literal B/G/R byte written into a generated image buffer. Fonts: no direct Montserrat/icon font
# (address or copy; LV_FONT_DECLARE lines are fine). Known limits: a colour laundered through a macro, a decimal or
# computed-index pixel write, or an unrelated struct field named red/green/blue are not told apart.
.DEFAULT_GOAL := mq_ui   # plain `make` builds the UI (check-theme runs inside it)
THEME_SRCS := $(filter-out $(APP)/theme.c $(APP)/font_%,$(APP_SRCS))
THEME_HDRS  = $(filter-out $(APP)/theme.h $(APP)/sqlite3%,$(shell find $(APP) -name '*.h' -not -path '$(APP)/lvgl/*' -not -path '$(APP)/vendor/*' -not -name 'stb_image.h'))   # stb_image.h: vendored decoder, not UI
THEME_COLOUR_RE := lv_color(32|16)?_(hex|hex3|make|white|black|mix|mix32|16_16_mix|lighten|darken|hsv_to_rgb|from_int|to_hsv|premultiply)[[:space:]]*\(|lv_color(32|16)_t[[:space:]]+[A-Za-z_0-9]+[[:space:]]*=[[:space:]]*\{|\(lv_color(32|16)_t\)[[:space:]]*\{|lv_palette_[a-z]+[[:space:]]*\(|LV_COLOR_MAKE[[:space:]]*\(|theme_(color|accent)_from_(rgb|hsv)[[:space:]]*\([[:space:]]*(0x|[0-9])|\(lv_color_t\)[[:space:]]*\{|lv_color_t[[:space:]]+[A-Za-z_0-9]+[[:space:]]*=[[:space:]]*\{|(\.|->)(red|green|blue)[[:space:]]*=[^=]|\[[0-2]\][[:space:]]*=[[:space:]]*0x[0-9A-Fa-f]{2}
check-theme:
	@if grep -n -E '$(THEME_COLOUR_RE)' $(THEME_SRCS) $(THEME_HDRS); then \
	  echo "check-theme: FAIL - use a theme role (TC(...)) or theme_color_from_rgb() on a runtime value instead"; exit 1; fi
	@if grep -n -E '\b(lv_font_montserrat_[0-9]+|font_icons_[0-9]+)\b' $(THEME_SRCS) $(filter-out $(APP)/lv_conf.h,$(THEME_HDRS)) | grep -v 'LV_FONT_DECLARE'; then \
	  echo "check-theme: FAIL - use a font role (TF(...)) instead"; exit 1; fi

mq_ui: $(OBJS)
	@$(MAKE) --no-print-directory check-theme
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	cp -f $@ $@.debug                 # unstripped twin for crash symbolication (not shipped)
	$(CROSS)strip $@ || true
	@ls -l $@
	@{ echo "artifact: $(notdir $@)"; \
	   echo "md5: $$(md5sum $@ | cut -d' ' -f1)"; \
	   echo "size: $$(stat -c%s $@)"; \
	   echo "built: $$(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
	   echo "cc: $$($(CC) --version | head -1)"; \
	   echo "src_md5: $$(cat $(APP_SRCS) $(APP)/*.h 2>/dev/null | md5sum | cut -d' ' -f1)"; \
	 } > $@.manifest
	@cat $@.manifest

# SongSphere (ui/songsphere): an add-on app, its own program (/usr/data/apps/songsphere/app). It links LVGL, SQLite,
# mq_ui's screen driver (fb_pan.c) and the Inter fonts, and takes its colours from /tmp/diskos-app-env (written by mq_ui),
# so it carries no theme engine. On a PC (make CROSS=) the same program draws pictures (SS_SHOT) and times dragging
# (SS_BENCH) instead of opening the screen.
SS_SRCS := $(APP)/songsphere/app.c $(APP)/songsphere/sphere.c $(APP)/songsphere/ssdb.c $(APP)/songsphere/platform.c \
  $(APP)/fb_pan.c $(APP)/font_inter_medium_12.c $(APP)/font_inter_medium_14.c $(APP)/font_inter_medium_16.c \
  $(APP)/font_inter_medium_20.c $(APP)/font_inter_bold_16.c $(APP)/font_inter_bold_18.c $(APP)/font_inter_bold_22.c
SS_OBJS := $(patsubst $(APP)/%.c,$(APP)/songsphere/build/%.o,$(SS_SRCS))
$(APP)/songsphere/build/%.o: $(APP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O2 -c $< -o $@
songsphere: $(LVGL_SRCS:.c=.o) $(SS_OBJS) $(APP)/sqlite3.o
	$(CC) $(LDFLAGS) -o $(APP)/songsphere/songsphere $^ $(LDLIBS)
	cp -f $(APP)/songsphere/songsphere $(APP)/songsphere/songsphere.debug
	$(CROSS)strip $(APP)/songsphere/songsphere || true
	@ls -l $(APP)/songsphere/songsphere

# Boot-selector probe (installed read-only at /opt/diskos/bin/diskos-bootprobe by the installer).
diskos-bootprobe: $(APP)/tools/diskos_bootprobe.c
	$(CC) -Os -static -Wall -Wextra -Werror $(FP_FLAGS) -o $@ $<
	$(CROSS)strip $@ || true
	@md5sum $@

# Artwork helper (installed read-only at /opt/diskos/bin/diskos-artdec): extracts + decodes covers in its own
# process so no stock ffmpeg is needed (stock V2.57's ffmpeg has no image decoders). stb_image.h is vendored.
diskos-artdec: $(APP)/tools/diskos_artdec.c $(APP)/tools/stb_image.h
	$(CC) -O2 -static -Wall -Wextra -Werror $(REPRO_FLAGS) $(FP_FLAGS) -o $@ $< -lm
	$(CROSS)strip $@ || true
	@md5sum $@

.PHONY: checkpunct
checkpunct:   # app text only; generated font sources intentionally describe Unicode glyphs
	@if grep -rnP '[\x{2013}\x{2014}\x{2018}\x{2019}\x{201C}\x{201D}\x{2026}]' $(filter-out $(APP)/font_%,$(APP_SRCS)) $(APP)/*.h; then \
	  echo "checkpunct: FAIL (AI-tell/tofu punctuation above)"; exit 1; else echo "checkpunct: clean"; fi

$(APP)/sqlite3.o: $(APP)/sqlite3.c
	$(CC) $(SQLITE_FLAGS) -c $< -o $@
$(APP)/boomshine/%.o: $(APP)/boomshine/%.c $(APP)/boomshine/game.h $(APP)/boomshine/fonts.h
	$(CC) $(BOOM_FLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) mq_ui mq_ui.debug mq_ui.manifest ipodos-ui diskos-bootprobe diskos-artdec
-include $(patsubst %.o,%.d,$(filter %.o,$(OBJS)))

# Native render/action diagnostics. Explicitly separate from the production theme/cross-build gates.
# Run with CROSS=; never stage this host executable into a device payload.
HOST_DIR := $(APP)/tests/host_build
HOST_CFG := -DCFG_PATH='"$(HOST_DIR)/diskos.conf"' -DCFG_DIR='"$(HOST_DIR)"' \
            -DLEGACY_CONF='"$(HOST_DIR)/legacy-none"' -DLEGACY_SWIPE='"$(HOST_DIR)/swipe-none"'
$(HOST_DIR):
	mkdir -p $@
$(HOST_DIR)/main.o: $(APP)/main.c | $(HOST_DIR)
	$(CC) $(CFLAGS) -Dmain=diskos_device_main -c $< -o $@
$(HOST_DIR)/theme_kit.o: $(APP)/theme_kit.c | $(HOST_DIR)
	$(CC) $(CFLAGS) -DDISKOS_HOST -c $< -o $@
$(HOST_DIR)/config.o: $(APP)/config.c | $(HOST_DIR)
	$(CC) $(CFLAGS) $(HOST_CFG) -c $< -o $@
$(HOST_DIR)/queue.o: $(APP)/queue.c | $(HOST_DIR)
	$(CC) $(CFLAGS) -DQUEUE_FILE='"$(HOST_DIR)/queue.tsv"' -c $< -o $@
$(HOST_DIR)/diskos-artdec: $(APP)/tools/diskos_artdec.c $(APP)/tools/stb_image.h | $(HOST_DIR)
	$(CC) -O2 -std=gnu11 $< -o $@ -lm
$(HOST_DIR)/art.o: $(APP)/art.c $(HOST_DIR)/diskos-artdec | $(HOST_DIR)
	$(CC) $(CFLAGS) -DARTDEC_PATH='"$(HOST_DIR)/diskos-artdec"' -c $< -o $@
$(HOST_DIR)/host_render.o: $(APP)/tests/host_render.c | $(HOST_DIR)
	$(CC) $(CFLAGS) $(HOST_CFG) -DDISKOS_HOST -c $< -o $@
HOST_OBJS := $(filter-out $(APP)/main.o $(APP)/theme_kit.o $(APP)/config.o $(APP)/art.o $(APP)/queue.o,$(OBJS)) \
             $(HOST_DIR)/main.o $(HOST_DIR)/theme_kit.o $(HOST_DIR)/config.o $(HOST_DIR)/art.o $(HOST_DIR)/queue.o $(HOST_DIR)/host_render.o
host-render: $(HOST_OBJS)
	$(CC) $(LDFLAGS) -o $(HOST_DIR)/host-render $(HOST_OBJS) $(LDLIBS) -lcrypt -Wl,--wrap=lv_obj_set_local_style_prop,--wrap=ssdb_open,--wrap=ui_play_list,--wrap=mdb_song_count,--wrap=ui_seek_to,--wrap=ui_set_volume,--wrap=ui_display_is_off,--wrap=ipc_get_state,--wrap=ui_is_playing,--wrap=ui_transport_command,--wrap=ipc_send_cmd,--wrap=fw_custom_peq_writable,--wrap=fw_custom_peq_curve_writable,--wrap=mdb_get_peq_ex,--wrap=mdb_get_peq_readonly,--wrap=mdb_song_accent,--wrap=art_make_all_ex_gen,--wrap=wifi_radio_live,--wrap=bt_state,--wrap=wifi_toggle,--wrap=bt_toggle,--wrap=artcache_get_thumb,--wrap=artcache_get_cover,--wrap=ui_local_playback_allowed,--wrap=mdb_song_rates
.PHONY: host-render
-include $(HOST_DIR)/*.d

$(APP)/vendor/smb-build/libsmb2.a: $(wildcard $(APP)/vendor/libsmb2/lib/*.c) $(wildcard $(APP)/vendor/libsmb2/include/smb2/*.h) $(APP)/tools/build_smb.py
	python3 $(APP)/tools/build_smb.py --cc '$(CC)' --flags '$(REPRO_FLAGS) $(FP_FLAGS)' --out $(APP)/vendor/smb-build
