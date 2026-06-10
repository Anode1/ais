Viewing the AIS man page
========================
Source: ais.1 (roff, man section 1). To read it:

  man -l man/ais.1                     view this source file directly (no install)
  groff -man -Tutf8 man/ais.1 | less   render to a pager
  man ais                              after `sudo make install` (-> share/man/man1)

Editing: it is plain roff; keep the SYNOPSIS and COMMANDS in sync with c/help.c.
After editing, check it renders clean:

  groff -man -Tutf8 -ww man/ais.1 >/dev/null
