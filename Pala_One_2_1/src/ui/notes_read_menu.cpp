#include "src/ui/notes_read_menu.h"

#include "src/hal/ble_keyboard.h"
#include "src/hal/display.h"
#include "src/pure/paths.h"
#include "src/storage/notes_paths.h"
#include "src/ui/font.h"
#include "src/ui/notes_document.h"
#include "src/ui/reader.h"
#include "src/ui/toast.h"
#include "src/ui/widgets.h"

namespace NotesReadMenu
{

namespace
{

static bool s_active = false;
static bool s_editRequested = false;

static const char *keyboardStatusLine()
{
  if (!BleKeyboard::isSessionActive())
    return D_NOTES_MENU_BT_OFF;
  switch (BleKeyboard::linkState())
  {
  case BleKeyboard::LinkState::Connected:
    return D_NOTES_MENU_BT_OK;
  case BleKeyboard::LinkState::Connecting:
    return D_NOTES_MENU_BT_CONNECT;
  case BleKeyboard::LinkState::Scanning:
    return D_NOTES_MENU_BT_SCAN;
  default:
    return D_NOTES_MENU_BT_FAIL;
  }
}

} // namespace

bool isActive() { return s_active; }

void open()
{
  s_active = true;
  s_editRequested = false;
  forceNextMenuFrameFull();
  draw();
}

void close() { s_active = false; }

bool consumeEditRequest()
{
  if (!s_editRequested)
    return false;
  s_editRequested = false;
  return true;
}

void draw()
{
  prepareMenuFrame();
  Font::useBody();
  int ascent = u8g2.getFontAscent();
  int lineH = (ascent - u8g2.getFontDescent()) + Font::currentLineGap() + 1;
  int y = drawSectionHeader(D_NOTES_READ_MENU_TITLE);

  String title = stripTxtExt(lastPathComponent(g_bookview.book.path()));
  Font::useBold();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(title.c_str());
  y += lineH;
  Font::useBody();

  size_t total = g_bookview.book.size();
  if (total == 0)
    total = 1;
  uint32_t pos = g_bookview.pages.offsets[g_bookview.cursor.pageIndex];
  int pct = (int)((pos * 100UL) / (uint32_t)total);

  char buf[64];
  if (g_bookview.pages.eofReached && g_bookview.pages.count > 0)
  {
    snprintf(buf, sizeof(buf), "Page %d of %d  (%d%%)",
             g_bookview.cursor.pageIndex + 1, g_bookview.pages.count, pct);
  }
  else
  {
    snprintf(buf, sizeof(buf), "Page %d  (%d%% of note)",
             g_bookview.cursor.pageIndex + 1, pct);
  }
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(buf);
  y += lineH * 2;

  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(keyboardStatusLine());
  y += lineH;

  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(D_NOTES_MENU_EDIT);

  u8g2.setCursor(MARGIN_X, SCREEN_H - 2);
  u8g2.print("2x: edit  3x: close");

  display.update();
}

bool onButton(const ButtonEvent &e)
{
  if (!s_active || !e.any())
    return false;

  if (e.kind == ButtonEvent::Triple)
  {
    close();
    return true;
  }

  if (e.kind == ButtonEvent::Double)
  {
    const size_t sz = NotesPaths::currentFileSize();
    if (sz > NotesDocument::kMaxEditBytes)
    {
      Toast::show(D_NOTES_EDIT_TOO_LARGE);
      close();
      return true;
    }
    s_editRequested = true;
    NotesDocument::open(NotesPaths::currentPath());
    close();
    return true;
  }

  close();
  return true;
}

} // namespace NotesReadMenu
