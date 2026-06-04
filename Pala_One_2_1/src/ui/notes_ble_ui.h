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
  bool forgetAllRow = false;
};

void notesBleResolveView(NotesBleView &out, bool deferBleBegin, bool forgetAllRow = false);
const char *notesBleHeaderStatus(bool deferBleBegin);

void notesBleResetSnapshot();
bool notesBleSnapshotChanged(bool deferBleBegin, bool forgetAllRow = false);

void notesBleDrawPanel(int topY, const NotesBleView &view);

bool notesBleShowsDeviceMenu(const NotesBleView &view, bool deferBleBegin);

int notesBleMenuRowCount();
void notesBleMenuRowLabel(int row, char *buf, size_t cap, int connectingRow);
const char *notesBleMenuBottomHint();
void notesBleDrawDeviceMenu(int topY, int selectedRow, int connectingRow);

#endif
