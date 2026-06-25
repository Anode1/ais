/* locate.c -- choose the index directory (front-end policy). See locate.h.
 *
 * Precedence (git-like):
 *   1. -f DIR  (the only explicit override -- AIS reads no env var for this)
 *   2. nearest .ais/ at or above the current dir
 *   3. the CURRENT named index from ~/.ais/config (`current = NAME` ->
 *      `index.NAME = PATH`); if no `current`, the legacy `index = PATH` line
 *   4. the built-in "home" index ~/.ais  (Windows: %USERPROFILE%\.ais)
 * For #4, if ~/.ais does not exist yet but the pre-1.0 location does, the old
 * one is used (with a one-line notice) so existing data is never stranded.
 *
 * Multiple indexes are like git branches: ~/.ais/config holds a registry of
 * `index.NAME = PATH` entries plus a `current = NAME` pointer; "home" (~/.ais)
 * is the always-present built-in. `--switch` moves current; `--indexes` lists.
 * Switching just repoints "current": indexes are separate stores, so there is
 * no merge of history (move records with --import / --import-interactively).
 *
 * Everything lives under one self-evident dir, ~/.ais (the SSH model: ~/.ssh +
 * ~/.ssh/config) -- no env-var indirection, identical on every OS.
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

/* Home-dir override (NULL/"" = the OS account home). A test seam, and a hook for
 * embedders wanting a custom config home. Set before any locate/registry call. */
static char g_home_override[AIS_PATH_MAX];
void ais_home_override(const char *dir)
{
    if (dir == NULL || dir[0] == '\0')
        g_home_override[0] = '\0';
    else
        snprintf(g_home_override, sizeof g_home_override, "%s", dir);
}

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
    if (g_home_override[0] != '\0')
        return put_str(out, outsz, g_home_override);
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

/* ---- ~/.ais/config: hand-editable `key = value` lines --------------------- */

/* Path of ~/.ais/config into OUT. 0/-1. */
static int config_path(char *out, size_t outsz)
{
    char dir[AIS_PATH_MAX];
    int n;
    if (home_ais(dir, sizeof dir) != 0)
        return -1;
    n = snprintf(out, outsz, "%s/config", dir);
    return (n >= 0 && (size_t)n < outsz) ? 0 : -1;
}

/* If LINE is `key = value`, copy the trimmed key into KBUF and return a pointer
 * to the value start (within LINE; trailing whitespace/newline not yet cut).
 * Returns NULL for a blank line, a `#` comment, or a line with no `=`. Does not
 * modify LINE. */
static const char *kv_split(const char *line, char *kbuf, size_t kbufsz)
{
    const char *p = line, *eq, *kend, *v;
    size_t kl;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '#')
        return NULL;
    eq = strchr(p, '=');
    if (eq == NULL)
        return NULL;
    kend = eq;
    while (kend > p && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
    if (kend == p)
        return NULL;
    kl = (size_t)(kend - p);
    if (kl >= kbufsz) kl = kbufsz - 1;
    memcpy(kbuf, p, kl);
    kbuf[kl] = '\0';
    v = eq + 1;
    while (*v == ' ' || *v == '\t') v++;
    return v;
}

/* Value of KEY from ~/.ais/config into OUT. 1 found, 0 absent/empty, -1 error. */
static int config_get(const char *key, char *out, size_t outsz)
{
    char cfg[AIS_PATH_MAX], line[AIS_LINE_MAX], kbuf[AIS_KEY_MAX];
    FILE *f;

    if (config_path(cfg, sizeof cfg) != 0)
        return -1;
    f = fopen(cfg, "r");
    if (f == NULL)
        return 0;                       /* no config -> not set */
    while (fgets(line, sizeof line, f) != NULL) {
        const char *v = kv_split(line, kbuf, sizeof kbuf);
        if (v != NULL && strcmp(kbuf, key) == 0) {
            size_t n = strlen(v);
            fclose(f);
            while (n > 0 && (v[n-1] == '\n' || v[n-1] == '\r'
                             || v[n-1] == ' ' || v[n-1] == '\t')) n--;
            if (n == 0)
                return 0;               /* `key =` (empty) -> not set */
            if (n >= outsz)
                return -1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* Set KEY = VALUE in ~/.ais/config, preserving every other line. VALUE NULL or
 * empty removes the key. Returns 0/-1. */
static int config_set(const char *key, const char *value)
{
    char dir[AIS_PATH_MAX], cfg[AIS_PATH_MAX], line[AIS_LINE_MAX], kbuf[AIS_KEY_MAX];
    static char keep[AIS_LINE_MAX * 4];     /* preserved lines (BSS, not the stack) */
    size_t klen = 0;
    FILE *f;

    if (home_ais(dir, sizeof dir) != 0)
        return -1;
    if (mkdir_p(dir) != 0)                  /* create ~/.ais if needed */
        return -1;
    if (config_path(cfg, sizeof cfg) != 0)
        return -1;

    f = fopen(cfg, "r");                     /* keep every line except KEY's */
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            const char *v = kv_split(line, kbuf, sizeof kbuf);
            size_t ll;
            if (v != NULL && strcmp(kbuf, key) == 0)
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
    if (value != NULL && value[0] != '\0')
        fprintf(f, "%s = %s\n", key, value);
    return (fclose(f) == 0) ? 0 : -1;
}

/* ---- the named-index registry (the git-branch-like layer) ----------------- */

int ais_home_path(char *out, size_t outsz)
{
    return home_ais(out, outsz);
}

int ais_current_get(char *out, size_t outsz)
{
    return config_get("current", out, outsz);
}

int ais_current_set(const char *name)
{
    if (name == NULL || name[0] == '\0' || strcmp(name, "home") == 0)
        return config_set("current", NULL);     /* clear -> home */
    return config_set("current", name);
}

int ais_index_path(const char *name, char *out, size_t outsz)
{
    char key[AIS_KEY_MAX];

    if (name == NULL || name[0] == '\0' || strcmp(name, "home") == 0)
        return (home_ais(out, outsz) == 0) ? 1 : -1;
    if (snprintf(key, sizeof key, "index.%s", name) >= (int)sizeof key)
        return -1;
    return config_get(key, out, outsz);
}

int ais_index_default_dir(const char *name, char *out, size_t outsz)
{
    char base[AIS_PATH_MAX];
    int n;

    if (name == NULL || name[0] == '\0')
        return -1;
    if (home_base(base, sizeof base) != 0)
        return -1;
    n = snprintf(out, outsz, "%s/.ais-%s", base, name);
    return (n >= 0 && (size_t)n < outsz) ? 0 : -1;
}

int ais_index_add(const char *name, const char *path)
{
    char key[AIS_KEY_MAX];

    if (name == NULL || name[0] == '\0' || strcmp(name, "home") == 0)
        return -1;                               /* "home" is reserved */
    if (path == NULL || path[0] == '\0')
        return -1;
    if (snprintf(key, sizeof key, "index.%s", name) >= (int)sizeof key)
        return -1;
    return config_set(key, path);
}

int ais_index_remove(const char *name)
{
    char key[AIS_KEY_MAX];

    if (name == NULL || name[0] == '\0' || strcmp(name, "home") == 0)
        return -1;
    if (snprintf(key, sizeof key, "index.%s", name) >= (int)sizeof key)
        return -1;
    return config_set(key, NULL);
}

int ais_index_list(ais_index_cb cb, void *ctx)
{
    char cfg[AIS_PATH_MAX], line[AIS_LINE_MAX], kbuf[AIS_KEY_MAX];
    FILE *f;
    int rc = 0;

    if (config_path(cfg, sizeof cfg) != 0)
        return -1;
    f = fopen(cfg, "r");
    if (f == NULL)
        return 0;
    while (fgets(line, sizeof line, f) != NULL) {
        const char *v = kv_split(line, kbuf, sizeof kbuf);
        if (v != NULL && strncmp(kbuf, "index.", 6) == 0 && kbuf[6] != '\0') {
            char val[AIS_PATH_MAX];
            size_t n = strlen(v);
            while (n > 0 && (v[n-1] == '\n' || v[n-1] == '\r'
                             || v[n-1] == ' ' || v[n-1] == '\t')) n--;
            if (n >= sizeof val) n = sizeof val - 1;
            memcpy(val, v, n);
            val[n] = '\0';
            rc = cb(kbuf + 6, val, ctx);         /* kbuf+6 skips "index." */
            if (rc != 0)
                break;
        }
    }
    fclose(f);
    return rc;
}

/* ---- deprecated single default (back-compat) ------------------------------ */

int ais_default_get(char *out, size_t outsz)
{
    return config_get("index", out, outsz);
}

int ais_default_set(const char *path)
{
    return config_set("index", path);            /* NULL/empty clears it */
}

/* ---- resolution ----------------------------------------------------------- */

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

    {                                            /* 3. current named index */
        char cur[AIS_KEY_MAX];
        int have = ais_current_get(cur, sizeof cur);
        if (have < 0)
            return -1;
        if (have == 1) {                         /* `current = NAME` is set */
            if (cur[0] != '\0' && strcmp(cur, "home") != 0) {
                rc = ais_index_path(cur, out, outsz);
                if (rc < 0)
                    return -1;
                if (rc == 1)
                    return 0;
                fprintf(stderr,
                        "ais: current index '%s' is not registered; using home (~/.ais).\n"
                        "     'ais --indexes' to list, 'ais --switch NAME' to fix.\n", cur);
            }
            /* current = home/empty, or a dangling name: fall through to home */
        } else {                                 /* no current -> legacy `index = PATH` */
            rc = ais_default_get(out, outsz);
            if (rc < 0)
                return -1;
            if (rc == 1)
                return 0;
        }
    }

    {                                            /* 4. built-in "home": ~/.ais */
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
