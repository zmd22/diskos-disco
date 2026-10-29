#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Player track frames (a2) with unescaped double quotes: the stock player sprintf's the song's fields into its JSON
without escaping, so an album tagged "Let's Rock" (quotes included) broke the frame and the previous song stayed on
screen. The frame is rebuilt from the player's fixed template and read like a well-formed one."""
import os, subprocess, tempfile
from pathlib import Path
ui = Path(__file__).resolve().parents[1]; src = (ui / "ipc.c").read_text()
start = src.index("static int tok_eq(")
end = src.index("\n}\n", src.index("static void parse_a2_frame(const char*payload,int len){")) + 3
TEMPLATE = (r'{"song":"{\"id\":%d,\"pos_id\":%d,\"song_name\":\"%s\",\"song_duration_time\":%d,\"song_file_path\":\"%s\",'
            r'\"song_sample_rate\":%d,\"song_encoding_rate\":%d,\"song_bit_rate\":%d,\"song_artist_name\":\"%s\",'
            r'\"song_album_name\":\"%s\",\"song_style_name\":\"%s\",\"song_track\":%d,\"song_channel\":%d,\"is_sacd\":%s,'
            r'\"is_cue\":%s,\"is_dsd\":%s,\"is_m3u\":%s,\"m3u_file_path\":\"%s\"}","love":%s,"playerflag":%d,"state":%d,'
            r'"playing_num":"%s","work_mode":"%s"}')
C = r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "jsmn.h"
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static track_state_t g_state;
int mdb_tag_title_by_path(const char *path, char *out, int cap){ (void)path; (void)out; (void)cap; return 0; }
''' + src[start:end] + r'''
static void frame(const char *title, const char *path, const char *artist, const char *album, int pos){
    char f[4096];
    snprintf(f, sizeof f, TEMPLATE, 7, pos, title, 240000, path, 44100, 900, 16, artist, album, "Rock", 1, 2,
             "false", "false", "false", "false", "", "false", 0, 2, "1/12", "8");
    memset(&g_state, 0, sizeof g_state);
    parse_a2(f, (int)strlen(f));
}
int main(void){
    frame("Shine A Little Light", "/tmp/sdcard/Keys/01.flac", "The Black Keys", "Let's Rock", 3);
    assert(g_state.have_track && !strcmp(g_state.album, "Let's Rock") && g_state.pos_id == 3 && g_state.state == 2);
    puts("PASS a well-formed frame (an apostrophe needs no escaping) reads as before");
    frame("Shine A Little Light", "/tmp/sdcard/Keys/2019 - Let's Rock/01. Shine A Little Light.flac", "The Black Keys", "\"Let's Rock\"", 1);
    assert(g_state.have_track && !strcmp(g_state.album, "\"Let's Rock\"") && !strcmp(g_state.title, "Shine A Little Light"));
    assert(!strcmp(g_state.path, "/tmp/sdcard/Keys/2019 - Let's Rock/01. Shine A Little Light.flac"));
    assert(g_state.pos_id == 1 && g_state.duration_ms == 240000 && g_state.sample_rate == 44100 && g_state.state == 2);
    puts("PASS an album tagged with quotes (\"Let's Rock\") is read: title, album, path, position and state");
    frame("Say \"Hi\" \\ back", "/tmp/sdcard/a \"b\"/c.flac", "The \"Band\"", "x", 5);
    assert(!strcmp(g_state.title, "Say \"Hi\" \\ back") && !strcmp(g_state.artist, "The \"Band\"") && !strcmp(g_state.path, "/tmp/sdcard/a \"b\"/c.flac"));
    puts("PASS quotes and backslashes in title, artist and path survive exactly");
    memset(&g_state, 0, sizeof g_state);
    const char *junk = "{\"song\":\"{\\\"id\\\":1,\\\"pos_id\\\":\"garbage";
    parse_a2(junk, (int)strlen(junk));
    assert(!g_state.have_track);
    puts("PASS a frame that doesn't follow the template is still dropped, never guessed");
    return 0;
}
'''
TEMPLATE_C = TEMPLATE.replace('\\', '\\\\').replace('"', '\\"')
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "t.c"; exe = Path(d) / "t"
    c.write_text('#define TEMPLATE "' + TEMPLATE_C + '"\n' + C)
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-std=gnu11", "-D_GNU_SOURCE", "-Wall", "-Werror=format", "-g", "-fsanitize=address,undefined",
                    "-I" + str(ui), str(c), "-o", str(exe), "-pthread"], check=True)
    subprocess.run([str(exe)], check=True)
print("COMPLETE a2_quote_test.py")
