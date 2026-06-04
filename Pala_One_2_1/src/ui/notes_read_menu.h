#ifndef PALA_UI_NOTES_READ_MENU_H
#define PALA_UI_NOTES_READ_MENU_H

#include "src/hal/input.h"

namespace NotesReadMenu
{

bool isActive();
void open();
void close();
void draw();
bool onButton(const ButtonEvent &e);

// Set when user confirms Edit from the menu; read screen consumes once.
bool consumeEditRequest();

} // namespace NotesReadMenu

#endif
