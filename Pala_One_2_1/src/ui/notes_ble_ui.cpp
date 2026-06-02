#include "src/ui/notes_ble_ui.h"

#include <string.h>

#include "src/hal/ble_keyboard.h"
#include "src/hal/display.h"
#include "src/ui/font.h"
#include "src/ui/widgets.h"

namespace
{

static NotesBleView s_uiSnap;

static void copyLine(char *dst, size_t cap, const char *src)
{
  if (!src)
  {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

static void fillCandidatePickerView(NotesBleView &v, bool failedState)
{
  if (BleKeyboard::hasCandidate())
  {
    v.deviceCount = BleKeyboard::deviceCount();
    v.deviceIndex = BleKeyboard::selectedIndex();
    copyLine(v.headline, sizeof(v.headline), BleKeyboard::candidateName());
    v.boldHeadline = true;
    if (v.deviceCount > 1)
    {
      char slot[32];
      snprintf(slot, sizeof(slot), D_NOTES_DEVICE_OF, v.deviceIndex + 1, v.deviceCount);
      copyLine(v.detail, sizeof(v.detail), slot);
      copyLine(v.action, sizeof(v.action), D_NOTES_ACTION_NEXT);
    }
    else
    {
      copyLine(v.detail, sizeof(v.detail), D_NOTES_ACTION_CONNECT);
    }
    return;
  }

  const char *adv = BleKeyboard::lastAdvertisedName();
  if (adv[0])
    copyLine(v.detail, sizeof(v.detail), adv);
  else
    copyLine(v.detail, sizeof(v.detail), D_NOTES_PAIR_MODE);

  if (failedState)
    copyLine(v.action, sizeof(v.action), D_NOTES_FAILED_HINT);
}

static void drawBleLine(int &y, const char *text, bool bold)
{
  if (!text || text[0] == '\0')
    return;
  if (bold)
    Font::useBold();
  else
    Font::useBody();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(text);
  Font::useBody();
  y += menuLineH();
}

static void drawBottomHint(const char *text)
{
  if (!text || text[0] == '\0')
    return;
  Font::useBody();
  u8g2.setCursor(MARGIN_X, SCREEN_H - 6);
  u8g2.print(text);
}

} // namespace

void notesBleResolveView(NotesBleView &v, bool deferBleBegin)
{
  v = {};
  if (deferBleBegin)
  {
    copyLine(v.detail, sizeof(v.detail), D_NOTES_BLE_START);
    return;
  }

  const BleKeyboard::LinkState st = BleKeyboard::linkState();
  if (st == BleKeyboard::LinkState::Connected)
  {
    v.showNote = true;
    return;
  }

  if (st == BleKeyboard::LinkState::Scanning)
  {
    fillCandidatePickerView(v, false);
    return;
  }

  if (st == BleKeyboard::LinkState::Connecting)
  {
    copyLine(v.pin, sizeof(v.pin), BleKeyboard::pairingCode());
    copyLine(v.action, sizeof(v.action), D_NOTES_ACTION_CANCEL);
    if (v.pin[0] != '\0')
    {
      switch (BleKeyboard::pairingHint())
      {
      case BleKeyboard::PairingHint::TypeOnKeyboard:
        copyLine(v.detail, sizeof(v.detail), D_NOTES_PIN_ENTER);
        break;
      case BleKeyboard::PairingHint::ConfirmOnKeyboard:
        copyLine(v.detail, sizeof(v.detail), D_NOTES_PIN_MATCH);
        break;
      default:
        v.detail[0] = '\0';
        break;
      }
      return;
    }
    copyLine(v.headline, sizeof(v.headline), BleKeyboard::candidateName());
    v.boldHeadline = true;
    return;
  }

  if (st == BleKeyboard::LinkState::Failed)
    fillCandidatePickerView(v, true);
}

const char *notesBleHeaderStatus(bool deferBleBegin)
{
  if (deferBleBegin)
    return D_NOTES_HDR_START;

  switch (BleKeyboard::linkState())
  {
  case BleKeyboard::LinkState::Scanning:
    return D_NOTES_HDR_SCAN;
  case BleKeyboard::LinkState::Connecting:
    return BleKeyboard::pairingCode()[0] ? D_NOTES_HDR_PAIR : D_NOTES_HDR_CONNECT;
  case BleKeyboard::LinkState::Connected:
    return D_NOTES_HDR_OK;
  case BleKeyboard::LinkState::Failed:
    return D_NOTES_HDR_FAIL;
  default:
    return D_NOTES_HDR_SCAN;
  }
}

void notesBleResetSnapshot() { s_uiSnap = {}; }

bool notesBleSnapshotChanged(bool deferBleBegin)
{
  NotesBleView now;
  notesBleResolveView(now, deferBleBegin);
  copyLine(now.header, sizeof(now.header), notesBleHeaderStatus(deferBleBegin));
  bool changed = strcmp(now.headline, s_uiSnap.headline) != 0 ||
                 strcmp(now.detail, s_uiSnap.detail) != 0 || strcmp(now.action, s_uiSnap.action) != 0 ||
                 strcmp(now.pin, s_uiSnap.pin) != 0 || strcmp(now.header, s_uiSnap.header) != 0 ||
                 now.showNote != s_uiSnap.showNote || now.boldHeadline != s_uiSnap.boldHeadline ||
                 now.deviceIndex != s_uiSnap.deviceIndex || now.deviceCount != s_uiSnap.deviceCount;
  if (changed)
    s_uiSnap = now;
  return changed;
}

void notesBleDrawPanel(int topY, const NotesBleView &view)
{
  int y = topY;

  if (view.boldHeadline && view.headline[0] != '\0')
    drawBleLine(y, view.headline, true);

  if (view.pin[0] != '\0')
  {
    Font::useBold();
    int w = u8g2.getUTF8Width(view.pin);
    int pinY = topY + max(menuLineH(), (SCREEN_H - topY - menuLineH() * 2) / 2);
    u8g2.setCursor((SCREEN_W - w) / 2, pinY);
    u8g2.print(view.pin);
    Font::useBody();
    if (view.detail[0] != '\0')
    {
      u8g2.setCursor(MARGIN_X, pinY + menuLineH() + 4);
      u8g2.print(view.detail);
    }
    drawBottomHint(view.action);
    return;
  }

  if (view.detail[0] != '\0')
    drawBleLine(y, view.detail, false);

  if (view.deviceCount > 1 && view.pin[0] == '\0')
    drawBleLine(y, D_NOTES_ACTION_CONNECT, false);

  drawBottomHint(view.action);
}
