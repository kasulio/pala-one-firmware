#ifndef PALA_HAL_BLE_KEYBOARD_H
#define PALA_HAL_BLE_KEYBOARD_H

#include <Arduino.h>

// BLE HID host (central, NimBLE). Wi-Fi must be off before beginSession().
// Remembers recent keyboards for scan ranking; user picks and connects manually.
// Notes: 1x cycles selection, 2x connectCandidate(), 3x forgetPeer().
namespace BleKeyboard
{

  enum class KeyAction : uint8_t
  {
    None = 0,
    Char,
    Backspace,
    Newline,
  };

  struct KeyEvent
  {
    KeyAction action = KeyAction::None;
    char ch = 0;
  };

  enum class LinkState : uint8_t
  {
    Off,
    Scanning,
    Connecting,
    Connected,
    Failed,
  };

  enum class PairingHint : uint8_t
  {
    None = 0,
    TypeOnKeyboard,
    ConfirmOnKeyboard,
  };

  void beginSession();
  // Tear down BLE on the worker task (non-blocking).
  void requestEndSession();
  // Blocks until worker teardown finishes (use from onSleep).
  void endSession();
  void cancelConnect();
  void forgetPeer();
  void loop();

  bool hasCandidate();
  int deviceCount();
  int selectedIndex();
  const char *candidateName();
  void cycleCandidate();
  void connectCandidate();

  LinkState linkState();
  const char *pairingCode();
  PairingHint pairingHint();
  const char *lastAdvertisedName();

  bool popEvent(KeyEvent &out);
  bool isSessionActive();

} // namespace BleKeyboard

#endif // PALA_HAL_BLE_KEYBOARD_H
