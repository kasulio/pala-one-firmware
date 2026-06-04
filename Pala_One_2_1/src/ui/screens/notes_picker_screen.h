#ifndef PALA_UI_SCREENS_NOTES_PICKER_SCREEN_H
#define PALA_UI_SCREENS_NOTES_PICKER_SCREEN_H

#include "src/ui/screen.h"

class NotesPickerScreen : public Screen
{
public:
  void onEnter() override;
  void onButton(const ButtonEvent &e) override;
  void draw() override;
};

extern NotesPickerScreen g_notesPickerScreen;

#endif
