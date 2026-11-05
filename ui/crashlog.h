/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef CRASHLOG_H
#define CRASHLOG_H
/* A small crash log on the Disc: /usr/data/diskos_crash.log (kept under 64 KB). One line per Disco start, per player
 * restart seen by Disco, and per crash (signal, fault address, pc/ra/sp, thread, uptime), so a crash under heavy use
 * can be traced to a function later, without catching it live over SSH. */
void crashlog_install(void);                      /* once, early in main(): crash handlers + a start line */
void crashlog_note(const char *fmt, ...);         /* an ordinary line (not for use inside a signal handler) */
void crashlog_tick(void);                         /* main loop, every iteration: freeze detector + memory watch */
/* Stall reporter: what the UI was last asked to do (a tap's action name, a menu section), so a hang that never ends
 * - which the main loop can't record itself - is written by a watchdog thread while it is still stuck. */
void crashlog_where(const char *what);            /* main thread: the thing about to run (copied) */
void crashlog_screen(int scr);                    /* main thread: the screen now shown */
#endif
