#ifndef PALA_HAL_SEEED_DISPLAY_H
#define PALA_HAL_SEEED_DISPLAY_H

// Heltec-compatible facade over Seeed_GFX EPaper for the EE05 board.
// Call sites keep using display.clear / update / fastmode* / landscape.
//
// fastmode uses a custom SSD1680 partial path (GxEPD2-style 0xFC) — Seeed's
// updataPartial is broken here: mode 0xFF + deep-sleep/reinit every frame
// wipes controller RAM and flashes like a full refresh.

#include <TFT_eSPI.h>

#ifndef EPAPER_ENABLE
#error "SEEED_EE05 requires EPAPER_ENABLE (driver.h / BOARD_SCREEN_COMBO=508)."
#endif

class PalaSeeedDisplay {
public:
  void begin();
  void landscape();
  void clearMemory();
  void clear();
  void fastmodeOn();
  void fastmodeOff();
  void update();
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void powerOff();

private:
  static constexpr size_t kBufBytes = (size_t)EPD_WIDTH * (size_t)EPD_HEIGHT / 8;

  void waitBusy();
  void setFullWindow();
  void pushRam(uint8_t cmd, const uint8_t* data);
  void syncPrevFromSprite();
  void refreshPartial();
  void refreshFull();

  EPaper epaper_;
  uint8_t prev_[kBufBytes];
  bool begun_ = false;
  bool fast_ = false;
  bool awake_ = false;      // controller not in deep sleep
  bool prev_valid_ = false; // prev_ matches what's on the glass
};

#endif  // PALA_HAL_SEEED_DISPLAY_H
