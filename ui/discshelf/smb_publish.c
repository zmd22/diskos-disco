/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "smb_publish.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int interrupted(int (*cancelled)(void)) {
    if (cancelled && cancelled()) { errno = ECANCELED; return 1; }
    return 0;
}

static int unsupported(int error) {
    return error == ENOSYS || error == EINVAL || error == EOPNOTSUPP;
}

static int leaf(const char *name) {
    return name && name[0] && strcmp(name, ".") && strcmp(name, "..") &&
           !strchr(name, '/');
}

/* The SD lease prevents unmount/export and DiscShelf has one writer. The
 * identity check also avoids deleting a name replaced by another cooperating
 * writer; it is not a namespace lock against arbitrary external mutation. */
static void remove_owned(int dir, const char *name, const struct stat *created) {
    struct stat current;
    if (!fstatat(dir, name, &current, AT_SYMLINK_NOFOLLOW) &&
        S_ISREG(current.st_mode) && current.st_dev == created->st_dev &&
        current.st_ino == created->st_ino)
        unlinkat(dir, name, 0);
}

int ds_smb_publish_new(int dir, const char *tmp, const char *name,
                       void *buffer, size_t capacity,
                       int (*cancelled)(void), int (*space)(uint64_t)) {
    if (!leaf(tmp) || !leaf(name) || !strcmp(tmp, name) || !buffer ||
        !capacity || capacity > (size_t)SSIZE_MAX) { errno = EINVAL; return 0; }
    if (interrupted(cancelled)) return 0;
#ifdef SYS_renameat2
    if (!syscall(SYS_renameat2, dir, tmp, dir, name, 1u)) return 1;
    if (!unsupported(errno)) return 0;
#endif
    if (!linkat(dir, tmp, dir, name, 0)) return 1;
    if (!unsupported(errno) && errno != EPERM) return 0;

    int source = openat(dir, tmp, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source < 0) return 0;
    int dest = -1, have_identity = 0, ok = 0, saved_error;
    struct stat before, after, created;
    if (fstat(source, &before)) goto finish;
    if (!S_ISREG(before.st_mode) || before.st_size < 0) { errno = EINVAL; goto finish; }
    if (interrupted(cancelled)) goto finish;
    if (space && !space((uint64_t)before.st_size)) { errno = ENOSPC; goto finish; }
    dest = openat(dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (dest < 0) goto finish;
    if (fstat(dest, &created)) goto finish;
    have_identity = 1;
    off_t remaining = before.st_size;
    while (remaining > 0) {
        if (interrupted(cancelled)) goto finish;
        size_t count = (uint64_t)remaining < capacity ? (size_t)remaining : capacity;
        ssize_t got = read(source, buffer, count);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { if (!got) errno = EIO; goto finish; }
        size_t at = 0;
        while (at < (size_t)got) {
            if (interrupted(cancelled)) goto finish;
            ssize_t written = write(dest, (char *)buffer + at, (size_t)got - at);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { if (!written) errno = EIO; goto finish; }
            at += (size_t)written;
        }
        remaining -= got;
    }
    if (fstat(source, &after)) goto finish;
    if (after.st_size != before.st_size ||
        after.st_mtim.tv_sec != before.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != before.st_mtim.tv_nsec ||
        after.st_ctim.tv_sec != before.st_ctim.tv_sec ||
        after.st_ctim.tv_nsec != before.st_ctim.tv_nsec) { errno = EIO; goto finish; }
    if (interrupted(cancelled)) goto finish;
    while (fsync(dest)) {
        if (errno != EINTR || interrupted(cancelled)) goto finish;
    }
    if (interrupted(cancelled)) goto finish;
    /* Do not register another writer's replacement as our download. Keep our
     * fd open until this check, which also prevents inode-number reuse. */
    if (fstatat(dir, name, &after, AT_SYMLINK_NOFOLLOW)) goto finish;
    if (!S_ISREG(after.st_mode) || after.st_dev != created.st_dev ||
        after.st_ino != created.st_ino || after.st_size != before.st_size) {
        errno = EIO;
        goto finish;
    }
    ok = 1;
finish:
    saved_error = errno;
    if (dest >= 0) {
        /* Keep the fd open through identity checking so its inode cannot be
         * recycled before a failed copy's entry is removed. */
        if (!ok && have_identity) remove_owned(dir, name, &created);
        if (close(dest) && ok) {
            saved_error = errno;
            ok = 0;
            if (have_identity) remove_owned(dir, name, &created);
        }
    }
    if (close(source) && ok) {
        saved_error = errno;
        ok = 0;
        if (have_identity) remove_owned(dir, name, &created);
    }
    if (!ok) errno = saved_error;
    return ok;
}
