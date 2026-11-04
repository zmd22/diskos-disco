/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The aptX BlueALSA files embedded in mq_ui (aptx_blob.c). */
#ifndef APTX_BLOB_H
#define APTX_BLOB_H
#include <stddef.h>
typedef struct { const char *name; const unsigned char *data; int mode; } aptx_file_t;
#define APTX_NFILES 4
extern const aptx_file_t APTX_FILES[APTX_NFILES];
size_t aptx_file_size(int i);
#endif
