#include "src/ui/notes_document.h"

#include "src/config.h"
#include "src/storage/fs_util.h"
#include "src/storage/notes_paths.h"
#include "src/state.h"
#include "src/ui/text.h"

namespace NotesDocument
{

static String s_text;
static char s_path[NotesPaths::kMaxNotePath] = "";
static size_t s_caret = 0;

static void insertAtCaret(const char *utf8, uint8_t len)
{
  if (!utf8 || len == 0 || s_text.length() + len > kMaxChars)
    return;
  s_text = s_text.substring(0, s_caret) + String(utf8, len) + s_text.substring(s_caret);
  s_caret += len;
}

static void deleteBeforeCaret()
{
  if (s_caret == 0)
    return;
  size_t start = s_caret - 1;
  while (start > 0 && ((uint8_t)s_text[start] & 0xC0) == 0x80)
    start--;
  const size_t removeLen = s_caret - start;
  s_text.remove(start, removeLen);
  s_caret = start;
}

void open(const char *path)
{
  if (path)
  {
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    NotesPaths::setCurrentPath(s_path);
  }
  else
  {
    s_path[0] = '\0';
  }
  load();
}

void load()
{
  s_text = "";
  const char *p = s_path[0] ? s_path : NotesPaths::currentPath();
  if (!p || !p[0] || !FS.exists(p))
  {
    s_caret = 0;
    return;
  }
  File f = FS.open(p, "r");
  if (!f)
  {
    s_caret = 0;
    return;
  }
  while (f.available() && s_text.length() < kMaxChars)
    s_text += (char)f.read();
  f.close();
  s_caret = s_text.length();
}

bool save()
{
  const char *p = s_path[0] ? s_path : NotesPaths::currentPath();
  if (!p || !p[0])
    return false;
  ensureDirRecursive(NotesPaths::kNotesDirAbs);
  File f = FS.open(p, "w");
  if (!f)
    return false;
  f.print(s_text);
  f.close();
  return true;
}

const char *path() { return s_path[0] ? s_path : NotesPaths::currentPath(); }

const String &text() { return s_text; }

size_t caret() { return s_caret; }

void applyKey(const BleKeyboard::KeyEvent &ev)
{
  switch (ev.action)
  {
  case BleKeyboard::KeyAction::Char:
    insertAtCaret(ev.utf8, ev.utf8Len);
    break;
  case BleKeyboard::KeyAction::Backspace:
    deleteBeforeCaret();
    break;
  case BleKeyboard::KeyAction::Newline:
    if (s_text.length() < kMaxChars)
      insertAtCaret("\n", 1);
    break;
  case BleKeyboard::KeyAction::Left:
    if (s_caret > 0)
      s_caret--;
    break;
  case BleKeyboard::KeyAction::Right:
    if (s_caret < s_text.length())
      s_caret++;
    break;
  case BleKeyboard::KeyAction::Up:
    s_caret = notesMoveCaretVertical(s_text, SCREEN_W - (MARGIN_X * 2), s_caret, -1);
    break;
  case BleKeyboard::KeyAction::Down:
    s_caret = notesMoveCaretVertical(s_text, SCREEN_W - (MARGIN_X * 2), s_caret, 1);
    break;
  default:
    break;
  }
}

} // namespace NotesDocument
