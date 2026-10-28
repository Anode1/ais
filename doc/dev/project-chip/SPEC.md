# Project chip: behaviour spec

A project is one tag the front-end keeps active: every search is ANDed with it, Recent lists only its items, and the Add sheet starts with it. The engine sees an ordinary tag. Mock: `mock.html`, rendered in `mock.png`. Decided 2026-10-28; the three front ends implement it word for word, and `tests/gui/cdptest.c` (both web pages) and `app/flutter/test` pin it.

# Choosing a project

From the Tags view only: each row's menu gains a first item, "Make it the project". Tapping a tag name still searches for it.

Slack, Apple Notes and Todoist all pick the working context from a list of all contexts. A typed tag stays a search term, as Gmail's typed search chips vanish with the search. The first time, a toast says: "Working in travel on this device. New items get this tag."

# The chip

A filled pill in the FAB's colours (`--fabbg` on `--fabfg`), the tag icon from the Tags tab, the name, and a cross. 34px high. Long names end in an ellipsis at 45% of the field's width. Phone and desktop look the same.

- Search bar: first in the row, typed tags after it, placeholder "more tags".
- Add sheet: first in the tag field, typed tags after it.
- Item meta lines inside a project omit the project tag: Gmail hides the current label in a label view, and Notes does not print the folder on its own notes.

# Gestures

| Where | Tap the name | Tap the cross |
|---|---|---|
| Search bar | Menu: "Switch project" (opens Tags), "Leave project", caption "Kept on this device" | Leave the project; toast "Left travel" with Undo |
| Add sheet | Nothing | This item only is saved without the project; the chip is back next time |

Slack's channel name opens a channel menu; the Undo toast is AIS's own, from delete. The sheet's cross follows Todoist, whose quick-add prefills the current project and lets one task go elsewhere without leaving it. The bar's chip belongs to the app and the sheet's chip to the item being written, so their crosses act on those.

No long-press: neither front-end uses one, and Apple's HIG keeps it for shortcuts to actions reachable another way. On a desktop keyboard, Backspace in an empty field highlights the chip and a second Backspace removes it (Gmail's recipient field, macOS token fields).

# The three views

- Recent: only the project's items, newest first, with day headers.
- Search: project AND typed tags. "Any tag" ORs the typed tags only; the project stays required (the page asks for the rows' tags and drops those without it). Text search matches are filtered to the project too (`/api/find?key=`). Below the results: "Only items tagged travel. Search all items": the same query once without the project, chip kept (Apple Mail's "All Mailboxes" scope).
- Tags: every tag, unfiltered, because this is where projects are switched (Slack's sidebar, the Notes folder list). The active project's row comes first, tinted, and its menu says "Leave project".

The search bar sits on all three views, so the chip is always on screen, as a shell prompt shows the current directory.

# Empty states

- Recent, project has no items: the project ends (below). The engine keeps no empty tag, so this state is reached only by deleting the last item.
- Search, nothing typed: "Type tags to search inside travel."
- Search, no match: "No results for food in travel." plus "Search all items".

# Saving outside the project

Tap the cross on the sheet's chip; the save says "Saved outside travel". Typing the project's name by hand adds it once. The Edit dialog never adds the project.

# Persistence and scope

The project survives reloads, restarts and updates until the user leaves it. It is kept per device and per library: switching library shows that library's project, or none. Sync carries only the tag on each item. The chip's menu caption "Kept on this device" says so.

Storage: the web page and the PWA in `localStorage`, keyed by library path (`ais.project:<path>`; clearing site data loses it, which was accepted); the Flutter app in a sidecar file `${dir}_project`, beside `_lastsync`.

If the user removes the project's tag from all items, deletes them from the Tags view, or Recent inside the project comes back empty, the project ends and the bar's chip goes, with a notice.

# Recent inside a project

Recent needs a save time per item and recall hits carry none, so the timeline gained a tag filter: `ais_timeline_key` in the engine (the page's ids come from the tag's posting list, one seek per row), `/api/timeline?key=` and `ais_embed_timeline_key` at the seams.

# Flutter

- The bar chip is an `InputChip` with the tag icon as avatar: `onPressed` opens a `MenuAnchor` with the two items, `onDeleted` leaves with a `SnackBar` Undo.
- The sheet chip is the `InputChip` `EditTagsDialog` already uses.
- Android's back gesture closes the sheet or exits the app, and keeps the project.
- The Tags row menu item and the empty states match the web text word for word.

# Decisions taken

The five questions left open on 2026-10-27 were answered the next day: the timeline filter above; "Any tag" keeps the project required; "Search all items" stays as a one-shot escape; a project ends with its last item; browser storage is acceptable for the web pages.
