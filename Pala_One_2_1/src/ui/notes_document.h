#ifndef PALA_UI_NOTES_DOCUMENT_H
#define PALA_UI_NOTES_DOCUMENT_H

#include <Arduino.h>
#include "src/hal/ble_keyboard.h"

namespace NotesDocument
{

constexpr size_t kMaxChars = 6000;
constexpr size_t kMaxEditBytes = 6000;

void open(const char *path);
void load();
bool save();
const char *path();
const String &text();
size_t caret();
void applyKey(const BleKeyboard::KeyEvent &ev);

} // namespace NotesDocument

#endif
