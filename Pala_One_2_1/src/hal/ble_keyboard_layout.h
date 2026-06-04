#ifndef PALA_HAL_BLE_KEYBOARD_LAYOUT_H
#define PALA_HAL_BLE_KEYBOARD_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "src/hal/ble_keyboard.h"

namespace BleKeyboard
{

// Writes one UTF-8 character (1–4 bytes) into out. Returns byte count, or 0 if
// unmapped for the active layout.
size_t hidUsageToUtf8(uint8_t usage, uint8_t mods, KeyboardLayout layout,
                      char *out, size_t outCap);

} // namespace BleKeyboard

#endif
