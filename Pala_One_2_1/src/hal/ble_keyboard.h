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
    Left,
    Right,
    Up,
    Down,
  };

  struct KeyEvent
  {
    KeyAction action = KeyAction::None;
    uint8_t utf8Len = 0;
    char utf8[4] = {};
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

  enum class KeyboardLayout : uint8_t
  {
    Unknown = 0,
    US,
    UK,
    DE,
    FR,
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

  int savedKeyboardCount();
  const char *savedKeyboardLabel(int index);
  void pickSavedKeyboard(int index);

  int extraScannedCount();
  const char *extraScannedLabel(int index);
  void pickExtraScanned(int index);

  int deviceListVersion();
  bool isScanInProgress();

  LinkState linkState();
  const char *pairingCode();
  PairingHint pairingHint();
  const char *lastAdvertisedName();

  bool popEvent(KeyEvent &out);
  bool isSessionActive();

  bool needsLayoutPick();
  void setKeyboardLayout(KeyboardLayout layout);
  KeyboardLayout activeLayout();

} // namespace BleKeyboard

#endif // PALA_HAL_BLE_KEYBOARD_H
