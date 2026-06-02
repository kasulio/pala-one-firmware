#include "src/ui/screens/notes_screen.h"

#include <WiFi.h>

#include "src/hal/ble_keyboard.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/wifi.h"
#include "src/storage/fs_util.h"
#include "src/state.h"
#include "src/ui/font.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/widgets.h"

static constexpr const char *kNotePath = "/notes/poc.txt";
static constexpr size_t kMaxChars = 6000;

static String s_text;

static void loadNote()
{
  s_text = "";
  if (!FS.exists(kNotePath))
    return;
  File f = FS.open(kNotePath, "r");
  if (!f)
    return;
  while (f.available() && s_text.length() < kMaxChars)
  {
    s_text += (char)f.read();
  }
  f.close();
}

static bool saveNote()
{
  ensureNotesDir();
  File f = FS.open(kNotePath, "w");
  if (!f)
    return false;
  f.print(s_text);
  f.close();
  return true;
}

static void applyKey(const BleKeyboard::KeyEvent &ev)
{
  switch (ev.action)
  {
  case BleKeyboard::KeyAction::Char:
    if (s_text.length() < kMaxChars)
      s_text += ev.ch;
    break;
  case BleKeyboard::KeyAction::Backspace:
    if (s_text.length() > 0)
      s_text.remove(s_text.length() - 1);
    break;
  case BleKeyboard::KeyAction::Newline:
    if (s_text.length() < kMaxChars)
      s_text += '\n';
    break;
  default:
    break;
  }
}

static void wrapLines(const String &text, int maxWidth, String *lines, int &count, int cap)
{
  count = 0;
  if (text.length() == 0)
    return;

  int lineStart = 0;
  while (lineStart < (int)text.length() && count < cap)
  {
    int lineEnd = lineStart;
    int lastBreak = -1;
    while (lineEnd < (int)text.length())
    {
      if (text[lineEnd] == '\n')
      {
        lines[count] = text.substring(lineStart, lineEnd);
        count++;
        lineStart = lineEnd + 1;
        lastBreak = -2;
        break;
      }
      String probe = text.substring(lineStart, lineEnd + 1);
      if (u8g2.getUTF8Width(probe.c_str()) > maxWidth)
        break;
      if (text[lineEnd] == ' ')
        lastBreak = lineEnd;
      lineEnd++;
    }
    if (lastBreak == -2)
      continue;
    if (lineEnd >= (int)text.length())
    {
      lines[count++] = text.substring(lineStart);
      break;
    }
    if (lastBreak >= lineStart)
    {
      lines[count++] = text.substring(lineStart, lastBreak);
      lineStart = lastBreak + 1;
      while (lineStart < (int)text.length() && text[lineStart] == ' ')
        lineStart++;
    }
    else
    {
      if (lineEnd == lineStart)
        lineEnd++;
      lines[count++] = text.substring(lineStart, lineEnd);
      lineStart = lineEnd;
    }
  }
}

static void drawBody(int topY)
{
  const int maxW = SCREEN_W - (MARGIN_X * 2);
  const int lineH = menuLineH();
  const int maxLines = max(1, (SCREEN_H - topY - 6) / lineH);

  static String lines[24];
  int lineCount = 0;
  wrapLines(s_text, maxW, lines, lineCount, 24);

  Font::useBody();
  int first = max(0, lineCount - maxLines);
  int y = topY;
  for (int i = first; i < lineCount; i++)
  {
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(lines[i].c_str());
    y += lineH;
  }

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connected)
  {
    bool endsWithNewline = s_text.length() > 0 && s_text[s_text.length() - 1] == '\n';
    if (lineCount > 0 && !endsWithNewline)
    {
      int xOff = u8g2.getUTF8Width(lines[lineCount - 1].c_str());
      u8g2.setCursor(MARGIN_X + xOff, y - lineH);
    }
    else
    {
      u8g2.setCursor(MARGIN_X, y);
    }
    u8g2.print("_");
  }
}

namespace
{

  bool s_deferBleBegin = false;

  struct NotesBleView
  {
    char headline[48] = "";
    char detail[48] = "";
    char action[40] = "";
    char pin[8] = "";
    char header[24] = "";
    bool showNote = false;
    bool boldHeadline = false;
    int deviceIndex = 0;
    int deviceCount = 0;
  };

  static NotesBleView s_uiSnap;

  static void copyLine(char *dst, size_t cap, const char *src)
  {
    if (!src)
    {
      dst[0] = '\0';
      return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
  }

  static const char *notesHeaderStatus()
  {
    if (s_deferBleBegin)
      return D_NOTES_HDR_START;

    switch (BleKeyboard::linkState())
    {
    case BleKeyboard::LinkState::Scanning:
      return D_NOTES_HDR_SCAN;
    case BleKeyboard::LinkState::Connecting:
      return BleKeyboard::pairingCode()[0] ? D_NOTES_HDR_PAIR : D_NOTES_HDR_CONNECT;
    case BleKeyboard::LinkState::Connected:
      return D_NOTES_HDR_OK;
    case BleKeyboard::LinkState::Failed:
      return D_NOTES_HDR_FAIL;
    default:
      return D_NOTES_HDR_SCAN;
    }
  }

  static void resolveBleView(NotesBleView &v)
  {
    v = {};
    if (s_deferBleBegin)
    {
      copyLine(v.detail, sizeof(v.detail), D_NOTES_BLE_START);
      return;
    }

    const BleKeyboard::LinkState st = BleKeyboard::linkState();
    if (st == BleKeyboard::LinkState::Connected)
    {
      v.showNote = true;
      return;
    }

    if (st == BleKeyboard::LinkState::Scanning)
    {
      if (BleKeyboard::hasCandidate())
      {
        v.deviceCount = BleKeyboard::deviceCount();
        v.deviceIndex = BleKeyboard::selectedIndex();
        copyLine(v.headline, sizeof(v.headline), BleKeyboard::candidateName());
        v.boldHeadline = true;
        if (v.deviceCount > 1)
        {
          char slot[32];
          snprintf(slot, sizeof(slot), D_NOTES_DEVICE_OF, v.deviceIndex + 1, v.deviceCount);
          copyLine(v.detail, sizeof(v.detail), slot);
          copyLine(v.action, sizeof(v.action), D_NOTES_ACTION_NEXT);
        }
        else
        {
          copyLine(v.detail, sizeof(v.detail), D_NOTES_ACTION_CONNECT);
        }
      }
      else
      {
        copyLine(v.detail, sizeof(v.detail), D_NOTES_PAIR_MODE);
      }
      return;
    }

    if (st == BleKeyboard::LinkState::Connecting)
    {
      copyLine(v.pin, sizeof(v.pin), BleKeyboard::pairingCode());
      copyLine(v.action, sizeof(v.action), D_NOTES_ACTION_CANCEL);
      if (v.pin[0] != '\0')
      {
        copyLine(v.detail, sizeof(v.detail), BleKeyboard::statusSubline());
        return;
      }
      copyLine(v.headline, sizeof(v.headline), BleKeyboard::candidateName());
      v.boldHeadline = true;
      return;
    }

    if (st == BleKeyboard::LinkState::Failed)
    {
      if (BleKeyboard::hasCandidate())
      {
        v.deviceCount = BleKeyboard::deviceCount();
        v.deviceIndex = BleKeyboard::selectedIndex();
        copyLine(v.headline, sizeof(v.headline), BleKeyboard::candidateName());
        v.boldHeadline = true;
        if (v.deviceCount > 1)
        {
          char slot[32];
          snprintf(slot, sizeof(slot), D_NOTES_DEVICE_OF, v.deviceIndex + 1, v.deviceCount);
          copyLine(v.detail, sizeof(v.detail), slot);
          copyLine(v.action, sizeof(v.action), D_NOTES_ACTION_NEXT);
        }
        else
        {
          copyLine(v.detail, sizeof(v.detail), D_NOTES_ACTION_CONNECT);
        }
      }
      else
      {
        copyLine(v.detail, sizeof(v.detail), D_NOTES_PAIR_MODE);
        copyLine(v.action, sizeof(v.action), D_NOTES_FAILED_HINT);
      }
      return;
    }
  }

  static void resetUiSnapshot()
  {
    s_uiSnap = {};
  }

  static bool uiSnapshotChanged()
  {
    NotesBleView now;
    resolveBleView(now);
    copyLine(now.header, sizeof(now.header), notesHeaderStatus());
    bool changed = strcmp(now.headline, s_uiSnap.headline) != 0 ||
                   strcmp(now.detail, s_uiSnap.detail) != 0 || strcmp(now.action, s_uiSnap.action) != 0 ||
                   strcmp(now.pin, s_uiSnap.pin) != 0 || strcmp(now.header, s_uiSnap.header) != 0 ||
                   now.showNote != s_uiSnap.showNote || now.boldHeadline != s_uiSnap.boldHeadline ||
                   now.deviceIndex != s_uiSnap.deviceIndex || now.deviceCount != s_uiSnap.deviceCount;
    if (changed)
      s_uiSnap = now;
    return changed;
  }

  static void drawBleLine(int &y, const char *text, bool bold)
  {
    if (!text || text[0] == '\0')
      return;
    if (bold)
      Font::useBold();
    else
      Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(text);
    Font::useBody();
    y += menuLineH();
  }

  static void drawBottomHint(const char *text)
  {
    if (!text || text[0] == '\0')
      return;
    Font::useBody();
    u8g2.setCursor(MARGIN_X, SCREEN_H - 6);
    u8g2.print(text);
  }

  static void drawPairingPanel(int topY, const NotesBleView &view)
  {
    int y = topY;

    if (view.boldHeadline && view.headline[0] != '\0')
      drawBleLine(y, view.headline, true);

    if (view.pin[0] != '\0')
    {
      Font::useBold();
      int w = u8g2.getUTF8Width(view.pin);
      int pinY = topY + max(menuLineH(), (SCREEN_H - topY - menuLineH() * 2) / 2);
      u8g2.setCursor((SCREEN_W - w) / 2, pinY);
      u8g2.print(view.pin);
      Font::useBody();
      if (view.detail[0] != '\0')
      {
        u8g2.setCursor(MARGIN_X, pinY + menuLineH() + 4);
        u8g2.print(view.detail);
      }
      drawBottomHint(view.action);
      return;
    }

    if (view.detail[0] != '\0')
      drawBleLine(y, view.detail, false);

    if (view.deviceCount > 1 && view.pin[0] == '\0')
      drawBleLine(y, D_NOTES_ACTION_CONNECT, false);

    drawBottomHint(view.action);
  }

  static bool isExitHold(const ButtonEvent &e)
  {
    return e.kind == ButtonEvent::Long || e.kind == ButtonEvent::VeryLong;
  }

  static void leaveNotesToLibrary()
  {
    saveNote();
    s_deferBleBegin = false;
    BleKeyboard::requestEndSession();
    resetInputFrontend();
    g_notesScreen.nextScreen = &g_libraryScreen;
  }

  static void requestBleDraw(uint32_t now, uint32_t &bleRedrawAt, bool &bleDirty)
  {
    bleDirty = true;
    if (now >= bleRedrawAt)
    {
      g_notesScreen.draw();
      bleRedrawAt = now + 400;
      bleDirty = false;
    }
  }

  static void flushBleDraw(uint32_t now, uint32_t &bleRedrawAt, bool &bleDirty)
  {
    if (!bleDirty || now < bleRedrawAt)
      return;
    g_notesScreen.draw();
    bleRedrawAt = now + 400;
    bleDirty = false;
  }

} // namespace

void NotesScreen::draw()
{
  NotesBleView view;
  resolveBleView(view);

  prepareMenuFrame();
  int y = drawSectionHeader(D_NOTES_HEADER, notesHeaderStatus());

  if (view.showNote)
    drawBody(y);
  else
    drawPairingPanel(y, view);

  display.update();
}

void NotesScreen::onEnter()
{
  if (WiFi.getMode() != WIFI_OFF)
    wifiEnd();
  loadNote();
  resetUiSnapshot();
  s_deferBleBegin = true;
  forceNextMenuFrameFull();
  draw();
}

void NotesScreen::onIdleTick()
{
  static uint32_t s_noteRedrawAt = 0;
  static uint32_t s_bleRedrawAt = 0;
  static bool s_noteDirty = false;
  static bool s_bleDirty = false;
  static constexpr uint32_t kNoteRedrawMs = 120;

  if (s_deferBleBegin)
  {
    s_deferBleBegin = false;
    BleKeyboard::beginSession();
    uiSnapshotChanged();
    draw();
    const uint32_t t = millis();
    s_noteRedrawAt = t + kNoteRedrawMs;
    s_bleRedrawAt = t + 400;
    s_noteDirty = false;
    s_bleDirty = false;
  }

  BleKeyboard::loop();

  bool keyChanged = false;
  BleKeyboard::KeyEvent ev;
  while (BleKeyboard::popEvent(ev))
  {
    applyKey(ev);
    keyChanged = true;
  }

  const bool bleUiChanged = uiSnapshotChanged();
  const uint32_t now = millis();

  if (bleUiChanged)
    requestBleDraw(now, s_bleRedrawAt, s_bleDirty);

  flushBleDraw(now, s_bleRedrawAt, s_bleDirty);

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

void NotesScreen::onButton(const ButtonEvent &e)
{
  if (!e.any())
    return;

  if (isExitHold(e))
  {
    leaveNotesToLibrary();
    return;
  }

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connected)
    return;

  if (e.kind == ButtonEvent::Short)
  {
    BleKeyboard::cycleCandidate();
    draw();
    return;
  }
  if (e.kind == ButtonEvent::Double)
  {
    if (BleKeyboard::hasCandidate())
      BleKeyboard::connectCandidate();
    draw();
    return;
  }
  if (e.kind == ButtonEvent::Triple)
  {
    BleKeyboard::forgetPeer();
    draw();
    return;
  }
}

void NotesScreen::onSleep()
{
  saveNote();
  s_deferBleBegin = false;
  BleKeyboard::endSession();
}

NotesScreen g_notesScreen;
