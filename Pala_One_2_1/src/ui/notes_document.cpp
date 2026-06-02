#include "src/ui/notes_document.h"

#include "src/storage/fs_util.h"

namespace NotesDocument
{

static String s_text;

void load()
{
  s_text = "";
  if (!FS.exists(kDefaultPath))
    return;
  File f = FS.open(kDefaultPath, "r");
  if (!f)
    return;
  while (f.available() && s_text.length() < kMaxChars)
    s_text += (char)f.read();
  f.close();
}

bool save()
{
  ensureNotesDir();
  File f = FS.open(kDefaultPath, "w");
  if (!f)
    return false;
  f.print(s_text);
  f.close();
  return true;
}

const String &text() { return s_text; }

void applyKey(const BleKeyboard::KeyEvent &ev)
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

} // namespace NotesDocument
