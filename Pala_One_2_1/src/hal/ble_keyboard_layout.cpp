#include "src/hal/ble_keyboard_layout.h"

#include <string.h>

namespace BleKeyboard
{

namespace
{

static size_t putAscii(char *out, size_t cap, char c)
{
  if (cap < 1)
    return 0;
  out[0] = c;
  return 1;
}

static size_t putUtf8(char *out, size_t cap, const char *utf8)
{
  const size_t n = strlen(utf8);
  if (cap < n)
    return 0;
  memcpy(out, utf8, n);
  return n;
}

static char usLetterFromHid(uint8_t key, bool shift)
{
  if (key < 0x04 || key > 0x1D)
    return 0;
  char c = (char)('a' + (key - 0x04));
  return shift ? (char)(c - 32) : c;
}

static char remapUsLetter(char usLower, const char map[26])
{
  if (usLower < 'a' || usLower > 'z')
    return usLower;
  return map[usLower - 'a'];
}

static size_t mapDigitsUs(uint8_t key, bool shift, char *out, size_t cap)
{
  if (key < 0x1E || key > 0x26)
    return 0;
  const char *nums = "123456789";
  const char *shifted = "!@#$%^&*(";
  return putAscii(out, cap, shift ? shifted[key - 0x1E] : nums[key - 0x1E]);
}

static size_t mapDigitsDe(uint8_t key, bool shift, char *out, size_t cap)
{
  if (key < 0x1E || key > 0x26)
    return 0;
  const char *nums = "123456789";
  const char *shifted[] = {"!", "\"", "\xC2\xA7", "$", "%", "&", "/", "(", ")"};
  if (shift)
    return putUtf8(out, cap, shifted[key - 0x1E]);
  return putAscii(out, cap, nums[key - 0x1E]);
}

static size_t mapPunctuationUs(uint8_t key, bool shift, char *out, size_t cap)
{
  switch (key)
  {
  case 0x27:
    return putAscii(out, cap, shift ? ')' : '0');
  case 0x2C:
    return putAscii(out, cap, ' ');
  case 0x2D:
    return putAscii(out, cap, shift ? '_' : '-');
  case 0x2E:
    return putAscii(out, cap, shift ? '+' : '=');
  case 0x2F:
    return putAscii(out, cap, shift ? '{' : '[');
  case 0x30:
    return putAscii(out, cap, shift ? '}' : ']');
  case 0x31:
    return putAscii(out, cap, shift ? '|' : '\\');
  case 0x33:
    return putAscii(out, cap, shift ? ':' : ';');
  case 0x34:
    return putAscii(out, cap, shift ? '"' : '\'');
  case 0x35:
    return putAscii(out, cap, shift ? '~' : '`');
  case 0x36:
    return putAscii(out, cap, shift ? '<' : ',');
  case 0x37:
    return putAscii(out, cap, shift ? '>' : '.');
  case 0x38:
    return putAscii(out, cap, shift ? '?' : '/');
  default:
    return 0;
  }
}

static size_t mapPunctuationDe(uint8_t key, bool shift, char *out, size_t cap)
{
  switch (key)
  {
  case 0x27:
    return putAscii(out, cap, shift ? '=' : '0');
  case 0x2C:
    return putAscii(out, cap, ' ');
  case 0x2D:
    return shift ? putAscii(out, cap, '?') : putUtf8(out, cap, "\xC3\x9F");
  case 0x2E:
    return shift ? putAscii(out, cap, '`')
                 : putUtf8(out, cap, "\xC2\xB4");
  case 0x2F:
    return shift ? putUtf8(out, cap, "\xC3\x9C") : putUtf8(out, cap, "\xC3\xBC");
  case 0x30:
    return putAscii(out, cap, shift ? '*' : '+');
  case 0x31:
    return putAscii(out, cap, shift ? '\'' : '#');
  case 0x33:
    return shift ? putUtf8(out, cap, "\xC3\x96") : putUtf8(out, cap, "\xC3\xB6");
  case 0x34:
    return shift ? putUtf8(out, cap, "\xC3\x84") : putUtf8(out, cap, "\xC3\xA4");
  case 0x35:
    return shift ? putUtf8(out, cap, "\xC2\xB0") : putAscii(out, cap, '^');
  case 0x36:
    return putAscii(out, cap, shift ? ';' : ',');
  case 0x37:
    return putAscii(out, cap, shift ? ':' : '.');
  case 0x38:
    return putAscii(out, cap, shift ? '_' : '-');
  default:
    return 0;
  }
}

static size_t mapAltGrDe(uint8_t key, char *out, size_t cap)
{
  switch (key)
  {
  case 0x08:
    return putUtf8(out, cap, "\xE2\x82\xAC");
  case 0x10:
    return putAscii(out, cap, '@');
  case 0x24:
    return putAscii(out, cap, '{');
  case 0x25:
    return putAscii(out, cap, '[');
  case 0x26:
    return putAscii(out, cap, ']');
  case 0x27:
    return putAscii(out, cap, '\\');
  case 0x2D:
    return putAscii(out, cap, '\\');
  case 0x2F:
    return putAscii(out, cap, '[');
  case 0x30:
    return putAscii(out, cap, ']');
  case 0x31:
    return putAscii(out, cap, '\'');
  case 0x33:
    return putAscii(out, cap, '{');
  case 0x34:
    return putAscii(out, cap, '}');
  case 0x35:
    return putAscii(out, cap, '|');
  case 0x36:
    return putAscii(out, cap, '|');
  default:
    return 0;
  }
}

} // namespace

size_t hidUsageToUtf8(uint8_t key, uint8_t mods, KeyboardLayout layout,
                      char *out, size_t outCap)
{
  if (!out || outCap == 0)
    return 0;

  const bool shift = (mods & 0x22) != 0;
  const bool altGr = (mods & 0x40) != 0;

  if (altGr && layout == KeyboardLayout::DE)
  {
    const size_t n = mapAltGrDe(key, out, outCap);
    if (n)
      return n;
  }

  static const char kUsToDe[] = "abcdefghijklmnopqrstuvwxzy";
  static const char kUsToFr[] = "qbcdefghijklmnoparstuvzxyw";

  char ch = usLetterFromHid(key, shift);
  if (ch)
  {
    const char lower = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
    switch (layout)
    {
    case KeyboardLayout::DE:
      ch = remapUsLetter(lower, kUsToDe);
      break;
    case KeyboardLayout::FR:
      ch = remapUsLetter(lower, kUsToFr);
      break;
    default:
      break;
    }
    if (shift && ch >= 'a' && ch <= 'z')
      ch = (char)(ch - 32);
    return putAscii(out, outCap, ch);
  }

  switch (layout)
  {
  case KeyboardLayout::DE:
    if (size_t n = mapDigitsDe(key, shift, out, outCap))
      return n;
    return mapPunctuationDe(key, shift, out, outCap);
  default:
    if (size_t n = mapDigitsUs(key, shift, out, outCap))
      return n;
    return mapPunctuationUs(key, shift, out, outCap);
  }
}

} // namespace BleKeyboard
