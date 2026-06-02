#ifndef PALA_UI_NOTES_DOCUMENT_H
#define PALA_UI_NOTES_DOCUMENT_H

#include <Arduino.h>
#include "src/hal/ble_keyboard.h"

namespace NotesDocument
{

constexpr const char *kDefaultPath = "/notes/poc.txt";
constexpr size_t kMaxChars = 6000;

void load();
bool save();
const String &text();
void applyKey(const BleKeyboard::KeyEvent &ev);

} // namespace NotesDocument

#endif
