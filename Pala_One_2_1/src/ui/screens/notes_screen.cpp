#include "src/ui/screens/notes_screen.h"

#include <WiFi.h>

#include "src/hal/ble_keyboard.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/wifi.h"
#include "src/state.h"
#include "src/ui/font.h"
#include "src/ui/notes_ble_ui.h"
#include "src/ui/notes_document.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/text.h"
#include "src/ui/widgets.h"

namespace
{

static constexpr int kMaxVisibleLines = 24;
static bool s_deferBleBegin = false;

static void drawBody(int topY)
{
  const int maxW = SCREEN_W - (MARGIN_X * 2);
  const int lineH = menuLineH();
  const int maxLines = max(1, (SCREEN_H - topY - 6) / lineH);

  static String lines[kMaxVisibleLines];
  const String &text = NotesDocument::text();
  int lineCount = collectWrappedLines(text, maxW, lines, kMaxVisibleLines);

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
    bool endsWithNewline = text.length() > 0 && text[text.length() - 1] == '\n';
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

static bool isExitHold(const ButtonEvent &e)
{
  return e.kind == ButtonEvent::Long || e.kind == ButtonEvent::VeryLong;
}

static void leaveNotesToLibrary()
{
  NotesDocument::save();
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
  notesBleResolveView(view, s_deferBleBegin);

  prepareMenuFrame();
  int y = drawSectionHeader(D_NOTES_HEADER, notesBleHeaderStatus(s_deferBleBegin));

  if (view.showNote)
    drawBody(y);
  else
    notesBleDrawPanel(y, view);

  display.update();
}

void NotesScreen::onEnter()
{
  if (WiFi.getMode() != WIFI_OFF)
    wifiEnd();
  NotesDocument::load();
  notesBleResetSnapshot();
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
    notesBleSnapshotChanged(false);
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
    NotesDocument::applyKey(ev);
    keyChanged = true;
  }

  const bool bleUiChanged = notesBleSnapshotChanged(false);
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
    if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connecting)
    {
      BleKeyboard::cancelConnect();
      draw();
      return;
    }
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
  NotesDocument::save();
  s_deferBleBegin = false;
  BleKeyboard::endSession();
}

NotesScreen g_notesScreen;
