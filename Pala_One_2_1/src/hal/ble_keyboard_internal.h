#ifndef PALA_HAL_BLE_KEYBOARD_INTERNAL_H
#define PALA_HAL_BLE_KEYBOARD_INTERNAL_H

#include "src/hal/ble_keyboard.h"

class BLERemoteCharacteristic;

namespace BleKeyboard
{
namespace Internal
{

void enqueue(KeyAction action, char ch = 0);
void clearPrevKeys();
void onHidNotify(BLERemoteCharacteristic *ch, uint8_t *data, size_t len, bool isNotify);

} // namespace Internal
} // namespace BleKeyboard

#endif
