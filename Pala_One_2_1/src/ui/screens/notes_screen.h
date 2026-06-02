#ifndef PALA_UI_SCREENS_NOTES_SCREEN_H
#define PALA_UI_SCREENS_NOTES_SCREEN_H

#include "src/ui/screen.h"

class NotesScreen : public Screen
{
public:
  void onEnter() override;
  void onButton(const ButtonEvent &e) override;
  void onIdleTick() override;
  void draw() override;
  void onSleep() override;
  bool allowSleep() const override { return false; }
};

extern NotesScreen g_notesScreen;

#endif // PALA_UI_SCREENS_NOTES_SCREEN_H
