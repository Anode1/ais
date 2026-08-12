/* ptyrun.c -- run a command under a REAL pseudo-terminal and answer its prompts.
 *
 * Why this exists: aisc_prompt_passphrase() opens /dev/tty directly with
 * O_NOCTTY and clears ECHO, and it deliberately has no environment or file
 * bypass -- a passphrase must never be readable from $AIS_TTY, from argv, or
 * from a file, because the environment is visible in `ps -e` and /proc, and a
 * file is a file. That is the right design, and it means the CLI encrypt path
 * cannot be driven by redirecting stdin. The way to test it is to hand
 * the program a terminal, which is what forkpty does. Nothing here weakens the
 * binary under test: it runs exactly as shipped.
 *
 *   ptyrun <answers-file> <cmd> [args...]
 *
 * Each line of <answers-file> is sent only AFTER the child has gone quiet, so
 * the answer lands after the prompt and after tcsetattr(TCSAFLUSH), which
 * discards anything queued before it. Everything the child prints is copied to
 * stdout. Exits with the child's status, or 77 if no pty is available.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#  include <util.h>
#else
#  include <pty.h>
#endif

#define MAXLINES 32

int main(int argc, char **argv)
{
    char *lines[MAXLINES];
    int nlines = 0, sent = 0;
    char buf[4096];
    FILE *af;
    int master, status = 0;
    pid_t pid;

    if (argc < 3) {
        fprintf(stderr, "usage: ptyrun <answers-file> <cmd> [args...]\n");
        return 2;
    }

    af = fopen(argv[1], "r");
    if (af == NULL) { perror("ptyrun: answers"); return 2; }
    while (nlines < MAXLINES && fgets(buf, sizeof buf, af) != NULL) {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
        lines[nlines] = strdup(buf);
        if (lines[nlines] == NULL) { fclose(af); return 2; }
        nlines++;
    }
    fclose(af);

    pid = forkpty(&master, NULL, NULL, NULL);
    if (pid < 0) { fprintf(stderr, "ptyrun: no pty (%s) -- SKIP\n", strerror(errno)); return 77; }
    if (pid == 0) {
        execvp(argv[2], &argv[2]);
        _exit(127);
    }

    /* Answer on silence: the child has printed its prompt and stopped. */
    for (;;) {
        struct pollfd p;
        int r;

        p.fd = master; p.events = POLLIN; p.revents = 0;
        r = poll(&p, 1, 250);
        /* POLLHUP arrives without POLLIN when the child exits: read anyway, or
         * the loop spins on a hangup forever. */
        if (r > 0) {
            ssize_t got = read(master, buf, sizeof buf);
            if (got <= 0) break;                  /* child closed the pty */
            fwrite(buf, 1, (size_t)got, stdout);
            continue;
        }
        if (r == 0) {                             /* quiet: time to answer */
            if (sent < nlines) {
                dprintf(master, "%s\n", lines[sent]);
                sent++;
                continue;
            }
            if (waitpid(pid, &status, WNOHANG) == pid) break;
            continue;
        }
        if (r < 0 && errno != EINTR) break;
    }

    fflush(stdout);
    if (waitpid(pid, &status, 0) != pid && !WIFEXITED(status)) status = 0;
    close(master);
    while (nlines > 0) free(lines[--nlines]);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
