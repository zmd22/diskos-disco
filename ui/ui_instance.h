/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UI_INSTANCE_H
#define UI_INSTANCE_H
/* Acquire the test UI's lifetime lock. Does not signal another process. */
int ui_instance_acquire(void);
#endif
