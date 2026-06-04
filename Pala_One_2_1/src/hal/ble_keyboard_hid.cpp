#include "src/hal/ble_keyboard_internal.h"
#include "src/hal/ble_keyboard_layout.h"

#include <BLEDevice.h>

#include "src/config.h"

namespace BleKeyboard
{
namespace
{

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
    if (key == 0x50)
    {
      Internal::enqueue(KeyAction::Left);
      continue;
    }
    if (key == 0x4F)
    {
      Internal::enqueue(KeyAction::Right);
      continue;
    }
    if (key == 0x52)
    {
      Internal::enqueue(KeyAction::Up);
      continue;
    }
    if (key == 0x51)
    {
      Internal::enqueue(KeyAction::Down);
      continue;
    }
    char utf8[4] = {};
    const size_t n = hidUsageToUtf8(key, mods, Internal::activeLayoutForHid(),
                                    utf8, sizeof(utf8));
#if DEBUG_BUILD
    if (n)
    {
      Serial.printf("[key] 0x%02X shift=%d -> ", key, shift);
      for (size_t i = 0; i < n; i++)
        Serial.printf("%02X ", (uint8_t)utf8[i]);
      Serial.println();
    }
    else
      Serial.printf("[key] 0x%02X shift=%d -> NO MAP (dropped)\n", key, shift);
#endif
    if (n)
      Internal::enqueue(KeyAction::Char, utf8, (uint8_t)n);
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
