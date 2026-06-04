# Notes + BLE keyboard refactor

**Status:** Shipped (product + BLE layouts + quick reconnect). Optional follow-up: split `ble_keyboard.cpp` for maintainability; document `/books/notes` in README.

This document records the design intent and where the firmware landed. It is not an implementation checklist.

---

## Why refactor

- **BLE keyboard** had grown into a single ~1.3k-line module mixing scan, bonding, NVS, worker, and HID — hard to change safely.
- **Notes** were a POC (`/notes/poc.txt`, one monolithic screen). The product needed multiple notes on-device, read vs edit, and BLE only when typing.

Goals: mirror how **books** are stored and discovered, keep the **library tree** clean, and confine **Wi‑Fi off + BLE session** to edit mode.

---

## Design decisions

### Storage and catalog

| Topic          | Decision                                                                  |
| -------------- | ------------------------------------------------------------------------- |
| Note files     | `/books/notes/*.txt` — same recursive scan as books (`loadBooks()`)       |
| Index          | None — filesystem is source of truth                                      |
| Library tree   | `notes` folder hidden in `buildLibraryEntries` / folder tree              |
| Picker data    | `NotesPaths::listNoteBookIndices()` — books where `folder == "notes"`     |
| Legacy migrate | `/notes/poc.txt` → first free `Note N.txt` on upgrade (`notes_paths.cpp`) |
| New files      | `Note 1.txt`, `Note 2.txt`, … first free integer                          |

### Entry and navigation

| Topic        | Decision                                                                                                          |
| ------------ | ----------------------------------------------------------------------------------------------------------------- |
| Library      | Single **Notes** row → note picker (always, even one note)                                                        |
| Picker       | **`+ New note`** first row; bookmark-style buttons (1× next, long prev, 2× act, 3× library)                       |
| Read vs edit | **Read** = paginated view, BLE off, reader gestures. **Edit** = BLE session on enter, typing when connected       |
| Size limit   | 6000 chars in RAM for edit; save writes that prefix; read can show full file; edit blocked in read menu if larger |

**Implemented navigation (differs slightly from early picker→read sketch):**

- Picker **2×** on an existing note → **edit** (not read).
- **Read** is reached from edit’s BLE device menu (**“Read on device”**, row 0) or after leaving edit to read-only; read menu (**MENU** hold) offers **Edit** when size allows.
- Picker **2×** on **`+ New note`** → create file → edit.
- Edit **long hold** (exit) → picker; **3×** → library and `endSession()`.
- Picker **3×** → library (no BLE session on picker).

There is no separate library **New note** row; creation is only via the picker’s first row.

### BLE session and keyboards

| Topic           | Decision                                                                                                                                                        |
| --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Session start   | `wifiEnd()` + `beginSession()` on `NotesEditScreen` enter (deferred one idle tick)                                                                              |
| Session stop    | **3×** in notes flow → library + `requestEndSession()`; sleep → `endSession()`                                                                                  |
| Quick reconnect | On session init: bonded `savedKb[0]` → auto-pick and connect when seen; else scan                                                                               |
| Layout          | Per **saved keyboard** in `ble_kb_lst` (`addr`, `addrType`, `name`, `layout`); full-screen pick on first connect (`ble_keyboard_layout.cpp` + `hidUsageToUtf8`) |
| Forget          | Device menu last row **“Clear pairings”** → `forgetPeer()` (bonds, list, layouts). **3×** is exit home, not forget                                              |
| Connecting      | Long hold while connecting → `cancelConnect()` (unchanged)                                                                                                      |

### Read screen UX

- Reuses reader paging (`g_bookview`, `renderCurrentPage`, `reader_actions.h` holds).
- Read menu shows title, page/%, keyboard status (session may be off), **Edit** (2×) with size guard + toast.

---

## What shipped (module map)

```text
Pala_One_2_1/src/
  hal/
    ble_keyboard.{h,cpp}           # session, scan, connect, NVS, worker (~1.3k lines, not split yet)
    ble_keyboard_hid.cpp           # delegates to layout
    ble_keyboard_layout.{h,cpp}  # US / UK / DE / FR …
    ble_keyboard_internal.h
  storage/
    notes_paths.{h,cpp}          # dir, migrate, create, list indices
  ui/
    notes_document.{h,cpp}       # open(path), 6k cap, key apply
    notes_ble_ui.{h,cpp}         # BLE panel + device menu drawing
    notes_read_menu.{h,cpp}
    text.{h,cpp}                 # collectWrappedLines (+ edit variant)
  ui/screens/
    notes_picker_screen.{h,cpp}
    notes_read_screen.{h,cpp}
    notes_edit_screen.{h,cpp}    # replaced notes_screen
```

Earlier extractions (still in place): HID out of main BLE file, shared line wrapping, BLE UI separated from edit screen logic.

---

## Optional follow-up

1. **Split `ble_keyboard.cpp`** into prefs / scan / connect (and keep HID/layout as today) — refactor only, no behavior change.
2. **Docs:** README / GETTING_STARTED — note files live under `/books/notes`, not `/notes`.

---

## Non-goals (unchanged)

- Cloud sync, markdown, delete-note UI in v1
- Notes as normal library books / opening notes in `ReaderScreen`
- Per-note keyboard layout (layout is per **device**)
- **3×** to forget keyboards
