/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_VERSION_H
#define DISKOS_VERSION_H
/* The diskOS release this UI belongs to (shown in Settings > About). Bump when a release is cut. */
#define DISKOS_VERSION "1.2.4"            /* internal only (the hidden online-update path compares it); never shown to users */
#define DISCO_NAME     "diskOS Disco!"
#define DISCO_VERSION  "3.6.1"    /* this fork's own release, shown in Settings > About */
/* Private test builds only: shown after the version in About ("Disco! 3.3.1 test5"). MUST be "" for a release. */
#define DISCO_TEST_LABEL ""
#endif
