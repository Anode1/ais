#!/usr/bin/env wish
# ais-put.tcl -- Tk GUI for AIS: recall-first (like search), with an expandable
# "add" panel. Shells out to the `ais` CLI -- the binary is the backend.
#   * type keys, Enter or Get -> ais KEY... (recall) -> results listed, one per line,
#     with a "N results for Q - T ms" header (the v1 search look).
#   * "+ add" expands a panel: values (one per line, or File.../Folder...) -> put,
#     and a document box -> ais --doc (saved as a blob file).
# How to run:  wish gui/ais-put.tcl   (or ./gui/ais-put.tcl)

package require Tk
set AIS [expr {[auto_execok ais] ne "" ? "ais" : "/home/vas/ais/c/ais"}]
set ADDSHOWN 0

wm title   . "AIS"
wm minsize . 540 460

ttk::frame .f -padding 14
pack .f -fill both -expand 1

# --- GET first: the search row + results ----------------------------------
ttk::entry  .f.q   -font {TkDefaultFont 12}
ttk::button .f.get -text "Get" -command do_get
text        .f.res -height 14 -wrap word -font {TkTextFont 11} -state disabled \
                   -relief solid -borderwidth 1 -padx 8 -pady 8
.f.res tag configure head -foreground "#777777" -spacing3 10
.f.res tag configure item -spacing1 4 -spacing3 4   ;# space above+below each result
.f.res tag configure link -foreground "#1a0dab"
ttk::button .f.add -text "+ add" -command toggle_add
ttk::label  .f.status -text "type keys to recall, then Get" -anchor w

grid .f.q      -row 0 -column 0 -sticky ew -ipady 4
grid .f.get    -row 0 -column 1 -sticky e -padx {6 0}
grid .f.res    -row 1 -column 0 -columnspan 2 -sticky nsew -pady {10 0}
grid .f.add    -row 2 -column 0 -sticky w -pady {8 0}
grid .f.status -row 4 -column 0 -columnspan 2 -sticky ew -pady {8 0}
grid columnconfigure .f 0 -weight 1
grid rowconfigure    .f 1 -weight 1

# --- ADD panel (hidden until "+ add") -------------------------------------
ttk::frame  .f.p
ttk::label  .f.p.vlab -text "Values (one per line)"
text        .f.p.val  -height 4 -wrap word -relief solid -borderwidth 1 -padx 6 -pady 6
ttk::frame  .f.p.vb
ttk::button .f.p.vb.file -text "File…"   -command browse_file
ttk::button .f.p.vb.dir  -text "Folder…" -command browse_dir
ttk::button .f.p.vb.put  -text "Put values" -command do_put
pack .f.p.vb.file -side left
pack .f.p.vb.dir  -side left -padx {6 0}
pack .f.p.vb.put  -side right
ttk::label  .f.p.dlab -text "Document (saved as a file)"
text        .f.p.doc  -height 4 -wrap word -relief solid -borderwidth 1 -padx 6 -pady 6
ttk::button .f.p.dput -text "Add document" -command do_doc
grid .f.p.vlab -row 0 -column 0 -sticky w  -pady {0 3}
grid .f.p.val  -row 1 -column 0 -sticky ew
grid .f.p.vb   -row 2 -column 0 -sticky ew -pady {3 10}
grid .f.p.dlab -row 3 -column 0 -sticky w  -pady {0 3}
grid .f.p.doc  -row 4 -column 0 -sticky ew
grid .f.p.dput -row 5 -column 0 -sticky e  -pady {3 0}
grid columnconfigure .f.p 0 -weight 1

bind .f.q <Return> do_get
focus .f.q

proc do_get {} {
    global AIS
    set keys [string trim [.f.q get]]
    if {$keys eq ""} { .f.status configure -text "type keys to recall"; return }
    set t0 [clock milliseconds]
    if {[catch {exec $AIS {*}$keys} out]} {
        .f.status configure -text "error: $out"
        return
    }
    set ms [expr {[clock milliseconds] - $t0}]
    set vals {}
    foreach ln [split $out "\n"] {
        if {$ln eq ""} continue
        set bar [string first "|" $ln]
        lappend vals [expr {$bar >= 0 ? [string range $ln [expr {$bar + 1}] end] : $ln}]
    }
    set n [llength $vals]
    .f.res configure -state normal
    .f.res delete 1.0 end
    .f.res insert end "$n result[expr {$n == 1 ? {} : {s}}] for $keys - $ms ms\n" head
    foreach v $vals {
        set tags item
        if {[string match "http*" $v]} { lappend tags link }
        .f.res insert end "$v\n" $tags
    }
    .f.res configure -state disabled
    .f.status configure -text "recall: $keys"
}

proc toggle_add {} {
    global ADDSHOWN
    if {$ADDSHOWN} {
        grid remove .f.p
        .f.add configure -text "+ add"
        set ADDSHOWN 0
    } else {
        grid .f.p -row 3 -column 0 -columnspan 2 -sticky ew -pady {10 0}
        .f.add configure -text "− add"
        set ADDSHOWN 1
    }
}

proc keys_or_warn {} {
    set keys [string trim [.f.q get]]
    if {$keys eq ""} { .f.status configure -text "type keys in the search box first" }
    return $keys
}

proc add_path {p} {
    if {$p ne ""} { .f.p.val insert end "$p\n"; .f.status configure -text "added path: $p" }
}
proc browse_file {} { foreach p [tk_getOpenFile -multiple 1] { add_path $p } }
proc browse_dir  {} { add_path [tk_chooseDirectory] }

proc do_put {} {
    global AIS
    set keys [keys_or_warn]
    if {$keys eq ""} return
    set values {}
    foreach ln [split [.f.p.val get 1.0 end] "\n"] {
        set ln [string trim $ln]
        if {$ln ne ""} { lappend values $ln }
    }
    if {[llength $values] == 0} { .f.status configure -text "enter one value per line"; return }
    if {[catch {exec $AIS -v - {*}$keys << [join $values "\n"]} err]} {
        .f.status configure -text "error: $err"
    } else {
        .f.status configure -text "stored [llength $values] value(s) under: $keys"
        .f.p.val delete 1.0 end
    }
}

proc do_doc {} {
    global AIS
    set keys [keys_or_warn]
    if {$keys eq ""} return
    set text [.f.p.doc get 1.0 end]
    if {[string trim $text] eq ""} { .f.status configure -text "the document is empty"; return }
    if {[catch {exec $AIS --doc {*}$keys << $text} out]} {
        .f.status configure -text "error: $out"
    } else {
        .f.status configure -text "saved document $out under: $keys"
        .f.p.doc delete 1.0 end
    }
}
