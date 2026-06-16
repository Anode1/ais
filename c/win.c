/* win.c -- native Windows (MinGW-w64) compatibility shims (see win.h). Compiles
 * to an empty translation unit on POSIX, where the real syscalls are used. */
#include "win.h"

#ifdef _WIN32

#include <fcntl.h>      /* _O_BINARY, _fmode */
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
