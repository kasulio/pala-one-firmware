#include "src/hal/ble_keyboard_internal.h"

#include <BLEDevice.h>

#include "src/config.h"

namespace BleKeyboard
{
namespace
{

static char hidToAscii(uint8_t key, bool shift)
{
  if (key >= 0x04 && key <= 0x1D)
  {
    char base = (char)('a' + (key - 0x04));
    return shift ? (char)(base - 32) : base;
  }
  if (key >= 0x1E && key <= 0x26)
  {
    const char *nums = "123456789";
    char c = nums[key - 0x1E];
    if (shift)
    {
      const char *shifted = "!@#$%^&*(";
      return shifted[key - 0x1E];
    }
    return c;
  }
  if (key == 0x27)
    return shift ? ')' : '0';
  if (key == 0x2C)
    return ' ';
  if (key == 0x2D)
    return shift ? '_' : '-';
  if (key == 0x2E)
    return shift ? '+' : '=';
  if (key == 0x2F)
    return shift ? '{' : '[';
  if (key == 0x30)
    return shift ? '}' : ']';
  if (key == 0x31)
    return shift ? '|' : '\\';
  if (key == 0x33)
    return shift ? ':' : ';';
  if (key == 0x34)
    return shift ? '"' : '\'';
  if (key == 0x35)
    return shift ? '~' : '`';
  if (key == 0x36)
    return shift ? '<' : ',';
  if (key == 0x37)
    return shift ? '>' : '.';
  if (key == 0x38)
    return shift ? '?' : '/';
  return 0;
}

static uint8_t s_prevKeys[6] = {};

static bool wasInPrevReport(uint8_t key)
{
  for (int i = 0; i < 6; i++)
    if (s_prevKeys[i] == key)
      return true;
  return false;
}

static void handleBootReport(const uint8_t *data, size_t len, size_t keyStart)
{
  if (len < keyStart + 1)
    return;
  uint8_t mods = data[0];
  bool shift = (mods & 0x22) != 0;
  size_t keySlots = len - keyStart;
  if (keySlots > 6)
    keySlots = 6;

  uint8_t curKeys[6] = {};
  for (size_t i = 0; i < keySlots; i++)
    curKeys[i] = data[keyStart + i];

  for (size_t i = 0; i < keySlots; i++)
  {
    uint8_t key = data[keyStart + i];
    if (key == 0)
      continue;
    if (wasInPrevReport(key))
      continue;
    if (key == 0x28)
    {
#if DEBUG_BUILD
      Serial.println("[key] ENTER");
#endif
      Internal::enqueue(KeyAction::Newline);
      continue;
    }
    if (key == 0x2A)
    {
#if DEBUG_BUILD
      Serial.println("[key] BACKSPACE");
#endif
      Internal::enqueue(KeyAction::Backspace);
      continue;
    }
    char ch = hidToAscii(key, shift);
#if DEBUG_BUILD
    if (ch)
      Serial.printf("[key] 0x%02X shift=%d -> '%c'\n", key, shift, ch);
    else
      Serial.printf("[key] 0x%02X shift=%d -> NO MAP (dropped)\n", key, shift);
#endif
    if (ch)
      Internal::enqueue(KeyAction::Char, ch);
  }

  memcpy(s_prevKeys, curKeys, sizeof(s_prevKeys));
}

static void handleHidReport(const uint8_t *data, size_t len)
{
  if (len < 2)
    return;
  if (len == 9)
  {
    handleBootReport(data + 1, 8, 2);
    return;
  }
  if (len == 8)
  {
    handleBootReport(data, 8, 2);
    return;
  }
  if (len == 7)
  {
    handleBootReport(data, 7, 1);
    return;
  }
  handleBootReport(data, len, 1);
}

} // namespace

namespace Internal
{

void clearPrevKeys() { memset(s_prevKeys, 0, sizeof(s_prevKeys)); }

void onHidNotify(BLERemoteCharacteristic *, uint8_t *data, size_t len, bool)
{
#if DEBUG_BUILD
  Serial.printf("[hid] raw len=%u:", (unsigned)len);
  for (size_t i = 0; i < len; i++)
    Serial.printf(" %02X", data[i]);
  Serial.println();
#endif
  handleHidReport(data, len);
}

} // namespace Internal
} // namespace BleKeyboard
