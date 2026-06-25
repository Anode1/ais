/* secret.c -- see secret.h. Detection of "aisc:" values and the recall-time
 * reveal policy. No crypto here: this is the plaintext-side routing, so it
 * builds and is testable whether or not crypto/ has been vendored. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "secret.h"

int secret_is_marked(const char *value)
{
    return value != NULL
        && strncmp(value, AIS_SECRET_PREFIX, sizeof AIS_SECRET_PREFIX - 1) == 0;
}

int secret_reveal_context(void)
{
    int fd;

    if (!isatty(STDOUT_FILENO))                  /* piped / redirected -> opaque */
        return 0;
    fd = open("/dev/tty", O_RDWR | O_NOCTTY);     /* must be able to prompt+reveal */
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

int secret_reveal(long id, const char *marked_value)
{
    char msg[160];
    int fd, n;

    (void)marked_value;                          /* the real version decrypts it */
    fd = open("/dev/tty", O_WRONLY | O_NOCTTY);
    if (fd < 0)
        return -1;
    n = snprintf(msg, sizeof msg,
                 "ais: record %ld is an encrypted secret; reveal is not wired yet "
                 "(vendor crypto/ to enable decrypt).\n", id);
    if (n > 0) {
        ssize_t w = write(fd, msg, (size_t)n);
        (void)w;
    }
    close(fd);
    return 0;
}
