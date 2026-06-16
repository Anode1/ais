/* locate.c -- choose the index directory (front-end policy). See locate.h.
 *
 * Precedence (git-like):
 *   1. -f DIR  (the only explicit override -- AIS reads no env var for this)
 *   2. nearest .ais/ at or above the current dir
 *   3. the saved default in ~/.ais/config  (an `index = PATH` line)
 *   4. the built-in default ~/.ais  (Windows: %USERPROFILE%\.ais)
 * For #4, if ~/.ais does not exist yet but the pre-1.0 location does, the old
 * one is used (with a one-line notice) so existing data is never stranded.
 *
 * Everything lives under one self-evident dir, ~/.ais (the SSH model: ~/.ssh +
 * ~/.ssh/config) -- no env-var indirection, identical on every OS, and the same
 * ".ais" that a local `ais --init` creates.
 */
#define _DEFAULT_SOURCE      /* getcwd, mkdir */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef _WIN32
#include <pwd.h>          /* getpwuid -- the home dir from the OS, no env var */
#include <sys/types.h>
#endif

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

/* The account's home directory itself (no env var). Windows: %USERPROFILE%;
 * unix: getpwuid. 0/-1. */
static int home_base(char *out, size_t outsz)
{
#ifdef _WIN32
    char base[AIS_PATH_MAX];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, base) != S_OK)
        return -1;
    return put_str(out, outsz, base);
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL || pw->pw_dir == NULL || pw->pw_dir[0] == '\0')
        return -1;
    return put_str(out, outsz, pw->pw_dir);
#endif
}

/* Walk up from CWD looking for a ".ais" directory. Writes its path into OUT and
 * returns 1 if found, 0 if none, -1 on error. The walk STOPS at the home
 * directory: ~/.ais is the built-in default (step 4), not a "discovered local"
 * index -- otherwise it would shadow the saved default for anyone working under
 * their home dir (which is almost always). So a local index must be strictly
 * below home; ~/.ais is reached only via ais_locate's default step. */
static int find_local(char *out, size_t outsz)
{
    char dir[AIS_PATH_MAX], home[AIS_PATH_MAX], cand[AIS_PATH_MAX];
    int have_home = (home_base(home, sizeof home) == 0);

    if (getcwd(dir, sizeof(dir)) == NULL)
        return -1;

    for (;;) {
        char *slash;
        int n;
        if (have_home && strcmp(dir, home) == 0)
            return 0;                 /* at home: stop (don't treat ~/.ais as local) */
        n = snprintf(cand, sizeof(cand), "%s/.ais", dir);
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

/* The home AIS dir: ~/.ais. Windows: %USERPROFILE%\.ais (SHGetFolderPath); unix:
 * the account home (getpwuid). No env var either way. 0/-1. */
static int home_ais(char *out, size_t outsz)
{
    char base[AIS_PATH_MAX];
    int n;

    if (home_base(base, sizeof base) != 0)
        return -1;
    n = snprintf(out, outsz, "%s/.ais", base);
    return (n >= 0 && (size_t)n < outsz) ? 0 : -1;
}

/* The pre-1.0 per-user index location (for the one-time migration fallback):
 * $XDG_DATA_HOME/ais or $HOME/.local/share/ais; Windows %LOCALAPPDATA%\ais. */
static int legacy_dir(char *out, size_t outsz)
{
    char base[AIS_PATH_MAX];
    int n;
#ifdef _WIN32
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base) != S_OK)
        return -1;
#else
    {
        struct passwd *pw = getpwuid(getuid());
        if (pw == NULL || pw->pw_dir == NULL || pw->pw_dir[0] == '\0')
            return -1;
        n = snprintf(base, sizeof base, "%s/.local/share", pw->pw_dir);
        if (n < 0 || (size_t)n >= sizeof base)
            return -1;
    }
#endif
    n = snprintf(out, outsz, "%s/ais", base);
    return (n >= 0 && (size_t)n < outsz) ? 0 : -1;
}

/* Read the `index = PATH` line from ~/.ais/config (the saved default index).
 * 1 if set (-> OUT), 0 if no config / no index key, -1 on error. */
int ais_default_get(char *out, size_t outsz)
{
    char cfg[AIS_PATH_MAX], line[AIS_LINE_MAX];
    FILE *f;
    int n;

    if (home_ais(cfg, sizeof cfg) != 0)
        return -1;
    n = (int)strlen(cfg);
    if (n < 0 || snprintf(cfg + n, sizeof(cfg) - (size_t)n, "/config") < 0)
        return -1;

    f = fopen(cfg, "r");
    if (f == NULL)
        return 0;                       /* no config -> no saved default */
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line, *e;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "index", 5) != 0
            || (p[5] != ' ' && p[5] != '\t' && p[5] != '='))
            continue;
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=')
            continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        e = p + strlen(p);
        while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = '\0';
        fclose(f);
        if (p[0] == '\0')
            return 0;                   /* `index =` (empty) -> none */
        return (put_str(out, outsz, p) == 0) ? 1 : -1;
    }
    fclose(f);
    return 0;
}

/* Set (PATH non-empty) or clear (PATH NULL/empty) the saved default index in
 * ~/.ais/config, preserving any other lines. 0/-1. */
int ais_default_set(const char *path)
{
    char dir[AIS_PATH_MAX], cfg[AIS_PATH_MAX], line[AIS_LINE_MAX];
    char keep[AIS_LINE_MAX * 8];        /* other config lines, preserved */
    size_t klen = 0;
    FILE *f;
    int n;

    if (home_ais(dir, sizeof dir) != 0)
        return -1;
    if (mkdir_p(dir) != 0)              /* create ~/.ais if needed */
        return -1;
    n = snprintf(cfg, sizeof cfg, "%s/config", dir);
    if (n < 0 || (size_t)n >= sizeof cfg)
        return -1;

    f = fopen(cfg, "r");               /* keep every line except the index one */
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            const char *p = line;
            size_t ll;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "index", 5) == 0
                && (p[5] == ' ' || p[5] == '\t' || p[5] == '='))
                continue;
            ll = strlen(line);
            if (klen + ll < sizeof keep) {
                memcpy(keep + klen, line, ll);
                klen += ll;
            }
        }
        fclose(f);
    }
    keep[klen] = '\0';

    f = fopen(cfg, "w");
    if (f == NULL)
        return -1;
    if (klen > 0)
        fputs(keep, f);
    if (path != NULL && path[0] != '\0')
        fprintf(f, "index = %s\n", path);
    return (fclose(f) == 0) ? 0 : -1;
}

int ais_locate(const char *opt, char *out, size_t outsz)
{
    int rc;

    if (opt != NULL && opt[0] != '\0')          /* 1. -f (the only override) */
        return put_str(out, outsz, opt);

    rc = find_local(out, outsz);                 /* 2. nearest .ais/ */
    if (rc < 0)
        return -1;
    if (rc == 1)
        return 0;

    rc = ais_default_get(out, outsz);            /* 3. saved default (~/.ais/config) */
    if (rc < 0)
        return -1;
    if (rc == 1)
        return 0;

    {                                            /* 4. built-in default: ~/.ais */
        char def[AIS_PATH_MAX], legacy[AIS_PATH_MAX];
        if (home_ais(def, sizeof def) != 0)
            return -1;
        if (!is_dir(def) && legacy_dir(legacy, sizeof legacy) == 0
            && is_dir(legacy)) {                 /* migration: don't strand old data */
            fprintf(stderr,
                    "ais: using your existing index at %s\n"
                    "     (the default is now %s; run  ais --default \"%s\"\n"
                    "      to keep the old one, or move that folder to %s)\n",
                    legacy, def, legacy, def);
            return put_str(out, outsz, legacy);
        }
        return put_str(out, outsz, def);         /* ais_open creates it on first use */
    }
}
