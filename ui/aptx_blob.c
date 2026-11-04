/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The aptX BlueALSA files carried inside mq_ui (ui/vendor/aptx, see its README.md): BlueALSA 4.0.0 (MIT) and
 * libfreeaptx 0.1.1 (LGPL-2.1, kept a separate shared library), with their license texts. bt.c writes them to
 * /usr/data/diskos-aptx/ when Settings > aptX (experimental) is on. Plain bytes in .rodata, never run from here. */
#include "aptx_blob.h"

#ifndef APTX_DIR
#define APTX_DIR "vendor/aptx"
#endif
#define APTX_INCBIN(sym, file) \
    __asm__(".section .rodata\n.balign 4\n.global " #sym "_start\n" #sym "_start:\n" \
            ".incbin \"" APTX_DIR "/" file "\"\n.global " #sym "_end\n" #sym "_end:\n.previous\n")
APTX_INCBIN(aptx_bluealsa, "bluealsa");
APTX_INCBIN(aptx_libfreeaptx, "libfreeaptx.so.0");
APTX_INCBIN(aptx_lic_bluealsa, "LICENSE-bluez-alsa.txt");
APTX_INCBIN(aptx_lic_libfreeaptx, "LICENSE-libfreeaptx.txt");

extern const unsigned char aptx_bluealsa_start[], aptx_bluealsa_end[], aptx_libfreeaptx_start[], aptx_libfreeaptx_end[],
    aptx_lic_bluealsa_start[], aptx_lic_bluealsa_end[], aptx_lic_libfreeaptx_start[], aptx_lic_libfreeaptx_end[];

const aptx_file_t APTX_FILES[APTX_NFILES] = {
    { "libfreeaptx.so.0",        aptx_libfreeaptx_start,     0755 },   /* the library first: bluealsa needs it */
    { "bluealsa",                aptx_bluealsa_start,        0755 },
    { "LICENSE-bluez-alsa.txt",  aptx_lic_bluealsa_start,    0644 },
    { "LICENSE-libfreeaptx.txt", aptx_lic_libfreeaptx_start, 0644 },
};
size_t aptx_file_size(int i){
    static const unsigned char *const end[APTX_NFILES] = { aptx_libfreeaptx_end, aptx_bluealsa_end,
                                                           aptx_lic_bluealsa_end, aptx_lic_libfreeaptx_end };
    return (size_t)(end[i] - APTX_FILES[i].data);
}
