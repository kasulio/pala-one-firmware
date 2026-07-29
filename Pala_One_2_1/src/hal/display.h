#ifndef PALA_HAL_DISPLAY_H
#define PALA_HAL_DISPLAY_H

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "src/config.h"
#include "src/state.h"
#include "src/ui/screen_settings.h"

// ============================================================================
//  Display adapter — Adafruit_GFX face for u8g2. Rotates 180° by default
//  (panel mounting). Flipped orientation skips the transform.
// ============================================================================
class HeltecGFXAdapter : public Adafruit_GFX {
public:
  explicit HeltecGFXAdapter(EInkDisplay& d)
    : Adafruit_GFX(SCREEN_W, SCREEN_H), disp(d) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
#if defined(SEEED_EE05)
    // u8g2: 1 = ink. SSD1680 bit1 = white, bit0 = black → ink clears the bit.
    uint16_t c = color ? TFT_BLACK : TFT_WHITE;
    // Seeed panel is native portrait 122×250. Map landscape UI → portrait.
    // Default (Heltec-equivalent 180°): (ux,uy) → (uy, W-1-ux)
    // Flipped:                          (ux,uy) → (H-1-uy, ux)
    int16_t px, py;
    if (!ScreenSettings::isScreenFlipped()) {
      px = y;
      py = (SCREEN_W - 1) - x;
    } else {
      px = (SCREEN_H - 1) - y;
      py = x;
    }
    disp.drawPixel(px, py, c);
#else
    uint16_t c = color ? BLACK : WHITE;
    int16_t xx = (SCREEN_W - 1) - x;
    int16_t yy = (SCREEN_H - 1) - y;
    if (!ScreenSettings::isScreenFlipped()) {
      disp.drawPixel(xx, yy, c);
    } else {
      disp.drawPixel(x, y, c);
    }
#endif
  }

private:
  EInkDisplay& disp;
};

extern HeltecGFXAdapter gfx;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

void beginPageCanvas(bool clearMem = true);

#endif  // PALA_HAL_DISPLAY_H
