#!/usr/bin/env python3
"""Exercise the actual SMB publication helper with old-FAT syscall failures.

Linux linker wrappers inject errors; all writes stay in a temporary directory.
No SMB server, device, mounted card or privileged filesystem setup is needed.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ui = Path(__file__).resolve().parents[2]
driver = r'''
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "discshelf/smb_publish.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static const char *mode;
static int directory, destination = -1, reads, writes, syncs, spaces, cancels, changed;
static size_t copied;
static uint64_t requested_space;
static unsigned char payload[131077];

long __real_syscall(long, ...);
int __real_linkat(int, const char *, int, const char *, int);
int __real_openat(int, const char *, int, ...);
ssize_t __real_read(int, void *, size_t);
ssize_t __real_write(int, const void *, size_t);
int __real_fsync(int);

static int is(const char *value) { return !strcmp(mode, value); }
static void foreign(void) {
    int f = __real_openat(directory, "final", O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(f >= 0);
    assert(__real_write(f, "KEEP", 4) == 4);
    assert(!close(f));
}
long __wrap_syscall(long number, ...) {
    assert(number == SYS_renameat2);
    va_list ap; va_start(ap, number);
    int olddir = va_arg(ap, int); const char *oldname = va_arg(ap, const char *);
    int newdir = va_arg(ap, int); const char *newname = va_arg(ap, const char *);
    unsigned int flags = va_arg(ap, unsigned int); va_end(ap);
    if (is("native") || is("native-collision"))
        return __real_syscall(number, olddir, oldname, newdir, newname, flags);
    errno = is("rename-io") ? EIO : is("enosys") ? ENOSYS : EINVAL;
    return -1;
}
int __wrap_linkat(int olddir, const char *oldname, int newdir,
                  const char *newname, int flags) {
    if (is("link")) return __real_linkat(olddir, oldname, newdir, newname, flags);
    errno = is("link-io") ? EIO : is("notsup") ? EOPNOTSUPP : EPERM;
    return -1;
}
int __wrap_openat(int dir, const char *name, int flags, ...) {
    mode_t permissions = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); permissions = va_arg(ap, int); va_end(ap);
    }
    if (!strcmp(name, "final") && (flags & O_CREAT)) {
        assert((flags & (O_EXCL | O_NOFOLLOW)) == (O_EXCL | O_NOFOLLOW));
        if (is("create-race")) foreign();
        destination = __real_openat(dir, name, flags, permissions);
        return destination;
    }
    return __real_openat(dir, name, flags, permissions);
}
ssize_t __wrap_read(int fd, void *buf, size_t count) {
    reads++;
    if (is("read-io") && reads == 3) { errno = EIO; return -1; }
    if (is("early-eof") && reads == 3) return 0;
    if (is("short") && reads == 1) { errno = EINTR; return -1; }
    if (is("short") && count > 17) count = 17;
    return __real_read(fd, buf, count);
}
ssize_t __wrap_write(int fd, const void *buf, size_t count) {
    assert(fd == destination); writes++;
    if (is("write-io") && writes == 3) { errno = ENOSPC; return -1; }
    if (is("write-zero") && writes == 3) return 0;
    if (is("short") && writes == 1) { errno = EINTR; return -1; }
    if (is("short") && count > 3) count = 3;
    if (is("replace-race") && writes == 3) {
        assert(!unlinkat(directory, "final", 0)); foreign();
        errno = EIO; return -1;
    }
    if (is("source-change") && writes == 3) {
        int source = __real_openat(directory, "source.part", O_WRONLY);
        assert(source >= 0); payload[0] ^= 0xff;
        assert(__real_write(source, payload, 1) == 1);
        const struct timespec stamp[2] = {{1, 0}, {1, 0}};
        assert(!futimens(source, stamp)); assert(!close(source));
    }
    ssize_t n = __real_write(fd, buf, count);
    if (n > 0) copied += n;
    return n;
}
int __wrap_fsync(int fd) {
    assert(fd == destination); syncs++;
    if (is("sync-io")) { errno = EIO; return -1; }
    if (is("short") && syncs == 1) { errno = EINTR; return -1; }
    return __real_fsync(fd);
}
static int cancelled(void) {
    cancels++;
    /* Simulate ordinary external mutation after all I/O has succeeded: the
     * callback returns false, so only final publication validation catches it. */
    if (!changed && syncs > 0 && is("success-replace-race")) {
        changed = 1; assert(!unlinkat(directory, "final", 0)); foreign();
    }
    if (!changed && syncs > 0 && is("success-truncate")) {
        changed = 1; assert(!ftruncate(destination, 1));
    }
    return is("cancel-before") || (is("cancel") && copied >= 514) ||
           (is("cancel-synced") && syncs > 0);
}
static int space(uint64_t extra) {
    spaces++; requested_space = extra;
    return !is("space");
}
static void exact(const char *name, const unsigned char *bytes, size_t size) {
    int fd = __real_openat(directory, name, O_RDONLY | O_NOFOLLOW);
    assert(fd >= 0);
    struct stat st; assert(!fstat(fd, &st)); assert(st.st_size == (off_t)size);
    unsigned char buffer[1024]; size_t offset = 0;
    while (offset < size) {
        ssize_t n = __real_read(fd, buffer, sizeof buffer);
        assert(n > 0 && offset + (size_t)n <= size);
        assert(!memcmp(buffer, bytes + offset, n)); offset += n;
    }
    assert(!close(fd));
}
int main(int argc, char **argv) {
    assert(argc == 3); mode = argv[1];
    directory = open(argv[2], O_RDONLY | O_DIRECTORY); assert(directory >= 0);
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (i * 7) % 251;
    size_t size = is("empty") ? 0 : sizeof payload;
    int fd = __real_openat(directory, "source.part", O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0); assert(__real_write(fd, payload, size) == (ssize_t)size);
    assert(!__real_fsync(fd)); assert(!close(fd));
    if (is("collision") || is("native-collision")) foreign();
    if (is("symlink")) assert(!symlinkat("source.part", directory, "final"));
    if (is("directory")) assert(!mkdirat(directory, "final", 0700));
    unsigned char bounded[259]; memset(bounded, 0xa5, sizeof bounded);
    int ok = ds_smb_publish_new(directory, "source.part", "final", bounded + 1,
                                257, cancelled, space);
    int error = errno;
    assert(bounded[0] == 0xa5 && bounded[258] == 0xa5);
    int success = is("native") || is("link") || is("fallback") || is("short") ||
                  is("empty") || is("enosys") || is("notsup");
    assert(ok == success);
    if (success) {
        exact("final", payload, size);
        if (!is("native") && !is("link")) {
            assert(spaces == 1 && requested_space == size);
            assert(syncs > 0 && copied == size);
        } else assert(!spaces && !reads && !writes);
    } else {
        int expected = (is("collision") || is("native-collision") || is("create-race") ||
                        is("symlink") || is("directory")) ? EEXIST :
                       (is("space") || is("write-io")) ? ENOSPC :
                       (is("cancel") || is("cancel-before") || is("cancel-synced")) ? ECANCELED : EIO;
        assert(error == expected);
        if (is("collision") || is("native-collision") || is("create-race") ||
            is("replace-race") || is("success-replace-race"))
            exact("final", (const unsigned char *)"KEEP", 4);
        else if (is("symlink")) {
            struct stat st; assert(!fstatat(directory, "final", &st, AT_SYMLINK_NOFOLLOW));
            assert(S_ISLNK(st.st_mode));
        } else if (is("directory")) {
            struct stat st; assert(!fstatat(directory, "final", &st, AT_SYMLINK_NOFOLLOW));
            assert(S_ISDIR(st.st_mode));
        } else {
            struct stat st; assert(fstatat(directory, "final", &st, AT_SYMLINK_NOFOLLOW));
            assert(errno == ENOENT);
        }
        if (is("space")) assert(spaces == 1 && requested_space == size && destination < 0);
    }
    if (!is("native")) exact("source.part", payload, size);
    assert(!close(directory));
    printf("PASS %s: result=%d errno=%d copied=%zu\n", mode, ok, error, copied);
    return 0;
}
'''

cases = ["native", "link", "fallback", "enosys", "notsup", "empty", "short",
         "native-collision", "collision", "create-race", "replace-race",
         "symlink", "directory", "space", "write-io", "write-zero", "read-io",
         "early-eof", "sync-io", "cancel-before", "cancel", "cancel-synced",
         "rename-io", "link-io", "success-replace-race", "success-truncate",
         "source-change"]
with tempfile.TemporaryDirectory(prefix="shelf-publish-") as temporary:
    root = Path(temporary)
    source = root / "driver.c"
    source.write_text(driver)
    exe = root / "test"
    subprocess.run(["cc", "-g", "-O1", "-Wall", "-Wextra", "-Werror", "-D_GNU_SOURCE",
                    "-fsanitize=address,undefined", "-I" + str(ui), str(source),
                    str(ui / "discshelf/smb_publish.c"),
                    *["-Wl,--wrap=" + name for name in
                      ("syscall", "linkat", "openat", "read", "write", "fsync")],
                    "-o", str(exe)], check=True)
    for case in cases:
        directory = root / case
        directory.mkdir()
        subprocess.run([str(exe), case, str(directory)],
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"},
                       check=True, timeout=10)
print("PASS SMB publication: old-FAT fallback, bounded copy, races, failures and cancellation")
