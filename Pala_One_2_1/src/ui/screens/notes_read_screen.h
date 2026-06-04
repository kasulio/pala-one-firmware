#ifndef PALA_UI_SCREENS_NOTES_READ_SCREEN_H
#define PALA_UI_SCREENS_NOTES_READ_SCREEN_H

#include "src/ui/screen.h"

class NotesReadScreen : public Screen
{
public:
  void onEnter() override;
  void onButton(const ButtonEvent &e) override;
  void draw() override;
  void onSleep() override;
};

extern NotesReadScreen g_notesReadScreen;

#endif
