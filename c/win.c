/* win.c -- native Windows (MinGW-w64) compatibility shims (see win.h). Compiles
 * to an empty translation unit on POSIX, where the real syscalls are used. */
#include "win.h"

#ifdef _WIN32

#include <dirent.h>
#include <fcntl.h>      /* _O_BINARY, _fmode */
#include <stdlib.h>
#include <string.h>

/* AIS's store/idx/off files are LF plain text addressed by EXACT byte offsets
 * (store.c fseek by id*width; compact.c ftell). MinGW defaults new streams to
 * TEXT mode, which inserts CR on write and makes ftell/fseek return opaque
 * cookies -- corrupting that offset arithmetic (recall then finds nothing).
 * Force BINARY mode for every fopen, before main() runs, so the on-disk format
 * stays byte-exact and LF-only, identical to POSIX. (Cygwin was binary already,
 * which is why this never showed there.) */
__attribute__((constructor))
static void ais_force_binary_mode(void) { _fmode = _O_BINARY; }

/* flock(2) -> LockFileEx / UnlockFileEx on the underlying OS handle. */
int ais_flock(int fd, int op)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    OVERLAPPED ov;
    DWORD flags = 0;

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    memset(&ov, 0, sizeof ov);

    if (op & LOCK_UN)
        return UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov) ? 0 : -1;
    if (op & LOCK_EX)
        flags |= LOCKFILE_EXCLUSIVE_LOCK;
    if (op & LOCK_NB)
        flags |= LOCKFILE_FAIL_IMMEDIATELY;
    return LockFileEx(h, flags, 0, MAXDWORD, MAXDWORD, &ov) ? 0 : -1;
}

/* nftw(3) subset: recurse PATH with opendir/readdir (MinGW provides dirent),
 * calling FN(child, &st, FTW_F|FTW_D, &ftw) for each entry. Non-zero FN return
 * stops the walk (as nftw specifies). Only the fields feed_dir() reads are
 * filled. */
static int walk(char *path, size_t len, int level,
                int (*fn)(const char *, const struct stat *, int, struct FTW *))
{
    DIR *d = opendir(path);
    struct dirent *e;
    int rc = 0;

    if (d == NULL)
        return 0;                       /* unreadable dir: skip, like FTW */
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        struct FTW ftw;
        size_t n;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        n = strlen(e->d_name);
        if (len + 1 + n + 1 > PATH_MAX)
            continue;                   /* path too long: skip */
        path[len] = '/';
        memcpy(path + len + 1, e->d_name, n + 1);
        if (stat(path, &st) != 0) {
            path[len] = '\0';
            continue;
        }
        ftw.base = (int)len + 1;
        ftw.level = level;
        if (st.st_mode & S_IFDIR) {
            rc = fn(path, &st, FTW_D, &ftw);
            if (rc == 0)
                rc = walk(path, len + 1 + n, level + 1, fn);
        } else {
            rc = fn(path, &st, FTW_F, &ftw);
        }
        path[len] = '\0';
        if (rc != 0)
            break;
    }
    closedir(d);
    return rc;
}

int nftw(const char *path,
         int (*fn)(const char *, const struct stat *, int, struct FTW *),
         int nopenfd, int flags)
{
    char buf[PATH_MAX];
    struct stat st;
    struct FTW ftw;
    size_t len = strlen(path);

    (void)nopenfd;
    (void)flags;
    if (len >= sizeof buf)
        return -1;
    memcpy(buf, path, len + 1);
    while (len > 1 && buf[len - 1] == '/')   /* trim trailing slash */
        buf[--len] = '\0';
    if (stat(buf, &st) != 0)
        return -1;
    ftw.base = 0;
    ftw.level = 0;
    if (!(st.st_mode & S_IFDIR))
        return fn(buf, &st, FTW_F, &ftw);
    if (fn(buf, &st, FTW_D, &ftw) != 0)
        return 0;
    return walk(buf, len, 1, fn);
}

char *realpath(const char *path, char *resolved)
{
    return _fullpath(resolved, path, PATH_MAX);
}

void ais_net_init(void)
{
    static int done = 0;
    WSADATA wsa;
    if (!done && WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
        done = 1;
}

#else  /* !_WIN32 -- keep this translation unit non-empty for the POSIX build */
typedef int ais_win_translation_unit_not_empty;
#endif
