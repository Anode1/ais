#!/usr/bin/env python3
# ais-put.py -- minimal Tkinter demo: file values and documents into AIS.
#
# Two ways to add, both shelling out to `ais` under the shared keys:
#   * Values box  -> `ais put - KEY...` : each non-blank line is one single-line
#       value (URL, command, short note), stored inline.
#   * Document box -> `ais doc KEY...`  : a multi-line block saved as a file
#       (<index>/blobs/<id>.txt); the index stores its relative path.
# How to run:  python3 gui/ais-put.py   (or ./gui/ais-put.py)
# Requires Tkinter (stdlib, but a separate package on some distros):
#   Debian/Ubuntu: sudo apt install python3-tk   Fedora: sudo dnf install python3-tkinter

import shutil, subprocess, sys

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
root.title("AIS — add")
root.minsize(540, 560)

frm = ttk.Frame(root, padding=14)
frm.pack(fill="both", expand=True)


def textbox(h):
    return tk.Text(frm, height=h, wrap="word", font=("TkTextFont", 11),
                   relief="solid", borderwidth=1, padx=8, pady=8,
                   highlightthickness=1, highlightcolor="#4a90d9")


ttk.Label(frm, text="Keys (space-separated)").grid(row=0, column=0, sticky="w", pady=(0, 3))
keys = ttk.Entry(frm, font=("TkDefaultFont", 11))
keys.grid(row=1, column=0, sticky="ew", ipady=3, pady=(0, 12))
ttk.Label(frm, text="Values (one per line)").grid(row=2, column=0, sticky="w", pady=(0, 3))
val = textbox(6)
val.grid(row=3, column=0, sticky="nsew", pady=(0, 6))
status = ttk.Label(frm, text="Keys, then add values and/or a document", anchor="w")


def keys_or_warn():
    k = keys.get().strip()
    if not k:
        status.config(text="error: enter at least one key")
    return k


def do_put(*_):
    k = keys_or_warn()
    if not k:
        return
    values = [ln.strip() for ln in val.get("1.0", "end").splitlines() if ln.strip()]
    if not values:
        status.config(text="error: enter one value per line")
        return
    r = subprocess.run([AIS, "put", "-", *k.split()], input="\n".join(values) + "\n",
                       capture_output=True, text=True)
    if r.returncode != 0:
        status.config(text="error: " + (r.stderr.strip() or "put failed"))
    else:
        status.config(text=f"stored {len(values)} value(s) under: {k}")
        val.delete("1.0", "end")


def do_doc(*_):
    k = keys_or_warn()
    if not k:
        return
    text = doc.get("1.0", "end")
    if not text.strip():
        status.config(text="error: the document is empty")
        return
    r = subprocess.run([AIS, "doc", *k.split()], input=text, capture_output=True, text=True)
    if r.returncode != 0:
        status.config(text="error: " + (r.stderr.strip() or "doc failed"))
    else:
        status.config(text=f"saved document {r.stdout.strip()} under: {k}")
        doc.delete("1.0", "end")


ttk.Button(frm, text="Put values", command=do_put).grid(row=4, column=0, sticky="e", pady=(0, 12))
ttk.Label(frm, text="Document (saved as a file)").grid(row=5, column=0, sticky="w", pady=(0, 3))
doc = textbox(8)
doc.grid(row=6, column=0, sticky="nsew", pady=(0, 6))
ttk.Button(frm, text="Add document", command=do_doc).grid(row=7, column=0, sticky="e", pady=(0, 10))
status.grid(row=8, column=0, sticky="ew")

frm.columnconfigure(0, weight=1)
frm.rowconfigure(3, weight=1)
frm.rowconfigure(6, weight=2)
keys.focus_set()
root.mainloop()
