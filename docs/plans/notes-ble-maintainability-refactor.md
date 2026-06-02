# Notes + BLE keyboard maintainability refactor

**Status:** Approved design — ready for implementation  
**Audience:** Implementing agent  
**Repo:** `pala-one-firmware` / firmware root `Pala_One_2_1/`  
**Do not** change behavior unless noted; prefer small PR-sized commits per step below.

## Context

Two large files drove this plan:

| File                                           | Lines | Problem                                                                                      |
| ---------------------------------------------- | ----- | -------------------------------------------------------------------------------------------- |
| `Pala_One_2_1/src/hal/ble_keyboard.cpp`        | ~1255 | HID, GATT, scan, connect FSM, NVS peers, worker task, and **localized UI strings** in one TU |
| `Pala_One_2_1/src/ui/screens/notes_screen.cpp` | ~534  | Note I/O, ad-hoc UTF-8 wrap, BLE pairing UI, redraw throttle, screen hooks                   |

**Only consumer of `BleKeyboard` today:** `notes_screen.cpp` (grep confirms). Still treat HAL as generic for future screens.

**Build:** PlatformIO `src_dir = Pala_One_2_1` compiles all `Pala_One_2_1/src/**/*.cpp` automatically — new `.cpp` files need no `platformio.ini` change. Arduino IDE uses same tree.

**Includes:** Always `#include "src/..."` from sketch root (see `platformio.ini` / README).

---

## Target architecture

```
Pala_One_2_1/src/
  hal/
    ble_keyboard.h              # public API (updated)
    ble_keyboard.cpp            # session, scan, connect, prefs, worker, GATT subscribe
    ble_keyboard_hid.cpp        # HID report decode → key queue (step 5)
    ble_keyboard_internal.h     # optional: shared between ble_keyboard*.cpp (step 5)

  ui/
    text.h / text.cpp           # + collectWrappedLines()
    notes_document.h / .cpp     # note buffer + FS
    notes_ble_ui.h / .cpp       # pairing/scan UI view-model + draw helpers

  ui/screens/
    notes_screen.cpp            # thin Screen: lifecycle, drawBody, onIdleTick, buttons
```

---

## Implementation order (mandatory)

Complete **one step per commit** (or PR). Run `pio run -e wireless-paper-v1_2-en` (or project default env) after each step.

| Step | Title                          | Depends on       |
| ---- | ------------------------------ | ---------------- |
| 1    | Generic BLE HAL API            | —                |
| 2    | Paginator-backed note wrapping | —                |
| 3    | `notes_document` module        | —                |
| 4    | `notes_ble_ui` module          | Step 1           |
| 5    | `ble_keyboard_hid.cpp` split   | Steps 1–4 stable |

---

## Step 1 — Generic BLE HAL API

### 1.1 Update `Pala_One_2_1/src/hal/ble_keyboard.h`

**Add** to namespace `BleKeyboard` (public):

```cpp
enum class PairingHint : uint8_t {
  None = 0,
  TypeOnKeyboard,   // user types passkey on keyboard
  ConfirmOnKeyboard // user confirms PIN on keyboard
};

PairingHint pairingHint();
const char* lastAdvertisedName();  // empty string if none; never localized
```

**Remove** declarations:

- `const char* linkStateLabel();`
- `const char* statusSubline();`

**Keep unchanged:** `KeyAction`, `KeyEvent`, `LinkState`, session functions, candidate API, `pairingCode()`, `popEvent`, `cancelConnect()` (see step 4 note).

### 1.2 Update `ble_keyboard.cpp`

- `PairingHint` is today **private** inside anonymous namespace (~line 94–100). Move enum to header; keep `s_pairingHint` static in `.cpp`.
- Implement:

```cpp
PairingHint pairingHint() { return s_pairingHint; }
const char* lastAdvertisedName() { return s_lastAdvName; }
```

- **Delete** entire functions `linkStateLabel()` and `statusSubline()` (~lines 1201–1241).
- **Remove** all `D_NOTES_*` usage from this file (today pulled in via `config.h` → `lang.h`). After step 1, `ble_keyboard.cpp` must not reference any `D_NOTES_*` macro.

### 1.3 Update `notes_screen.cpp` (minimal fix in step 1)

Only caller of `statusSubline()`:

**File:** `resolveBleView()`, `LinkState::Connecting` branch (~line 249–260).

Replace:

```cpp
copyLine(v.detail, sizeof(v.detail), BleKeyboard::statusSubline());
```

With:

```cpp
switch (BleKeyboard::pairingHint()) {
  case BleKeyboard::PairingHint::TypeOnKeyboard:
    copyLine(v.detail, sizeof(v.detail), D_NOTES_PIN_ENTER);
    break;
  case BleKeyboard::PairingHint::ConfirmOnKeyboard:
    copyLine(v.detail, sizeof(v.detail), D_NOTES_PIN_MATCH);
    break;
  default:
    v.detail[0] = '\0';
    break;
}
```

**Optional in step 1 (scan UX):** In `Scanning` / `Failed` branches where `!hasCandidate()`, if `BleKeyboard::lastAdvertisedName()[0]` show that name as `v.detail` instead of only `D_NOTES_PAIR_MODE`. Product choice: prefer `lastAdvertisedName()` when non-empty, else `D_NOTES_PAIR_MODE`.

### 1.4 Verify step 1

- [ ] Grep: no `statusSubline`, no `linkStateLabel`, no `D_NOTES_` in `ble_keyboard.cpp`
- [ ] Build succeeds
- [ ] Notes screen: pairing PIN lines still show `D_NOTES_PIN_ENTER` / `D_NOTES_PIN_MATCH`

### 1.5 Dead code note

`linkStateLabel()` had **zero call sites** before removal — safe delete.

`D_NOTES_STATUS_*` macros in `en.h` / `es_la.h` become unused after step 1 — **do not delete** lang strings in this refactor unless you grep and confirm zero references.

---

## Step 2 — Paginator-backed wrapping (`collectWrappedLines`)

### 2.1 Background

**Ebooks** wrap via `paginatePage()` in `Pala_One_2_1/src/pure/paginator.cpp`:

- Stream: `FileReadStream` / `IReadStream`
- Width: `MeasureFn` → `Font::measureBionicLine` (see `bodyMeasure` in `text.cpp`)
- UTF-8-safe hard breaks, punctuation/whitespace rules in `text_util.h`

**Notes** today use local `wrapLines()` in `notes_screen.cpp` (~lines 67–117): O(n²) `substring` + `u8g2.getUTF8Width`.

**In-memory stream already exists:** `StringReadStream` in `Pala_One_2_1/src/pure/stream.h`.

### 2.2 Add API — `Pala_One_2_1/src/ui/text.h`

```cpp
// Wrap in-memory UTF-8 text using the same rules as reader pagination (body font).
// Fills out[0..count-1]. Stops at cap lines or end of text.
// Caller must Font::useBody() before draw if lines were measured with body face.
int collectWrappedLines(const String& text, int maxWidthPx, String* out, int cap);
```

### 2.3 Implement in `Pala_One_2_1/src/ui/text.cpp`

- Reuse existing static `bodyMeasure` (already calls `Font::measureBionicLine`).
- Pattern:

```cpp
int collectWrappedLines(const String& text, int maxWidthPx, String* out, int cap) {
  if (!out || cap <= 0) return 0;
  StringReadStream stream(text);
  LayoutMetrics m = Font::bodyLayout();
  m.maxWidth = maxWidthPx;
  m.maxLines = cap;  // paginator stops after cap lines
  Font::useBody();
  int count = 0;
  paginatePage(stream, 0, m, bodyMeasure, [&](const char* buf, size_t len) {
    if (count < cap)
      out[count++] = String(buf, (unsigned)len);
  });
  return count;
}
```

**Important:** `Font::bodyLayout().maxWidth` defaults to full reader width; **override** `m.maxWidth` with notes panel width (`SCREEN_W - 2 * MARGIN_X`), same as current `drawBody`.

**Do not** use reader `TOP_PAD` / statusbar layout for notes — only share **wrap algorithm**, not `bodyLayout().maxLines` for page size.

### 2.4 Change `notes_screen.cpp` `drawBody`

- Remove static `wrapLines()`.
- `#include "src/ui/text.h"`.
- Replace wrap call:

```cpp
static String lines[24];  // or constexpr kMaxVisibleLines = 24
int lineCount = collectWrappedLines(s_text, maxW, lines, 24);
```

- Keep **unchanged:** `menuLineH()` for vertical spacing, tail scroll `first = max(0, lineCount - maxLines)`, BLE cursor `_` logic (~lines 139–152).

### 2.5 Verify step 2

- [ ] Long unbroken token wraps (paginator hard-break) vs old char-at-a-time behavior
- [ ] Explicit newlines in note still break lines
- [ ] Word wrap at spaces still works
- [ ] Typing redraw still throttled (`kNoteRedrawMs = 120`)

---

## Step 3 — `notes_document` module

### 3.1 Create `Pala_One_2_1/src/ui/notes_document.h`

```cpp
#ifndef PALA_UI_NOTES_DOCUMENT_H
#define PALA_UI_NOTES_DOCUMENT_H

#include <Arduino.h>
#include "src/hal/ble_keyboard.h"

namespace NotesDocument {

constexpr const char* kDefaultPath = "/notes/poc.txt";
constexpr size_t kMaxChars = 6000;

void load();
bool save();
const String& text();
void applyKey(const BleKeyboard::KeyEvent& ev);

}  // namespace NotesDocument

#endif
```

### 3.2 Create `Pala_One_2_1/src/ui/notes_document.cpp`

- Move verbatim logic from `notes_screen.cpp`:
  - `kNotePath` → `kDefaultPath`
  - `kMaxChars`, static `String s_text`
  - `loadNote` → `load()`, `saveNote` → `save()`, `applyKey`
- `save()` calls `ensureNotesDir()` from `src/storage/fs_util.h` before open write.

### 3.3 Update `notes_screen.cpp`

- Remove `s_text`, `loadNote`, `saveNote`, `applyKey`, constants.
- `onEnter`: `NotesDocument::load()`
- `onSleep` / `leaveNotesToLibrary`: `NotesDocument::save()`
- `onIdleTick`: `NotesDocument::applyKey(ev)`; `drawBody` uses `NotesDocument::text()`

### 3.4 Verify step 3

- [ ] Note persists across exit/sleep at `/notes/poc.txt`
- [ ] 6000 char cap enforced on input

---

## Step 4 — `notes_ble_ui` module

### 4.1 Create `Pala_One_2_1/src/ui/notes_ble_ui.h`

Export:

```cpp
struct NotesBleView {
  char headline[48];
  char detail[48];
  char action[40];
  char pin[8];
  char header[24];
  bool showNote;
  bool boldHeadline;
  int deviceIndex;
  int deviceCount;
};

// deferBleBegin: true while notes screen waiting to call BleKeyboard::beginSession()
void notesBleResolveView(NotesBleView& out, bool deferBleBegin);
const char* notesBleHeaderStatus(bool deferBleBegin);

void notesBleResetSnapshot();
bool notesBleSnapshotChanged(bool deferBleBegin);  // updates internal snap, returns dirty

void notesBleDrawPanel(int topY, const NotesBleView& view);
```

### 4.2 Move from `notes_screen.cpp` anonymous namespace

| Symbol                                               | Destination                            |
| ---------------------------------------------------- | -------------------------------------- |
| `NotesBleView`                                       | `notes_ble_ui.h`                       |
| `copyLine`                                           | static in `notes_ble_ui.cpp`           |
| `notesHeaderStatus`                                  | `notesBleHeaderStatus`                 |
| `resolveBleView`                                     | `notesBleResolveView`                  |
| `resetUiSnapshot` / `uiSnapshotChanged` / `s_uiSnap` | `notes_ble_ui.cpp`                     |
| `drawBleLine`, `drawBottomHint`, `drawPairingPanel`  | `notesBleDrawPanel` (+ static helpers) |

**Keep in `notes_screen.cpp`:**

- `s_deferBleBegin` (screen owns BLE session deferral)
- `requestBleDraw` / `flushBleDraw` (or pass `Screen*` if you want to drop `g_notesScreen.draw()` coupling — optional cleanup)
- `drawBody`, `NotesScreen::*` methods, `leaveNotesToLibrary`

### 4.3 Deduplicate `resolveBleView`

**Scanning** (~222–246) and **Failed** (~263–288) blocks are nearly identical. Extract:

```cpp
static void fillCandidatePickerView(NotesBleView& v, bool failedState);
```

- When `BleKeyboard::hasCandidate()`: set headline, deviceCount/Index, detail/action like today.
- When no candidate: `D_NOTES_PAIR_MODE`; if `failedState` also set `D_NOTES_FAILED_HINT` on action.

### 4.4 `cancelConnect()` wiring (recommended)

Today `D_NOTES_ACTION_CANCEL` shows during Connecting but **no button calls** `BleKeyboard::cancelConnect()` — long hold calls `leaveNotesToLibrary()` only.

In `NotesScreen::onButton`, when `linkState() == Connecting` and `isExitHold(e)`:

- Call `BleKeyboard::cancelConnect()` instead of (or before) full exit — **product decision:**
  - **Recommended:** Long hold while Connecting → `cancelConnect()` only; long hold otherwise → `leaveNotesToLibrary()`.

Document choice in commit message.

### 4.5 Verify step 4

- [ ] Scan / connect / pair / fail UI matches pre-refactor screenshots or serial logs
- [ ] `uiSnapshotChanged` still limits BLE redraws (~400 ms)
- [ ] Button map unchanged: short=cycle, double=connect, triple=forget

---

## Step 5 — Split `ble_keyboard_hid.cpp`

### 5.1 Move to `Pala_One_2_1/src/hal/ble_keyboard_hid.cpp`

From anonymous namespace in `ble_keyboard.cpp`:

| Symbol                          | Approx lines |
| ------------------------------- | ------------ |
| `hidToAscii`                    | 175–220      |
| `s_prevKeys`, `wasInPrevReport` | 222–230      |
| `handleBootReport`              | 232–283      |
| `handleHidReport`               | 285–309      |
| `notifyThunk`                   | 311–320      |

**Stay in `ble_keyboard.cpp`:**

- `findHidService`, `subscribeHidReports`, `ensureBleSecurity`, scan/connect/worker/NVS, public API
- `enqueue`, `s_keyQueue`, `KeyEvent` queue — HID path must call `enqueue`

### 5.2 Shared internal header (recommended)

`Pala_One_2_1/src/hal/ble_keyboard_internal.h` (include only from `ble_keyboard*.cpp`):

```cpp
namespace BleKeyboard::Internal {
  void enqueue(KeyAction action, char ch = 0);
  void clearPrevKeys();  // called on connect start (today memset s_prevKeys)
}
```

- `ble_keyboard_hid.cpp` includes internal header + `ble_keyboard.h` if needed.
- `connect` path calls `Internal::clearPrevKeys()` instead of touching `s_prevKeys` from main file.

### 5.3 Verify step 5

- [ ] HID typing: chars, backspace, enter
- [ ] Connect + subscribe still works
- [ ] No new warnings from cppcheck if run

---

## Public API reference (final `ble_keyboard.h`)

```cpp
namespace BleKeyboard {

enum class KeyAction : uint8_t { None, Char, Backspace, Newline };
struct KeyEvent { KeyAction action; char ch; };

enum class LinkState : uint8_t { Off, Scanning, Connecting, Connected, Failed };
enum class PairingHint : uint8_t { None, TypeOnKeyboard, ConfirmOnKeyboard };

void beginSession();
void requestEndSession();
void endSession();
void cancelConnect();
void forgetPeer();
void loop();

bool hasCandidate();
int deviceCount();
int selectedIndex();
const char* candidateName();
void cycleCandidate();
void connectCandidate();

LinkState linkState();
const char* pairingCode();
PairingHint pairingHint();
const char* lastAdvertisedName();

bool popEvent(KeyEvent& out);
bool isSessionActive();
}
```

Header comment block (~lines 6–9) remains accurate; remove notes-specific string mentions if any.

---

## `notes_screen.cpp` target shape (~150–200 lines)

```cpp
// includes: notes_screen.h, notes_document.h, notes_ble_ui.h, text.h, ble_keyboard, display, input, wifi, library_screen, widgets, font

void NotesScreen::draw() {
  NotesBleView view;
  notesBleResolveView(view, s_deferBleBegin);
  prepareMenuFrame();
  int y = drawSectionHeader(D_NOTES_HEADER, notesBleHeaderStatus(s_deferBleBegin));
  if (view.showNote) drawBody(y);
  else notesBleDrawPanel(y, view);
  display.update();
}

void NotesScreen::onIdleTick() {
  // defer begin → BleKeyboard::beginSession()
  BleKeyboard::loop();
  drain keys → NotesDocument::applyKey
  notesBleSnapshotChanged → throttled draw
  key dirty → throttled drawBody
}
```

---

## Testing checklist (manual, device)

1. Library → Notes: WiFi off, BLE session starts after first frame.
2. Scan: keyboards appear; 1× cycle, 2× connect, 3× forget bonds.
3. Pairing: PIN shows correct hint strings (EN + ES env if available).
4. Connected: type, backspace, newline; `_` cursor; hold exit saves file.
5. Re-enter Notes: text restored from `/notes/poc.txt`.
6. Sleep from Notes: `endSession()` + save.
7. Long note / long word: wrap matches reader-ish breaks (no overflow off screen).

---

## Explicit non-goals

- Multi-note file picker / note list UI
- New `text_layout.cpp` parallel to paginator
- Deleting unused `D_NOTES_STATUS_*` lang macros (optional cleanup)
- Changing scan scoring, timeouts, or NVS key names
- Unit tests unless repo already has host tests for paginator (optional: add host test for `collectWrappedLines` later)

---

## File touch summary

| Action             | Path                                                                       |
| ------------------ | -------------------------------------------------------------------------- |
| Edit               | `src/hal/ble_keyboard.h`, `src/hal/ble_keyboard.cpp`                       |
| Create             | `src/hal/ble_keyboard_hid.cpp`, `src/hal/ble_keyboard_internal.h` (step 5) |
| Edit               | `src/ui/text.h`, `src/ui/text.cpp`                                         |
| Create             | `src/ui/notes_document.h`, `src/ui/notes_document.cpp`                     |
| Create             | `src/ui/notes_ble_ui.h`, `src/ui/notes_ble_ui.cpp`                         |
| Edit               | `src/ui/screens/notes_screen.cpp`                                          |
| No change expected | `platformio.ini`, `Pala_One_2_1.ino`, `library_screen.cpp`                 |

---

## Reference: button / BLE UX contract

Documented in `ble_keyboard.h` comment — preserve:

- **1× short:** `cycleCandidate()`
- **2× double:** `connectCandidate()` if `hasCandidate()`
- **3× triple:** `forgetPeer()`
- **Long / very long:** exit notes (and optionally `cancelConnect` while Connecting — step 4)

WiFi must be off before `beginSession()` — `notes_screen` already calls `wifiEnd()` in `onEnter`.
