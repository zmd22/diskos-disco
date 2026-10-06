/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef COMMAND_SPAWN_H
#define COMMAND_SPAWN_H
#include <sys/types.h>
/* Start argv with null stdin, inherited logs, an empty signal mask and its own
 * process group. Returns a posix_spawn error number, not errno. */
int command_spawn(pid_t *pid, char *const argv[]);
/* The same, with stdout dup'd from out_fd (-1: inherited) and pass_fd handed to the child as fd 3 (-1: none). */
int command_spawn_io(pid_t *pid, char *const argv[], int out_fd, int pass_fd);
/* Worker-thread only: never call from LVGL. Kills timed-out commands and reaps
 * the direct child before returning; stuck kernel I/O keeps this worker busy. */
int command_wait(char *const argv[], int timeout_ms);
#endif
