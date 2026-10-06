/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The artwork decoder (tools/diskos_artdec.c) linked into mq_ui and run in the art worker thread instead of as a
 * separate diskos-artdec process: same decoding, limits and outputs, no fork. See artdec_inproc() there. */
#define ARTDEC_INPROC 1
#include "tools/diskos_artdec.c"
