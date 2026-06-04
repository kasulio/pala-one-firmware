#include "src/ui/screens/notes_edit_screen.h"

#include <WiFi.h>

#include "src/hal/ble_keyboard.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/wifi.h"
#include "src/storage/library.h"
#include "src/ui/font.h"
#include "src/ui/notes_ble_ui.h"
#include "src/ui/notes_document.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/screens/notes_picker_screen.h"
#include "src/ui/text.h"
#include "src/ui/widgets.h"

namespace
{

static constexpr int kMaxWrapLines = 256;
static constexpr uint32_t kMenuListRedrawMs = 500;
static bool s_deferBleBegin = false;
static bool s_bleStartingUi = false;
static uint32_t s_bleStartMs = 0;
static int s_bleMenuCursor = 0;
static int s_bleConnectingRow = -1;
static int s_layoutPickIndex = 0;
static bool s_layoutPickActive = false;

static const char *kLayoutLabels[] = {
    D_NOTES_LAYOUT_US,
    D_NOTES_LAYOUT_UK,
    D_NOTES_LAYOUT_DE,
    D_NOTES_LAYOUT_FR,
};
static constexpr int kLayoutCount = 4;

static BleKeyboard::KeyboardLayout layoutAt(int idx)
{
  switch (idx)
  {
  case 0:
    return BleKeyboard::KeyboardLayout::US;
  case 1:
    return BleKeyboard::KeyboardLayout::UK;
  case 2:
    return BleKeyboard::KeyboardLayout::DE;
  default:
    return BleKeyboard::KeyboardLayout::FR;
  }
}

static void drawBody(int topY)
{
  const int maxW = SCREEN_W - (MARGIN_X * 2);
  const int lineH = menuLineH();
  const int maxLines = max(1, (SCREEN_H - topY - 6) / lineH);

  static String lines[kMaxWrapLines];
  static uint32_t lineStarts[kMaxWrapLines];
  const String &text = NotesDocument::text();
  const int lineCount = notesBuildWrapped(text, maxW, lines, lineStarts, kMaxWrapLines);

  int caretLine = 0;
  int caretColPx = 0;
  if (lineCount > 0)
    notesMapCaret(text, maxW, NotesDocument::caret(), lineStarts, lineCount,
                  &caretLine, &caretColPx);

  int first = caretLine - maxLines + 1;
  if (first < 0)
    first = 0;
  if (first > lineCount - maxLines)
    first = max(0, lineCount - maxLines);

  Font::useBody();
  int y = topY;
  const int lastDrawn = min(lineCount, first + maxLines);
  for (int i = first; i < lastDrawn; i++)
  {
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(lines[i].c_str());
    y += lineH;
  }

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connected &&
      !BleKeyboard::needsLayoutPick() && lineCount > 0 &&
      caretLine >= first && caretLine < lastDrawn)
  {
    const int cx = MARGIN_X + caretColPx;
    const int cy = topY + (caretLine - first) * lineH;
    constexpr int kCaretBarW = 6;
    gfx.drawFastHLine(cx, cy + 1, kCaretBarW, 1);
  }
}

static void drawLayoutPick()
{
  prepareMenuFrame();
  int y = drawSectionHeader(D_NOTES_LAYOUT_HEADER);
  drawScrollableList(y, kLayoutCount, s_layoutPickIndex,
                     [](int idx, int rowY, bool selected, int /*budget*/) {
                       drawMenuRow(rowY, kLayoutLabels[idx], selected);
                       return 1;
                     });
  display.update();
}

static bool isExitHold(const ButtonEvent &e)
{
  return e.kind == ButtonEvent::Long || e.kind == ButtonEvent::VeryLong;
}

static void resetBleMenuState()
{
  s_bleMenuCursor = 0;
  s_bleConnectingRow = -1;
}

static void leaveNotesToPicker()
{
  NotesDocument::save();
  loadBooks();
  s_deferBleBegin = false;
  resetBleMenuState();
  s_layoutPickActive = false;
  BleKeyboard::requestEndSession();
  resetInputFrontend();
  g_notesEditScreen.nextScreen = &g_notesPickerScreen;
}

static void leaveNotesToLibrary()
{
  NotesDocument::save();
  s_deferBleBegin = false;
  resetBleMenuState();
  s_layoutPickActive = false;
  BleKeyboard::requestEndSession();
  resetInputFrontend();
  g_notesEditScreen.nextScreen = &g_libraryScreen;
}

static int bleMenuForgetRow() { return notesBleMenuRowCount() - 1; }

static void activateBleMenuRow(int row)
{
  const int devices = bleMenuForgetRow();
  if (row < 0 || row > devices)
    return;

  if (row == devices)
  {
    BleKeyboard::forgetPeer();
    resetBleMenuState();
    return;
  }

  const int saved = BleKeyboard::savedKeyboardCount();
  if (row < saved)
    BleKeyboard::pickSavedKeyboard(row);
  else
    BleKeyboard::pickExtraScanned(row - saved);

  s_bleConnectingRow = row;
  BleKeyboard::connectCandidate();
}

} // namespace

void NotesEditScreen::draw()
{
  if (s_layoutPickActive || BleKeyboard::needsLayoutPick())
  {
    s_layoutPickActive = true;
    drawLayoutPick();
    return;
  }

  NotesBleView view;
  notesBleResolveView(view, s_deferBleBegin || s_bleStartingUi, false);

  prepareMenuFrame();
  const char *hdr = s_bleStartingUi ? D_NOTES_HDR_START : notesBleHeaderStatus(s_deferBleBegin);
  int y = drawSectionHeader(D_NOTES_HEADER, hdr);

  if (view.showNote)
    drawBody(y);
  else if (s_bleStartingUi)
    notesBleDrawPanel(y, view);
  else if (notesBleShowsDeviceMenu(view, s_deferBleBegin))
  {
    const int rows = notesBleMenuRowCount();
    if (s_bleMenuCursor >= rows)
      s_bleMenuCursor = max(0, rows - 1);
    const int connectingRow =
        BleKeyboard::linkState() == BleKeyboard::LinkState::Connecting ? s_bleConnectingRow : -1;
    notesBleDrawDeviceMenu(y, s_bleMenuCursor, connectingRow);
  }
  else
    notesBleDrawPanel(y, view);

  display.update();
}

void NotesEditScreen::onEnter()
{
  if (WiFi.getMode() != WIFI_OFF)
    wifiEnd();
  NotesDocument::load();
  notesBleResetSnapshot();
  s_deferBleBegin = true;
  s_bleStartingUi = false;
  resetBleMenuState();
  s_layoutPickActive = false;
  s_layoutPickIndex = 0;
  forceNextMenuFrameFull();
  draw();
}

void NotesEditScreen::onIdleTick()
{
  static uint32_t s_noteRedrawAt = 0;
  static bool s_noteDirty = false;
  static constexpr uint32_t kNoteRedrawMs = 120;

  static BleKeyboard::LinkState s_prevLink = BleKeyboard::LinkState::Off;
  static bool s_prevNeedsLayout = false;
  static bool s_prevScanning = false;
  static char s_prevPin[8] = "";
  static int s_prevListVer = -1;
  static uint32_t s_menuRedrawAt = 0;

  NotesBleView view;
  if (s_deferBleBegin)
  {
    s_deferBleBegin = false;
    s_bleStartingUi = true;
    s_bleStartMs = millis();
    BleKeyboard::beginSession();
    draw();
    const uint32_t t = millis();
    s_noteRedrawAt = t + kNoteRedrawMs;
    s_noteDirty = false;
    s_menuRedrawAt = t;
    s_prevListVer = BleKeyboard::deviceListVersion();
    s_prevLink = BleKeyboard::linkState();
    s_prevScanning = BleKeyboard::isScanInProgress();
    strncpy(s_prevPin, BleKeyboard::pairingCode(), sizeof(s_prevPin) - 1);
    s_prevPin[sizeof(s_prevPin) - 1] = '\0';
  }

  BleKeyboard::loop();

  if (s_bleStartingUi)
  {
    if (BleKeyboard::isScanInProgress() || BleKeyboard::deviceListVersion() > 0 ||
        (uint32_t)(millis() - s_bleStartMs) > 2500)
    {
      s_bleStartingUi = false;
      draw();
      s_menuRedrawAt = millis();
    }
  }

  const BleKeyboard::LinkState link = BleKeyboard::linkState();
  const bool needsLayout = BleKeyboard::needsLayoutPick();
  const bool scanning = BleKeyboard::isScanInProgress();
  const int listVer = BleKeyboard::deviceListVersion();
  const char *pin = BleKeyboard::pairingCode();

  if (needsLayout)
    s_layoutPickActive = true;

  const bool linkChanged = link != s_prevLink;
  const bool pinChanged = strcmp(pin, s_prevPin) != 0;
  const bool scanChanged = scanning != s_prevScanning;
  const bool listChanged = listVer != s_prevListVer;

  if (linkChanged)
  {
    if (link == BleKeyboard::LinkState::Connected ||
        link == BleKeyboard::LinkState::Scanning || link == BleKeyboard::LinkState::Failed)
      s_bleConnectingRow = -1;
    s_prevLink = link;
  }
  if (pinChanged)
  {
    strncpy(s_prevPin, pin, sizeof(s_prevPin) - 1);
    s_prevPin[sizeof(s_prevPin) - 1] = '\0';
  }
  if (scanChanged)
    s_prevScanning = scanning;
  if (listChanged)
    s_prevListVer = listVer;

  const uint32_t now = millis();
  notesBleResolveView(view, false, false);
  const bool onMenu = !s_bleStartingUi && notesBleShowsDeviceMenu(view, false);

  const bool urgentUi = linkChanged || pinChanged || needsLayout != s_prevNeedsLayout;
  const bool menuRefresh =
      onMenu && (urgentUi || scanChanged ||
                 (listChanged && (uint32_t)(now - s_menuRedrawAt) >= kMenuListRedrawMs));

  if (urgentUi || menuRefresh)
  {
    if (needsLayout != s_prevNeedsLayout)
    {
      s_prevNeedsLayout = needsLayout;
      if (s_layoutPickActive && !needsLayout)
        s_layoutPickActive = false;
    }
    draw();
    s_menuRedrawAt = now;
  }

  if (s_layoutPickActive && needsLayout)
    return;

  bool keyChanged = false;
  BleKeyboard::KeyEvent ev;
  while (BleKeyboard::popEvent(ev))
  {
    NotesDocument::applyKey(ev);
    keyChanged = true;
  }

  if (keyChanged)
  {
    if (now >= s_noteRedrawAt)
    {
      draw();
      s_noteRedrawAt = now + kNoteRedrawMs;
      s_noteDirty = false;
    }
    else
    {
      s_noteDirty = true;
    }
  }
  else if (s_noteDirty && now >= s_noteRedrawAt)
  {
    draw();
    s_noteRedrawAt = now + kNoteRedrawMs;
    s_noteDirty = false;
  }
}

void NotesEditScreen::onButton(const ButtonEvent &e)
{
  if (!e.any())
    return;

  if (s_layoutPickActive || BleKeyboard::needsLayoutPick())
  {
    if (e.kind == ButtonEvent::Triple)
    {
      leaveNotesToLibrary();
      return;
    }
    if (e.kind == ButtonEvent::Short)
    {
      s_layoutPickIndex = (s_layoutPickIndex + 1) % kLayoutCount;
      draw();
      return;
    }
    if (e.kind == ButtonEvent::Long)
    {
      s_layoutPickIndex--;
      if (s_layoutPickIndex < 0)
        s_layoutPickIndex = kLayoutCount - 1;
      draw();
      return;
    }
    if (e.kind == ButtonEvent::Double)
    {
      BleKeyboard::setKeyboardLayout(layoutAt(s_layoutPickIndex));
      s_layoutPickActive = false;
      draw();
      return;
    }
    return;
  }

  if (isExitHold(e))
  {
    if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connecting)
    {
      BleKeyboard::cancelConnect();
      s_bleConnectingRow = -1;
      draw();
      return;
    }
    leaveNotesToPicker();
    return;
  }

  if (e.kind == ButtonEvent::Triple)
  {
    leaveNotesToLibrary();
    return;
  }

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connected)
    return;

  NotesBleView view;
  notesBleResolveView(view, false, false);
  if (!notesBleShowsDeviceMenu(view, false))
  {
    if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connecting &&
        e.kind == ButtonEvent::Double)
    {
      BleKeyboard::cancelConnect();
      s_bleConnectingRow = -1;
      draw();
    }
    return;
  }

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connecting)
  {
    if (e.kind == ButtonEvent::Double)
    {
      BleKeyboard::cancelConnect();
      s_bleConnectingRow = -1;
      draw();
    }
    return;
  }

  const int rows = notesBleMenuRowCount();
  if (rows <= 0)
    return;

  if (e.kind == ButtonEvent::Short)
  {
    s_bleMenuCursor = (s_bleMenuCursor + 1) % rows;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Long)
  {
    s_bleMenuCursor--;
    if (s_bleMenuCursor < 0)
      s_bleMenuCursor = rows - 1;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double)
  {
    if (s_bleMenuCursor < bleMenuForgetRow())
      activateBleMenuRow(s_bleMenuCursor);
    else
      activateBleMenuRow(bleMenuForgetRow());
    draw();
    return;
  }
}

void NotesEditScreen::onSleep()
{
  NotesDocument::save();
  s_deferBleBegin = false;
  s_bleStartingUi = false;
  resetBleMenuState();
  s_layoutPickActive = false;
  BleKeyboard::endSession();
}

NotesEditScreen g_notesEditScreen;
