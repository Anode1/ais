#!/usr/bin/env wish
# ais-put.tcl -- minimal Tk demo: store one record in AIS.
#
# What it is: a tiny GUI (keys entry, value text area, Put button, status
#   label) that simply shells out to `ais put VALUE KEY...`. It does not
#   reimplement anything -- fork it for any toolkit (GTK/Qt/Cocoa/web), the
#   only contract is the `ais put` command line.
# How to run:  wish gui/ais-put.tcl   (or ./gui/ais-put.tcl)
# Note: the store is line-oriented, so a value must be a single line;
#   embedded newlines are flattened to spaces before calling put.

package require Tk

# Locate the binary: prefer `ais` on PATH, else the built copy.
set AIS [expr {[auto_execok ais] ne "" ? "ais" : "/home/vas/ais/c/ais"}]

wm title   . "AIS — put a record"
wm minsize . 520 420

# One padded container; ttk widgets give a native look on GNOME/macOS.
ttk::frame .f -padding 14
pack .f -fill both -expand 1

ttk::label  .f.klab -text "Keys (space-separated)"
ttk::entry  .f.keys -font {TkDefaultFont 11}
ttk::label  .f.vlab -text "Value"
text        .f.val  -height 12 -wrap word -font {TkTextFont 11} \
                    -relief solid -borderwidth 1 -padx 8 -pady 8 \
                    -highlightthickness 1 -highlightcolor "#4a90d9"
ttk::button .f.put  -text "Put" -command do_put
ttk::label  .f.status -text "Enter keys and a value, then Put" -anchor w

grid .f.klab   -row 0 -column 0 -sticky w    -pady {0 3}
grid .f.keys   -row 1 -column 0 -sticky ew   -pady {0 12} -ipady 3
grid .f.vlab   -row 2 -column 0 -sticky w    -pady {0 3}
grid .f.val    -row 3 -column 0 -sticky nsew -pady {0 12}
grid .f.put    -row 4 -column 0 -sticky e    -pady {0 10}
grid .f.status -row 5 -column 0 -sticky ew

grid columnconfigure .f 0 -weight 1
grid rowconfigure    .f 3 -weight 1   ;# the value area grows with the window

bind . <Control-Return> do_put
focus .f.keys

proc do_put {} {
    global AIS
    set keys  [string trim [.f.keys get]]
    set value [string trim [.f.val get 1.0 end]]
    if {$keys eq "" || $value eq ""} {
        .f.status configure -text "error: need both keys and a value"
        return
    }
    # Collapse any newlines (line-oriented store needs a single-line value).
    set flat [regsub -all {\s*\n\s*} $value " "]
    set note [expr {$flat ne $value ? " (newlines flattened)" : ""}]
    # exec in list form: each key is its own arg, spaces in value are safe.
    if {[catch {exec $AIS put $flat {*}$keys} out]} {
        .f.status configure -text "error: $out"
    } else {
        .f.status configure -text "stored id $out$note"
        .f.val delete 1.0 end
    }
}
