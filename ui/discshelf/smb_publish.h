/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DISCSHELF_SMB_PUBLISH_H
#define DISCSHELF_SMB_PUBLISH_H
#include <stddef.h>
#include <stdint.h>

/* Publish a validated, synced temporary file without replacing any existing
 * destination. Returns 1 on success, 0 with errno on failure. The caller owns
 * both the directory fd and buffer, and removes any remaining temporary file.
 * The caller must sync the directory after success.
 *
 * Old FAT/exFAT drivers support neither renameat2(NOREPLACE) nor hard links.
 * Their fallback exclusively creates the destination and copies into its fd;
 * it needs additional space equal to the temporary file's size. This preserves
 * no-overwrite safety, but the new name is visible while copying. Call under
 * the SD lease with cooperating workers: cancellation/I/O failure removes only
 * the entry still matching our created inode. A power loss can leave an
 * incomplete, unowned destination, which must not be marked as downloaded.
 */
int ds_smb_publish_new(int dir, const char *tmp, const char *name,
                       void *buffer, size_t capacity,
                       int (*cancelled)(void), int (*space)(uint64_t));
#endif
