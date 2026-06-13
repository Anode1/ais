/* win.h -- native Windows (MinGW-w64) compatibility shims, so the ANSI C engine
 * builds into a self-contained ais.exe with NO cygwin1.dll. Empty on POSIX;
 * safe to include anywhere. Each shim is only the subset AIS actually uses, not
 * a general implementation:
 *   - Winsock init (ais_net_init) for serve.c
 *   - flock(2)  -> LockFileEx        (store.c index lock)
 *   - nftw(3)   -> opendir/readdir   (feed.c -R directory walk)
 *   - realpath  -> _fullpath         (feed.c path canonicalisation)
 *   - mkdir(p,mode) -> _mkdir(p)     (mode ignored on Windows)
 * The MinGW build is cross-compiled from Linux CI; see native-windows.yml. */
#ifndef AIS_WIN_H
#define AIS_WIN_H

#ifdef _WIN32

#include <winsock2.h>   /* must precede windows.h */
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* mkdir(path, mode): Windows _mkdir takes no mode. */
#ifdef mkdir
#undef mkdir
#endif
#define mkdir(path, mode) _mkdir(path)

/* flock(2) subset: advisory whole-file locks via LockFileEx. */
#ifndef LOCK_SH
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#endif
int ais_flock(int fd, int op);
#define flock(fd, op) ais_flock((fd), (op))

/* nftw(3) subset used by feed_dir(): a FTW_PHYS walk reporting FTW_F/FTW_D. */
#ifndef FTW_F
#define FTW_F    1
#define FTW_D    2
#define FTW_PHYS 1
#endif
struct FTW { int base; int level; };
int nftw(const char *path,
         int (*fn)(const char *, const struct stat *, int, struct FTW *),
         int nopenfd, int flags);

/* realpath(3): resolve PATH to an absolute path (Windows _fullpath). RESOLVED
 * must point at a buffer of at least PATH_MAX bytes (AIS always passes one). */
char *realpath(const char *path, char *resolved);

/* Initialise Winsock once (WSAStartup); no-op after the first call. */
void ais_net_init(void);

#endif /* _WIN32 */
#endif /* AIS_WIN_H */
