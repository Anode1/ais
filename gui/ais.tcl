#!/usr/bin/env wish
# ais.tcl -- Tk GUI for AIS: recall-first (like search), with an expandable
# "add" panel. Shells out to the `ais` CLI -- the binary is the backend.
#   * type keys, Enter or Get -> ais KEY... (recall) -> results listed, one per line,
#     with a "N results for Q - T ms" header (the v1 search look).
#   * "+ add" expands a panel: values (one per line, or File.../Folder...) -> put,
#     and a document box -> ais --doc (saved as a blob file).
# How to run:  wish gui/ais.tcl   (or ./gui/ais.tcl)

package require Tk
set AIS [expr {[auto_execok ais] ne "" ? "ais" : "/home/vas/ais/c/ais"}]
set ADDSHOWN 0
set INDEX ""   ;# current index dir (passed as -f); empty = default resolution

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
ttk::button .f.storeb -text "Store…" -command choose_store
ttk::label  .f.status -text "type keys to recall, then Get" -anchor w
ttk::label  .f.storel -text "" -anchor w -foreground "#777777"

grid .f.q      -row 0 -column 0 -sticky ew -ipady 4
grid .f.get    -row 0 -column 1 -sticky e -padx {6 0}
grid .f.res    -row 1 -column 0 -columnspan 2 -sticky nsew -pady {10 0}
grid .f.add    -row 2 -column 0 -sticky w -pady {8 0}
grid .f.storeb -row 2 -column 1 -sticky e -pady {8 0}
grid .f.status -row 4 -column 0 -columnspan 2 -sticky ew -pady {8 0}
grid .f.storel -row 5 -column 0 -columnspan 2 -sticky ew
grid columnconfigure .f 0 -weight 1
grid rowconfigure    .f 1 -weight 1

# --- ADD panel (hidden until "+ add"): value-first, with its OWN keys field
# (prefilled from search, but editable; keys are optional). ------------------
ttk::frame  .f.p
ttk::label  .f.p.klab -text "Keys (space-separated, optional)"
ttk::entry  .f.p.keys
ttk::label  .f.p.vlab -text "What to remember (one value per line)"
text        .f.p.val  -height 4 -wrap word -relief solid -borderwidth 1 -padx 6 -pady 6
ttk::frame  .f.p.vb
ttk::button .f.p.vb.file -text "File…"   -command browse_file
ttk::button .f.p.vb.dir  -text "Folder…" -command browse_dir
ttk::button .f.p.vb.put  -text "Save" -command do_put
pack .f.p.vb.file -side left
pack .f.p.vb.dir  -side left -padx {6 0}
pack .f.p.vb.put  -side right
ttk::label  .f.p.dlab -text "…or a document (saved as a file)"
text        .f.p.doc  -height 4 -wrap word -relief solid -borderwidth 1 -padx 6 -pady 6
ttk::button .f.p.dput -text "Save document" -command do_doc
grid .f.p.klab -row 0 -column 0 -sticky w  -pady {0 3}
grid .f.p.keys -row 1 -column 0 -sticky ew -ipady 3
grid .f.p.vlab -row 2 -column 0 -sticky w  -pady {8 3}
grid .f.p.val  -row 3 -column 0 -sticky ew
grid .f.p.vb   -row 4 -column 0 -sticky ew -pady {3 10}
grid .f.p.dlab -row 5 -column 0 -sticky w  -pady {0 3}
grid .f.p.doc  -row 6 -column 0 -sticky ew
grid .f.p.dput -row 7 -column 0 -sticky e  -pady {3 0}
grid columnconfigure .f.p 0 -weight 1

bind .f.q <Return> do_get
focus .f.q

# --- store (which index) ---------------------------------------------------
proc ais_args {} { global INDEX; return [expr {$INDEX ne "" ? [list -f $INDEX] : {}}] }
proc cur_store {} {
    global AIS INDEX
    if {$INDEX ne ""} { return $INDEX }
    if {[catch {exec $AIS --where} w]} { return "(default)" }
    return [string trim $w]
}
proc refresh_store {} { .f.storel configure -text "store: [cur_store]" }
proc choose_store {} {
    global INDEX
    set d [tk_chooseDirectory -title "Choose AIS index folder"]
    if {$d eq ""} return
    set INDEX $d
    refresh_store
    if {[string trim [.f.q get]] ne ""} { do_get }
}

proc do_get {} {
    global AIS
    set keys [string trim [.f.q get]]
    if {$keys eq ""} { .f.status configure -text "type keys to recall"; return }
    set t0 [clock milliseconds]
    if {[catch {exec $AIS {*}[ais_args] {*}$keys} out]} {
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
        .f.p.keys delete 0 end
        .f.p.keys insert 0 [string trim [.f.q get]]   ;# prefill from search
        grid .f.p -row 3 -column 0 -columnspan 2 -sticky ew -pady {10 0}
        .f.add configure -text "− add"
        set ADDSHOWN 1
        focus .f.p.val
    }
}

proc add_keys {} { return [string trim [.f.p.keys get]] }   ;# may be empty (keyless)
proc where_txt {keys} { return [expr {$keys eq "" ? "(no keys)" : $keys}] }

proc add_path {p} {
    if {$p ne ""} { .f.p.val insert end "$p\n"; .f.status configure -text "added path: $p" }
}
proc browse_file {} { foreach p [tk_getOpenFile -multiple 1] { add_path $p } }
proc browse_dir  {} { add_path [tk_chooseDirectory] }

proc do_put {} {
    global AIS
    set keys [add_keys]
    set values {}
    foreach ln [split [.f.p.val get 1.0 end] "\n"] {
        set ln [string trim $ln]
        if {$ln ne ""} { lappend values $ln }
    }
    if {[llength $values] == 0} { .f.status configure -text "enter at least one value"; return }
    if {[catch {exec $AIS {*}[ais_args] -v - {*}$keys << [join $values "\n"]} err]} {
        .f.status configure -text "error: $err"
    } else {
        .f.status configure -text "stored [llength $values] value(s) under: [where_txt $keys]"
        .f.p.val delete 1.0 end
        if {$keys ne ""} { .f.q delete 0 end; .f.q insert 0 $keys; do_get }
    }
}

proc do_doc {} {
    global AIS
    set keys [add_keys]
    set text [.f.p.doc get 1.0 end]
    if {[string trim $text] eq ""} { .f.status configure -text "the document is empty"; return }
    if {[catch {exec $AIS {*}[ais_args] --doc {*}$keys << $text} out]} {
        .f.status configure -text "error: $out"
    } else {
        .f.status configure -text "saved document under: [where_txt $keys]"
        .f.p.doc delete 1.0 end
    }
}

# everything is defined now -> show the current store
refresh_store
