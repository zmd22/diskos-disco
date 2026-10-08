/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata.album_roulette,"a"
.balign 16
.global album_roulette_blob_start
.global album_roulette_blob_end
album_roulette_blob_start:
.incbin "bundled/album-roulette"
album_roulette_blob_end:
.section .note.GNU-stack,""
