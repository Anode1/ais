#!/usr/bin/env wish
# ais.tcl -- Tk GUI for AIS: recall-first (like search), with an expandable
# "add" panel. Shells out to the `ais` CLI -- the binary is the backend.
#   * type keys, Enter or Get -> ais KEY... (recall) -> results listed, one per line,
#     with a "N results for Q - T ms" header (the v1 search look).
#   * "+ add" expands a panel: one box -> the WHOLE text is one entry (a
#     multi-line value is saved as a document/blob via ais --doc); File.../
#     Folder... index a chosen path as its own record.
# How to run:  wish gui/ais.tcl   (or ./gui/ais.tcl)

package require Tk
# Find the engine: PATH first, then a sibling next to this script (the release
# bundle ships ais.tcl beside the ais binary), then the dev checkout.
set _here [file dirname [file normalize [info script]]]
if {[auto_execok ais] ne ""} {
    set AIS ais
} elseif {[file executable [file join $_here ais]]} {
    set AIS [file join $_here ais]
} elseif {[file executable [file join $_here ais.exe]]} {
    set AIS [file join $_here ais.exe]
} else {
    set AIS /home/vas/ais/c/ais
}
set ADDSHOWN 0
set INDEX ""       ;# current index dir (passed as -f); empty = default resolution
set VIEW  recall   ;# recall | timeline | tags  (the segmented control)
set MATCHALL 0     ;# 0 = OR (any key, the default); 1 = AND (match all keys)

wm title   . "AIS"
wm minsize . 540 460

ttk::frame .f -padding 14
pack .f -fill both -expand 1

# --- GET first: the search row + results ----------------------------------
ttk::entry  .f.q   -font {TkDefaultFont 12}
ttk::button .f.get -text "Get" -command do_get
ttk::checkbutton .f.all -text "Match all keys" -variable MATCHALL
text        .f.res -height 14 -wrap word -font {TkTextFont 11} -state disabled \
                   -relief solid -borderwidth 1 -padx 8 -pady 8
.f.res tag configure head -foreground "#777777" -spacing3 10
.f.res tag configure item -spacing1 4 -spacing3 4   ;# space above+below each result
.f.res tag configure link -foreground "#1a0dab"
.f.res tag configure meta -foreground "#777777" -spacing3 6   ;# timeline time+keys subline
.f.res tag configure tagkey -foreground "#1a0dab"            ;# a clickable tag in Tags view

# --- Recall / Timeline / Tags switch (a segmented control) ------------------
ttk::frame       .f.seg
ttk::radiobutton .f.seg.r -text "Get"      -value recall   -variable VIEW \
                 -style Toolbutton -command show_view
ttk::radiobutton .f.seg.t -text "Timeline" -value timeline -variable VIEW \
                 -style Toolbutton -command show_view
ttk::radiobutton .f.seg.g -text "Tags"     -value tags     -variable VIEW \
                 -style Toolbutton -command show_view
pack .f.seg.r .f.seg.t .f.seg.g -side left -fill x -expand 1

ttk::button .f.add -text "+ add" -command toggle_add
ttk::button .f.storeb -text "Store…" -command choose_store
ttk::label  .f.status -text "type keys, then Get" -anchor w
ttk::label  .f.storel -text "" -anchor w -foreground "#777777"

grid .f.q      -row 0 -column 0 -sticky ew -ipady 4
grid .f.get    -row 0 -column 1 -sticky e -padx {6 0}
grid .f.all    -row 0 -column 2 -sticky e -padx {6 0}
grid .f.seg    -row 1 -column 0 -columnspan 2 -sticky ew -pady {8 0}
grid .f.res    -row 2 -column 0 -columnspan 2 -sticky nsew -pady {10 0}
grid .f.add    -row 3 -column 0 -sticky w -pady {8 0}
grid .f.storeb -row 3 -column 1 -sticky e -pady {8 0}
grid .f.status -row 5 -column 0 -columnspan 2 -sticky ew -pady {8 0}
grid .f.storel -row 6 -column 0 -columnspan 2 -sticky ew
grid columnconfigure .f 0 -weight 1
grid rowconfigure    .f 2 -weight 1

# clicking a tag in the Tags view recalls it
.f.res tag bind tagkey <Button-1> {tag_click %x %y}
.f.res tag bind tagkey <Enter> {.f.res configure -cursor hand2}
.f.res tag bind tagkey <Leave> {.f.res configure -cursor xterm}

# --- ADD panel (hidden until "+ add"): keys-first, with its OWN keys field
# (prefilled from search, but editable; keys are optional). ------------------
ttk::frame  .f.p
ttk::label  .f.p.klab -text "Keys (space-separated, optional)"
ttk::entry  .f.p.keys
ttk::label  .f.p.vlab -text "What to remember (the whole box is one entry)"
text        .f.p.val  -height 3 -wrap word -relief solid -borderwidth 1 -padx 6 -pady 6
ttk::frame  .f.p.vb
ttk::button .f.p.vb.file -text "File…"   -command browse_file
ttk::button .f.p.vb.dir  -text "Folder…" -command browse_dir
ttk::button .f.p.vb.put  -text "Save" -command do_put
pack .f.p.vb.file -side left
pack .f.p.vb.dir  -side left -padx {6 0}
pack .f.p.vb.put  -side right
grid .f.p.klab -row 0 -column 0 -sticky w  -pady {0 3}
grid .f.p.keys -row 1 -column 0 -sticky ew -ipady 3
grid .f.p.vlab -row 2 -column 0 -sticky w  -pady {8 3}
grid .f.p.val  -row 3 -column 0 -sticky ew
grid .f.p.vb   -row 4 -column 0 -sticky ew -pady {3 10}
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
    global INDEX AIS
    set d [tk_chooseDirectory -title "Choose AIS index folder"]
    if {$d eq ""} return
    set INDEX $d
    catch {exec $AIS --default $d}   ;# persist the choice (~/.ais/config)
    refresh_store
    if {[string trim [.f.q get]] ne ""} { do_get }
}

# --- the segmented control: recall / timeline / tags ----------------------
proc show_view {} {
    global VIEW
    switch $VIEW {
        timeline { do_timeline }
        tags     { do_tags }
        default  { do_get }
    }
}

# Timeline: `ais --timeline` emits "WHEN<TAB>KEYS<TAB>VALUE", dateless first,
# then newest first. Group consecutive rows under their day.
proc do_timeline {} {
    global AIS
    if {[catch {exec $AIS {*}[ais_args] --timeline} out]} {
        .f.status configure -text "error: $out"; return
    }
    .f.res configure -state normal
    .f.res delete 1.0 end
    set day ""
    set n 0
    foreach ln [split $out "\n"] {
        if {$ln eq ""} continue
        set parts [split $ln "\t"]
        set when [lindex $parts 0]
        set keys [lindex $parts 1]
        set val  [lindex $parts 2]
        set d [expr {$when eq "(undated)" ? "(undated)" : [lindex [split $when] 0]}]
        if {$d ne $day} { set day $d; .f.res insert end "$d\n" head }
        set tags item
        if {[string match "http*" $val]} { lappend tags link }
        .f.res insert end "$val\n" $tags
        set tm [expr {$when eq "(undated)" ? "" : "[lindex [split $when] 1]  "}]
        .f.res insert end "    $tm$keys\n" meta
        incr n
    }
    if {$n == 0} { .f.res insert end "nothing saved yet\n" head }
    .f.res configure -state disabled
    .f.status configure -text "timeline: $n record[expr {$n == 1 ? {} : {s}}]"
}

# Tags: `ais --tags` emits "  <count>  <key>" busiest first. Each key is
# clickable (tag_click) to recall it.
proc do_tags {} {
    global AIS
    if {[catch {exec $AIS {*}[ais_args] --tags} out]} {
        .f.status configure -text "error: $out"; return
    }
    .f.res configure -state normal
    .f.res delete 1.0 end
    set n 0
    foreach ln [split $out "\n"] {
        set ln [string trim $ln]
        if {$ln eq ""} continue
        set sp [string first " " $ln]
        set count [string range $ln 0 [expr {$sp - 1}]]
        set key [string trim [string range $ln $sp end]]
        .f.res insert end $key tagkey
        .f.res insert end "   ($count)\n" meta
        incr n
    }
    if {$n == 0} { .f.res insert end "no tags yet\n" head }
    .f.res configure -state disabled
    .f.status configure -text "tags: $n key[expr {$n == 1 ? {} : {s}}]"
}

# click a key in the Tags view -> put it in the search box and recall it
proc tag_click {x y} {
    set idx [.f.res index @$x,$y]
    set line [.f.res get "$idx linestart" "$idx lineend"]
    set key [string trim [lindex [split $line "("] 0]]
    if {$key eq ""} return
    global VIEW
    set VIEW recall
    .f.q delete 0 end
    .f.q insert 0 $key
    do_get
}

proc do_get {} {
    global AIS VIEW MATCHALL
    set VIEW recall
    set keys [string trim [.f.q get]]
    if {$keys eq ""} {
        # entering Recall with no query: blank the pane (the old Timeline/Tags/
        # query content does not belong here) and show a hint -- the cleared
        # pane is itself the feedback that the tab changed.
        .f.res configure -state normal
        .f.res delete 1.0 end
        .f.res insert end "type keys above, then Get\n" head
        .f.res configure -state disabled
        .f.status configure -text "get"
        return
    }
    set t0 [clock milliseconds]
    set mode [expr {$MATCHALL ? [list] : [list -o]}]   ;# default OR; -o = union (any key)
    if {[catch {exec $AIS {*}[ais_args] {*}$mode {*}$keys} out]} {
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
    .f.status configure -text "get: $keys"
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
        grid .f.p -row 4 -column 0 -columnspan 2 -sticky ew -pady {10 0}
        .f.add configure -text "− add"
        set ADDSHOWN 1
        focus .f.p.val
    }
}

proc add_keys {} { return [string trim [.f.p.keys get]] }   ;# may be empty (keyless)
proc where_txt {keys} { return [expr {$keys eq "" ? "(no keys)" : $keys}] }

# File…/Folder… index a chosen path as its OWN record (one path = one value),
# kept separate from the text box, which is a single free-text entry.
proc store_path {p} {
    global AIS
    if {$p eq ""} return
    set keys [add_keys]
    if {[catch {exec $AIS {*}[ais_args] -v $p {*}$keys} err]} {
        .f.status configure -text "error: $err"
    } else {
        .f.status configure -text "indexed path under: [where_txt $keys]"
    }
}
proc browse_file {} { foreach p [tk_getOpenFile -multiple 1] { store_path $p } }
proc browse_dir  {} { store_path [tk_chooseDirectory] }

# The whole box is ONE entry: a single line is a plain record; a genuinely
# multi-line value is saved as a document (a blob via ais --doc). A pasted
# block is never split into several records.
proc do_put {} {
    global AIS
    set keys [add_keys]
    set text [string trimright [.f.p.val get 1.0 end] "\n"]
    if {[string trim $text] eq ""} { .f.status configure -text "enter something to remember"; return }
    if {[string first "\n" $text] >= 0} {
        set rc [catch {exec $AIS {*}[ais_args] --doc {*}$keys << $text} err]; set what "document"
    } else {
        set rc [catch {exec $AIS {*}[ais_args] -v $text {*}$keys} err];       set what "entry"
    }
    if {$rc} {
        .f.status configure -text "error: $err"
    } else {
        .f.status configure -text "saved $what under: [where_txt $keys]"
        .f.p.val delete 1.0 end
        .f.p.keys delete 0 end                    ;# reset the Add panel for the next entry
        if {$keys ne ""} { .f.q delete 0 end; .f.q insert 0 $keys; do_get }
        focus .f.p.keys
    }
}

# everything is defined now -> show the current store
refresh_store
