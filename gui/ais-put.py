#!/usr/bin/env python3
# ais-put.py -- minimal Tkinter demo: store one record in AIS.
#
# What it is: a tiny GUI (keys entry, value text area, Put button, status
#   label) that simply shells out to `ais put VALUE KEY...`. It does not
#   reimplement anything -- fork it for any toolkit (GTK/Qt/Cocoa/web), the
#   only contract is the `ais put` command line.
# How to run:  python3 gui/ais-put.py   (or ./gui/ais-put.py)
# Requires Tkinter (stdlib, but packaged separately on some distros):
#   Debian/Ubuntu: sudo apt install python3-tk   Fedora: sudo dnf install python3-tkinter
# Note: the store is line-oriented, so a value must be a single line;
#   embedded newlines are flattened to spaces before calling put.

import re, shutil, subprocess, sys

try:
    import tkinter as tk
    from tkinter import ttk
except ModuleNotFoundError:
    sys.exit("ais-put: Tkinter is not installed.\n"
             "  Debian/Ubuntu:  sudo apt install python3-tk\n"
             "  Fedora:         sudo dnf install python3-tkinter\n"
             "  macOS (brew):   brew install python-tk")

# Locate the binary: prefer `ais` on PATH, else the built copy.
AIS = shutil.which("ais") or "/home/vas/ais/c/ais"

root = tk.Tk()
root.title("AIS — put a record")
root.minsize(520, 420)

frm = ttk.Frame(root, padding=14)
frm.pack(fill="both", expand=True)

ttk.Label(frm, text="Keys (space-separated)").grid(row=0, column=0, sticky="w", pady=(0, 3))
keys = ttk.Entry(frm, font=("TkDefaultFont", 11))
keys.grid(row=1, column=0, sticky="ew", ipady=3, pady=(0, 12))
ttk.Label(frm, text="Value").grid(row=2, column=0, sticky="w", pady=(0, 3))
val = tk.Text(frm, height=12, wrap="word", font=("TkTextFont", 11),
              relief="solid", borderwidth=1, padx=8, pady=8,
              highlightthickness=1, highlightcolor="#4a90d9")
val.grid(row=3, column=0, sticky="nsew", pady=(0, 12))
status = ttk.Label(frm, text="Enter keys and a value, then Put", anchor="w")


def do_put(*_):
    k = keys.get().strip()
    v = val.get("1.0", "end").strip()
    if not k or not v:
        status.config(text="error: need both keys and a value")
        return
    # Collapse any newlines (line-oriented store needs a single-line value).
    flat = re.sub(r"\s*\n\s*", " ", v)
    note = " (newlines flattened)" if flat != v else ""
    # subprocess arg-list form: each key its own arg, spaces in value are safe.
    r = subprocess.run([AIS, "put", flat, *k.split()],
                       capture_output=True, text=True)
    if r.returncode != 0:
        status.config(text="error: " + (r.stderr.strip() or "put failed"))
    else:
        status.config(text="stored id " + r.stdout.strip() + note)
        val.delete("1.0", "end")


ttk.Button(frm, text="Put", command=do_put).grid(row=4, column=0, sticky="e", pady=(0, 10))
status.grid(row=5, column=0, sticky="ew")

frm.columnconfigure(0, weight=1)
frm.rowconfigure(3, weight=1)   # the value area grows with the window
root.bind("<Control-Return>", do_put)
keys.focus_set()
root.mainloop()
