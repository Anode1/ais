# GUI conventions (web, Flutter, native desktop)

One product across surfaces: the same vocabulary and order on the web GUI (`c/serve.c`),
the Flutter app (`app/flutter/lib/main.dart`), and the native Win32 app (`win32/ais-gui.c`).
Change a label in one, change it in all three.

## Vocabulary (LOCKED)
- The primary action **and** the first view are both labeled **Search** (not "Get", not
  "Recall"). The keyboard return key already renders "Search" (`TextInputAction.search`), so
  button + tab + keyboard all read the same. One word per concept.
- The three views, in this order: **Search · Timeline · Tags**.
- The create action is **Add**.
- Internal identifiers may keep their original names (`recall` / `getbtn` / `ID_GET` /
  `ID_VRECALL` / `_view=='recall'` / `data-v=recall`); only the **display labels** must read
  "Search". Renaming internals is optional churn, out of scope.

## Layout
- **Phone (Flutter):** search header on top; the three views in a bottom `NavigationBar`;
  Add as a FAB.
- **Web (`serve.c`):** same, bottom nav + Add FAB.
- **Desktop (Win32):** search box + a **Search** button on top; the three views as a row of
  tabs/buttons; Add as a normal button (NOT a phone-style bottom bar).

## History
The label drifted across surfaces (web/Flutter nav said "Search", the action button said
"Get", Win32 said "Recall"). Unified on **Search** for general-public clarity (avoids the
product-recall / MS-Recall ambiguity) and cross-platform consistency. Keep it that way.
