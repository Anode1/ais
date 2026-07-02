/* win.h -- native Windows (MinGW-w64) compatibility shims, so the ANSI C engine
 * builds into a self-contained ais.exe with NO cygwin1.dll. Empty on POSIX;
 * safe to include anywhere. Each shim is only the subset AIS actually uses, not
 * a general implementation:
 *   - Winsock init (ais_net_init) for serve.c
 *   - flock(2)  -> LockFileEx        (store.c index lock)
 *   - mkdir(p,mode) -> _mkdir(p)     (mode ignored on Windows)
 *   - rename(2) -> MoveFileEx        (POSIX replace-existing; MSVCRT rename fails)
 *   - lstat -> stat                  (no POSIX symlinks on Windows)
 * The MinGW build is cross-compiled from Linux CI; see native-windows.yml. */
#ifndef AIS_WIN_H
#define AIS_WIN_H

#ifdef _WIN32

#include <winsock2.h>   /* must precede windows.h */
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>     /* SHGetFolderPath, CSIDL_LOCAL_APPDATA */
#include <direct.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>

/* mkdir(path, mode): Windows _mkdir takes no mode. */
#ifdef mkdir
#undef mkdir
#endif
#define mkdir(path, mode) _mkdir(path)

/* lstat(2): Windows has no POSIX symlinks, so lstat == stat here. */
#ifdef lstat
#undef lstat
#endif
#define lstat(path, buf) stat((path), (buf))

/* flock(2) subset: advisory whole-file locks via LockFileEx. */
#ifndef LOCK_SH
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#endif
int ais_flock(int fd, int op);
#define flock(fd, op) ais_flock((fd), (op))

/* rename(2): POSIX atomically REPLACES an existing destination, but the MSVCRT
 * rename FAILS if the target exists. AIS updates idx/off files by writing a .tmp
 * then renaming over the live file (post.c detach, compact.c), so without this
 * those updates fail on Windows. MoveFileEx restores replace-existing. */
int ais_rename(const char *from, const char *to);
#ifdef rename
#undef rename
#endif
#define rename(from, to) ais_rename((from), (to))

/* Initialise Winsock once (WSAStartup); no-op after the first call. */
void ais_net_init(void);

#endif /* _WIN32 */
#endif /* AIS_WIN_H */
