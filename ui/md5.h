#ifndef MD5_H
#define MD5_H
#include <stddef.h>
/* Compute the MD5 of `len` bytes at `data`, writing a 32-char lowercase hex
 * digest + NUL terminator into `out` (must be >= 33 bytes). */
void md5_hex(const void *data, size_t len, char *out);
#endif
