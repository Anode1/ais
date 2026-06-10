#!/usr/bin/env wish
# ais-put.tcl -- minimal Tk demo: file values and documents into AIS.
#
# Two ways to add, both shelling out to `ais` under the shared keys:
#   * Values box  -> `ais put - KEY...` : each non-blank line is one single-line
#       value (URL, command, short note), stored inline.
#   * Document box -> `ais doc KEY...`  : a multi-line block saved as a file
#       (<index>/blobs/<id>.txt); the index stores its relative path.
# How to run:  wish gui/ais-put.tcl   (or ./gui/ais-put.tcl)

package require Tk

# Locate the binary: prefer `ais` on PATH, else the built copy.
set AIS [expr {[auto_execok ais] ne "" ? "ais" : "/home/vas/ais/c/ais"}]

wm title   . "AIS — add"
wm minsize . 540 560

ttk::frame .f -padding 14
pack .f -fill both -expand 1

ttk::label  .f.klab -text "Keys (space-separated)"
ttk::entry  .f.keys -font {TkDefaultFont 11}

ttk::label  .f.vlab -text "Values (one per line)"
text        .f.val  -height 6 -wrap word -font {TkTextFont 11} \
                    -relief solid -borderwidth 1 -padx 8 -pady 8 \
                    -highlightthickness 1 -highlightcolor "#4a90d9"
ttk::button .f.vput -text "Put values" -command do_put

ttk::label  .f.dlab -text "Document (saved as a file)"
text        .f.doc  -height 8 -wrap word -font {TkTextFont 11} \
                    -relief solid -borderwidth 1 -padx 8 -pady 8 \
                    -highlightthickness 1 -highlightcolor "#4a90d9"
ttk::button .f.dput -text "Add document" -command do_doc

ttk::label  .f.status -text "Keys, then add values and/or a document" -anchor w

grid .f.klab   -row 0 -column 0 -sticky w    -pady {0 3}
grid .f.keys   -row 1 -column 0 -sticky ew   -pady {0 12} -ipady 3
grid .f.vlab   -row 2 -column 0 -sticky w    -pady {0 3}
grid .f.val    -row 3 -column 0 -sticky nsew -pady {0 6}
grid .f.vput   -row 4 -column 0 -sticky e    -pady {0 12}
grid .f.dlab   -row 5 -column 0 -sticky w    -pady {0 3}
grid .f.doc    -row 6 -column 0 -sticky nsew -pady {0 6}
grid .f.dput   -row 7 -column 0 -sticky e    -pady {0 10}
grid .f.status -row 8 -column 0 -sticky ew

grid columnconfigure .f 0 -weight 1
grid rowconfigure    .f 3 -weight 1
grid rowconfigure    .f 6 -weight 2

focus .f.keys

proc keys_or_warn {} {
    set keys [string trim [.f.keys get]]
    if {$keys eq ""} { .f.status configure -text "error: enter at least one key" }
    return $keys
}

# Values box: each non-blank line a value under the keys (ais put - KEY...).
proc do_put {} {
    global AIS
    set keys [keys_or_warn]
    if {$keys eq ""} return
    set values {}
    foreach ln [split [.f.val get 1.0 end] "\n"] {
        set ln [string trim $ln]
        if {$ln ne ""} { lappend values $ln }
    }
    if {[llength $values] == 0} {
        .f.status configure -text "error: enter one value per line"
        return
    }
    if {[catch {exec $AIS put - {*}$keys << [join $values "\n"]} err]} {
        .f.status configure -text "error: $err"
    } else {
        .f.status configure -text "stored [llength $values] value(s) under: $keys"
        .f.val delete 1.0 end
    }
}

# Document box: a multi-line block saved as a blob file (ais doc KEY...).
proc do_doc {} {
    global AIS
    set keys [keys_or_warn]
    if {$keys eq ""} return
    set text [.f.doc get 1.0 end]
    if {[string trim $text] eq ""} {
        .f.status configure -text "error: the document is empty"
        return
    }
    if {[catch {exec $AIS doc {*}$keys << $text} out]} {
        .f.status configure -text "error: $out"
    } else {
        .f.status configure -text "saved document $out under: $keys"
        .f.doc delete 1.0 end
    }
}
