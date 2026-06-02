#ifndef PALA_UI_NOTES_BLE_UI_H
#define PALA_UI_NOTES_BLE_UI_H

#include <Arduino.h>

struct NotesBleView
{
  char headline[48] = "";
  char detail[48] = "";
  char action[40] = "";
  char pin[8] = "";
  char header[24] = "";
  bool showNote = false;
  bool boldHeadline = false;
  int deviceIndex = 0;
  int deviceCount = 0;
};

void notesBleResolveView(NotesBleView &out, bool deferBleBegin);
const char *notesBleHeaderStatus(bool deferBleBegin);

void notesBleResetSnapshot();
bool notesBleSnapshotChanged(bool deferBleBegin);

void notesBleDrawPanel(int topY, const NotesBleView &view);

#endif
