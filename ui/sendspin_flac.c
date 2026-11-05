/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FLAC for the MA Sendspin player: Music Assistant sends whole FLAC frames per audio chunk (roles/player/v1.md
 * "Codec framing") and the stream header (fLaC + STREAMINFO) once in stream/start. dr_flac (vendor/dr_flac, public
 * domain / MIT-0) decodes each chunk as a tiny in-memory FLAC file: the header, then the chunk's frames. */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#include "vendor/dr_flac/dr_flac.h"
#include <stdlib.h>
#include <string.h>

/* decodes `n` bytes of FLAC frames behind header `hdr`; calls out() with interleaved stereo s16 blocks.
 * Returns frames decoded, -1 if the data isn't 44.1 kHz stereo FLAC or doesn't decode. */
long ssf_decode(const unsigned char *hdr, size_t hn, const unsigned char *data, size_t n,
                void (*out)(void *ctx, const short *pcm, int frames), void *ctx){
    unsigned char *buf = malloc(hn + n); if(!buf) return -1;
    memcpy(buf, hdr, hn); memcpy(buf + hn, data, n);
    drflac *f = drflac_open_memory(buf, hn + n, NULL);
    if(!f){ free(buf); return -1; }
    long total = 0;
    if(f->channels == 2 && f->sampleRate == 44100){
        static drflac_int16 tmp[4096 * 2];
        drflac_uint64 got;
        while((got = drflac_read_pcm_frames_s16(f, 4096, tmp)) > 0){ out(ctx, tmp, (int)got); total += (long)got; }
    } else total = -1;
    drflac_close(f); free(buf);
    return total;
}
