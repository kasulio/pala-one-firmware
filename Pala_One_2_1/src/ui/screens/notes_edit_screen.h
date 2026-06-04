#ifndef PALA_UI_SCREENS_NOTES_EDIT_SCREEN_H
#define PALA_UI_SCREENS_NOTES_EDIT_SCREEN_H

#include "src/ui/screen.h"

class NotesEditScreen : public Screen
{
public:
  void onEnter() override;
  void onButton(const ButtonEvent &e) override;
  void onIdleTick() override;
  void draw() override;
  void onSleep() override;
  bool allowSleep() const override { return false; }
};

extern NotesEditScreen g_notesEditScreen;

#endif
