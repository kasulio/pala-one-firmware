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

static void fillCandidatePickerView(NotesBleView &v, bool failedState, bool forgetAllRow)
{
  if (BleKeyboard::hasCandidate())
  {
    v.deviceCount = BleKeyboard::deviceCount();
    v.deviceIndex = BleKeyboard::selectedIndex();
    copyLine(v.headline, sizeof(v.headline), BleKeyboard::candidateName());
    v.boldHeadline = true;
    if (forgetAllRow)
    {
      copyLine(v.detail, sizeof(v.detail), D_NOTES_REMOVE_ALL);
      copyLine(v.action, sizeof(v.action), D_NOTES_REMOVE_CONFIRM);
      return;
    }
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

enum class BleMenuLineKind : uint8_t
{
  Gap,
  Row,
};

struct BleMenuLine
{
  BleMenuLineKind kind = BleMenuLineKind::Gap;
  int selIndex = -1;
};

static int deviceRowCount()
{
  return BleKeyboard::savedKeyboardCount() + BleKeyboard::extraScannedCount();
}

static void buildMenuLines(BleMenuLine *lines, int &count)
{
  const int devices = deviceRowCount();
  const int forgetRow = notesBleMenuRowCount() - 1;
  int n = 0;

  for (int i = 0; i < devices; i++)
    lines[n++] = {BleMenuLineKind::Row, i};
  if (devices > 0)
    lines[n++] = {BleMenuLineKind::Gap, -1};
  lines[n++] = {BleMenuLineKind::Row, forgetRow};
  count = n;
}

static int lineHeightUnits(const BleMenuLine &line, int lineH, int gapH)
{
  return line.kind == BleMenuLineKind::Gap ? gapH : lineH;
}

} // namespace

void notesBleResolveView(NotesBleView &v, bool deferBleBegin, bool forgetAllRow)
{
  v = {};
  v.forgetAllRow = forgetAllRow;
  if (deferBleBegin)
  {
    copyLine(v.detail, sizeof(v.detail), D_NOTES_BLE_START);
    return;
  }

  const BleKeyboard::LinkState st = BleKeyboard::linkState();
  if (st == BleKeyboard::LinkState::Connected)
  {
    if (!BleKeyboard::needsLayoutPick())
      v.showNote = true;
    return;
  }

  if (st == BleKeyboard::LinkState::Scanning)
  {
    fillCandidatePickerView(v, false, forgetAllRow);
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
    return;
  }

  if (st == BleKeyboard::LinkState::Failed)
    fillCandidatePickerView(v, true, forgetAllRow);
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

bool notesBleSnapshotChanged(bool deferBleBegin, bool forgetAllRow)
{
  NotesBleView now;
  notesBleResolveView(now, deferBleBegin, forgetAllRow);
  {
    const char *hdr = notesBleHeaderStatus(deferBleBegin);
    copyLine(now.header, sizeof(now.header), hdr ? hdr : "");
  }
  bool changed = strcmp(now.headline, s_uiSnap.headline) != 0 ||
                 strcmp(now.detail, s_uiSnap.detail) != 0 || strcmp(now.action, s_uiSnap.action) != 0 ||
                 strcmp(now.pin, s_uiSnap.pin) != 0 || strcmp(now.header, s_uiSnap.header) != 0 ||
                 now.showNote != s_uiSnap.showNote || now.boldHeadline != s_uiSnap.boldHeadline ||
                 now.deviceIndex != s_uiSnap.deviceIndex || now.deviceCount != s_uiSnap.deviceCount ||
                 now.forgetAllRow != s_uiSnap.forgetAllRow;
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

  if (view.forgetAllRow && view.headline[0] != '\0')
    y += menuLineH();

  if (view.detail[0] != '\0')
    drawBleLine(y, view.detail, false);

  if (view.deviceCount > 1 && view.pin[0] == '\0' && !view.forgetAllRow)
    drawBleLine(y, D_NOTES_ACTION_CONNECT, false);

  drawBottomHint(view.action);
}

bool notesBleShowsDeviceMenu(const NotesBleView &view, bool deferBleBegin)
{
  if (view.showNote || deferBleBegin || view.pin[0] != '\0')
    return false;
  const BleKeyboard::LinkState st = BleKeyboard::linkState();
  return st == BleKeyboard::LinkState::Scanning || st == BleKeyboard::LinkState::Failed ||
         st == BleKeyboard::LinkState::Connecting;
}

int notesBleMenuRowCount()
{
  return deviceRowCount() + 1;
}

void notesBleMenuRowLabel(int row, char *buf, size_t cap, int connectingRow)
{
  if (!buf || cap == 0)
    return;
  buf[0] = '\0';

  if (row < 0)
    return;

  const int devices = deviceRowCount();
  const int forgetRow = notesBleMenuRowCount() - 1;
  const char *base = "";

  if (row < devices)
  {
    const int saved = BleKeyboard::savedKeyboardCount();
    if (row < saved)
      base = BleKeyboard::savedKeyboardLabel(row);
    else
      base = BleKeyboard::extraScannedLabel(row - saved);
  }
  else if (row == forgetRow)
  {
    base = D_NOTES_BLE_FORGET;
  }

  if (!base[0])
    return;

  if (row == connectingRow && row < forgetRow)
    snprintf(buf, cap, "%s%s", base, D_NOTES_BLE_CONNECTING);
  else
    strncpy(buf, base, cap - 1);
  buf[cap - 1] = '\0';
}

const char *notesBleMenuBottomHint()
{
  if (BleKeyboard::pairingCode()[0] != '\0')
    return nullptr;

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Connecting)
    return D_NOTES_ACTION_CANCEL;

  if (BleKeyboard::isScanInProgress())
  {
    if (deviceRowCount() == 0)
      return D_NOTES_BLE_SEARCHING;
    return D_NOTES_PAIR_MODE;
  }

  if (BleKeyboard::linkState() == BleKeyboard::LinkState::Failed && deviceRowCount() == 0)
    return D_NOTES_FAILED_HINT;

  if (deviceRowCount() > 0)
    return D_NOTES_BLE_MENU_CONNECT;

  return D_NOTES_PAIR_MODE;
}

void notesBleDrawDeviceMenu(int topY, int selectedRow, int connectingRow)
{
  BleMenuLine lines[16];
  int lineCount = 0;
  buildMenuLines(lines, lineCount);
  if (lineCount <= 0)
    return;

  const int lineH = menuLineH();
  const int gapH = max(4, lineH / 2);

  int selectedLine = 0;
  for (int i = 0; i < lineCount; i++)
  {
    if (lines[i].kind == BleMenuLineKind::Row && lines[i].selIndex == selectedRow)
    {
      selectedLine = i;
      break;
    }
  }

  int descent = u8g2.getFontDescent();
  int available = SCREEN_H - BOT_PAD + descent - topY;

  int top = selectedLine;
  int usedAbove = 0;
  for (int i = selectedLine - 1; i >= 0; i--)
  {
    int h = lineHeightUnits(lines[i], lineH, gapH);
    if (usedAbove + h > available / 2)
      break;
    usedAbove += h;
    top = i;
  }

  int y = topY;
  for (int i = top; i < lineCount; i++)
  {
    const int h = lineHeightUnits(lines[i], lineH, gapH);
    if (y + h > SCREEN_H - BOT_PAD + descent)
      break;

    if (lines[i].kind == BleMenuLineKind::Gap)
    {
      y += gapH;
      continue;
    }

    const int sel = lines[i].selIndex;
    char label[72];
    notesBleMenuRowLabel(sel, label, sizeof(label), connectingRow);
    drawMenuRow(y, label, sel == selectedRow);
    y += lineH;
  }

  drawBottomHint(notesBleMenuBottomHint());
}
