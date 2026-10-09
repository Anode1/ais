/* win.h -- platform shims. On native Windows (MinGW-w64) they let the ANSI C
 * engine build into a self-contained ais.exe with NO cygwin1.dll; on POSIX only
 * seven pass-through macros remain (SOCK_*, ais_tmpfile, AIS_WRITER_TAG,
 * AIS_SO_REUSE). Safe to include anywhere. Each shim is only
 * the subset AIS actually uses, not a general implementation:
 *   - Winsock init (ais_net_init) for serve.c and sync.c
 *   - poll(2)   -> WSAPoll            (sync.c's accept timeout; connect uses select)
 *   - flock(2)  -> LockFileEx         (store.c index lock)
 *   - mkdir(p,mode) -> _mkdir(p)      (mode ignored on Windows)
 *   - rename(2) -> MoveFileEx         (POSIX replace-existing; MSVCRT rename fails)
 *   - lstat -> stat                   (no POSIX symlinks on Windows)
 *   - fsync -> _commit                (sync.c's atomic bundle write)
 *   - realpath -> _fullpath           (main.c / mcp.c: is -f the home index?)
 *   - ais_tmpfile: a temp FILE in the user's temp dir, deleted on close.
 *     MSVCRT's tmpfile() opens it in the drive root, which a user cannot write,
 *     so every temp FILE in the engine goes through it (tmpfile() on POSIX).
 *   - AIS_WRITER_TAG: a number two concurrent writers of one index never
 *     share, for temp names: the thread id (the Host is a thread) on Windows,
 *     the pid (it is a child) on POSIX.
 *   - AIS_SO_REUSE: SO_EXCLUSIVEADDRUSE. Winsock's SO_REUSEADDR lets a second
 *     socket bind a port another process is listening on, so "port busy"
 *     would never be reported.
 * The MinGW build is cross-compiled from Linux; see native-windows.yml. */
#ifndef AIS_WIN_H
#define AIS_WIN_H

#include <stdio.h>

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600     /* Vista and later: WSAPoll, inet_pton, inet_ntop */
#endif
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

/* rename(2): POSIX atomically replaces an existing destination; MSVCRT rename
 * fails if the target exists. idx/off updates write a .tmp then rename over the
 * live file (post.c detach, compact.c). MoveFileEx restores replace-existing. */
int ais_rename(const char *from, const char *to);
#ifdef rename
#undef rename
#endif
#define rename(from, to) ais_rename((from), (to))

/* poll(2) subset: WSAPoll takes the same pollfd array and POLLIN. */
#define poll(fds, n, ms) WSAPoll((fds), (n), (ms))

/* fsync(2): flush an open file's OS buffers to the disk. */
#define fsync(fd) _commit(fd)

/* realpath(3): RESOLVED must hold AIS_PATH_MAX bytes, as the callers' do. */
char *ais_realpath(const char *path, char *resolved);
#define realpath(path, resolved) ais_realpath((path), (resolved))

/* A read/write temp FILE that disappears when closed. */
FILE *ais_tmpfile(void);

#define AIS_WRITER_TAG() ((long)GetCurrentThreadId())
#define AIS_SO_REUSE SO_EXCLUSIVEADDRUSE
#define SOCK_INTR()  (WSAGetLastError() == WSAEINTR)

/* Initialise Winsock once (WSAStartup); no-op after the first call. */
void ais_net_init(void);

/* 1 when one of the standard streams is a real console, so a person can answer
 * a prompt there. CONIN$ opens on any attached console, including the unattended
 * one a CI job runs under, and a read on that waits forever. */
int ais_console_present(void);

/* socket I/O is recv()/send()/closesocket() here: a SOCKET is not a file
 * descriptor, and read()/close() on one corrupt the CRT's fd table. */
#define SOCK_READ(fd, b, n)  recv((SOCKET)(fd), (char *)(b), (int)(n), 0)
#define SOCK_WRITE(fd, b, n) send((SOCKET)(fd), (const char *)(b), (int)(n), 0)
#define SOCK_CLOSE(fd)       closesocket((SOCKET)(fd))

#else  /* POSIX: a socket is a file descriptor */

#define SOCK_READ(fd, b, n)  read((fd), (b), (n))
#define SOCK_WRITE(fd, b, n) write((fd), (b), (n))
#define SOCK_CLOSE(fd)       close((fd))
#define SOCK_INTR()          (errno == EINTR)
#define ais_tmpfile()        tmpfile()
#define AIS_WRITER_TAG()     ((long)getpid())
#define AIS_SO_REUSE         SO_REUSEADDR

#endif /* _WIN32 */
#endif /* AIS_WIN_H */
