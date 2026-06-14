/* locate.c -- choose the index directory (front-end policy). See locate.h.
 *
 * Precedence (git-like): -f DIR  >  $AIS_INDEX  >  nearest .ais/ above CWD  >
 * the per-user index ($XDG_DATA_HOME/ais, else $HOME/.local/share/ais), whose
 * parents are created so ais_open can make the final dir. No implicit ./INDEX.
 */
#define _DEFAULT_SOURCE      /* getcwd, mkdir */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "locate.h"
#include "win.h"          /* mkdir shim on native Windows; empty on POSIX */

/* Copy SRC into OUT (size OUTSZ). Returns 0, or -1 if it would not fit. */
static int put_str(char *out, size_t outsz, const char *src)
{
    int n = snprintf(out, outsz, "%s", src);
    return (n >= 0 && (size_t)n < outsz) ? 0 : -1;
}

/* Is PATH an existing directory? */
static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* mkdir -p PATH: make each leading component; existing dirs are fine. 0/-1. */
static int mkdir_p(const char *path)
{
    char buf[AIS_PATH_MAX];
    size_t i, n = strlen(path);

    if (n == 0 || n >= sizeof(buf))
        return -1;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST)
                return -1;
            buf[i] = c;
        }
    }
    return 0;
}

/* Walk up from CWD looking for a ".ais" directory. Writes its path into OUT and
 * returns 1 if found, 0 if none up to the root, -1 on error. */
static int find_local(char *out, size_t outsz)
{
    char dir[AIS_PATH_MAX];
    char cand[AIS_PATH_MAX];

    if (getcwd(dir, sizeof(dir)) == NULL)
        return -1;

    for (;;) {
        char *slash;
        int n = snprintf(cand, sizeof(cand), "%s/.ais", dir);
        if (n < 0 || (size_t)n >= sizeof(cand))
            return -1;
        if (is_dir(cand))
            return (put_str(out, outsz, cand) == 0) ? 1 : -1;
        if (dir[0] == '/' && dir[1] == '\0')
            return 0;                 /* reached the filesystem root */
        slash = strrchr(dir, '/');
        if (slash == NULL)
            return 0;
        if (slash == dir)
            dir[1] = '\0';            /* parent is "/" */
        else
            *slash = '\0';
    }
}

int ais_locate(const char *opt, char *out, size_t outsz)
{
    const char *env;
    int rc;

    if (opt != NULL && opt[0] != '\0')          /* 1. -f */
        return put_str(out, outsz, opt);

    env = getenv("AIS_INDEX");                   /* 2. $AIS_INDEX */
    if (env != NULL && env[0] != '\0')
        return put_str(out, outsz, env);

    rc = find_local(out, outsz);                 /* 3. nearest .ais/ */
    if (rc < 0)
        return -1;
    if (rc == 1)
        return 0;

    {                                            /* 4. per-user global index */
        char base[AIS_PATH_MAX];
        int n;
#ifdef _WIN32
        /* the per-user local app-data dir via the shell API -- no env vars
         * (HOME/XDG are not set on native Windows): %LOCALAPPDATA%\ais */
        if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base) != S_OK)
            return -1;
#else
        {
            const char *xdg = getenv("XDG_DATA_HOME");
            const char *home = getenv("HOME");

            if (xdg != NULL && xdg[0] != '\0')
                n = snprintf(base, sizeof(base), "%s", xdg);
            else if (home != NULL && home[0] != '\0')
                n = snprintf(base, sizeof(base), "%s/.local/share", home);
            else
                return -1;                       /* no XDG, no HOME */
            if (n < 0 || (size_t)n >= sizeof(base))
                return -1;
        }
#endif
        if (mkdir_p(base) != 0)
            return -1;
        n = snprintf(out, outsz, "%s/ais", base);
        return (n >= 0 && (size_t)n < outsz) ? 0 : -1;
    }
}
