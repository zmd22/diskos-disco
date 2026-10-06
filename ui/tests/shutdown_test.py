#!/usr/bin/env python3
"""Check production shutdown routing with fake card/IPC operations; never powers off."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


power = (app / 'power.c').read_text()
main = (app / 'main.c').read_text()
harness = r'''
#include "power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int supported=1, order, shown, saves, hidden, reopened, sync_fail, sends;
static unsigned now;
static char message[128];
int power_supported(void){return supported;}
void ui_shutdown_screen(void){assert(order==0);order=1;shown++;}
void usage_save(void){assert(order==1);order=2;saves++;}
void ui_shutdown_hide(void){hidden++;}
void ui_toast(const char *m){snprintf(message,sizeof message,"%s",m);}
static void real_quiesce_begin(void *c){(void)c;assert(order==2);order=3;}
static int real_drained(void *c){(void)c;return 1;}
static void real_quiesce_end(void *c){(void)c;reopened++;}
static int real_sync_start(void *c){(void)c;assert(order==3);return sync_fail?-1:0;}
static int real_sync_poll(void *c){(void)c;return 1;}
static void real_sync_kill(void *c){(void)c;}
static int real_send(void *c,const char *f){(void)c;(void)f;sends++;return 0;}
static uint32_t real_now(void *c){(void)c;return now;}
'''
harness += '\n' + function(power, 'static void real_toast(')
harness += '\n' + function(power, 'int power_shutdown_request(')
harness += '\n' + function(main, 'void ui_power_off(')
harness += r'''
int main(void){
    supported=0;ui_power_off();assert(shown==0 && saves==0 && power_shutdown_phase()==PW_OFF);
    supported=1;message[0]=0;sync_fail=1;ui_power_off();
    assert(order==3 && shown==1 && saves==1 && message[0]==0);
    assert(power_shutdown_request()<0 && shown==1 && saves==1);
    power_shutdown_tick();assert(power_shutdown_phase()==PW_OFF && hidden==1 && reopened==1);
    assert(strstr(message,"Couldn't shut down"));
    /* The production idle timer calls the same central request. */
    order=0;sync_fail=0;now=100;power_idle_reset();
    assert(!power_idle_tick(now,60,0,0,power_shutdown_request));
    now+=60000;assert(power_idle_tick(now,60,0,0,power_shutdown_request));
    assert(order==3 && shown==2 && saves==2 && power_shutdown_phase()==PW_DRAIN);
    power_shutdown_tick();assert(power_shutdown_phase()==PW_SYNC);
    power_shutdown_tick();assert(power_shutdown_phase()==PW_HANDOFF && sends==1);
    now+=POWER_HANDOFF_MS;power_shutdown_tick();
    assert(power_shutdown_phase()==PW_PROTECTED && hidden==2 && reopened==1);
    assert(strstr(message,"restart the Disc"));
    assert(power_shutdown_request()<0 && shown==2 && saves==2);
    power_shutdown_new_generation();assert(power_shutdown_phase()==PW_OFF && reopened==2);
    puts("PASS: manual/idle screen -> usage save -> card drain; refused requests unchanged; failures hide screen; protected card stays closed");
}
'''
with tempfile.TemporaryDirectory(prefix='diskos-shutdown-') as temp:
    source = Path(temp) / 'shutdown_test.c'
    source.write_text(harness)
    binary = Path(temp) / 'shutdown_test'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-DPOWER_CORE_ONLY', '-ffunction-sections', '-fdata-sections',
                    '-I' + str(app), str(source), str(app / 'power.c'),
                    '-Wl,--gc-sections', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
