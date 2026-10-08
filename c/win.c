/* win.c -- native Windows (MinGW-w64) compatibility shims (see win.h). Compiles
 * to an empty translation unit on POSIX, where the real syscalls are used. */
#include "win.h"

#ifdef _WIN32

#include <fcntl.h>      /* _O_BINARY, _fmode */
#include <stdlib.h>     /* _fullpath */
#include <string.h>
#include "common.h"     /* AIS_PATH_MAX: the realpath buffer contract */

/* The store/idx/off files are LF plain text addressed by exact byte offsets
 * (store.c fseek by id*width; compact.c ftell). MinGW defaults new streams to
 * text mode, which inserts CR on write and makes ftell/fseek return opaque
 * cookies, corrupting that arithmetic. Force binary mode for every fopen,
 * before main() runs, so the on-disk format stays byte-exact and LF-only. */
__attribute__((constructor))
static void ais_force_binary_mode(void)
{
    _fmode = _O_BINARY;
    /* The standard streams are already open when this runs, so _fmode does not
     * reach them: --dump and --export to stdout came out CRLF, and a dump
     * imported elsewhere would carry a CR in every value. */
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}

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

/* rename(2) with POSIX replace-existing semantics; atomic on NTFS. Reached via
 * the win.h macro, which #undefs rename so this definition is not remapped. */
int ais_rename(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 0 : -1;
}

char *ais_realpath(const char *path, char *resolved)
{
    return _fullpath(resolved, path, AIS_PATH_MAX);
}

/* GetTempFileName creates the file in the user's temp dir; "D" makes the CRT
 * delete it on the last close, so nothing is left behind on any exit path. */
FILE *ais_tmpfile(void)
{
    char dir[MAX_PATH], name[MAX_PATH];
    DWORD n = GetTempPathA(sizeof dir, dir);

    if (n == 0 || n >= sizeof dir)
        return NULL;
    if (GetTempFileNameA(dir, "ais", 0, name) == 0)
        return NULL;
    return fopen(name, "w+bD");
}

int ais_console_present(void)
{
    static const DWORD which[] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    DWORD mode;
    size_t i;
    for (i = 0; i < sizeof which / sizeof which[0]; i++)
        if (GetConsoleMode(GetStdHandle(which[i]), &mode))
            return 1;
    return 0;
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
