#if defined(SEEED_EE05)

#include "src/hal/seeed_display.h"

#include <string.h>

#ifndef TFT_ENABLE
#define TFT_ENABLE 43
#endif

void PalaSeeedDisplay::waitBusy() {
#ifdef TFT_BUSY
  // SSD1680 BUSY is active-high (Seeed CHECK_BUSY / GxEPD2_213_B74).
  while (digitalRead(TFT_BUSY)) {
    delay(1);
  }
#endif
}

void PalaSeeedDisplay::setFullWindow() {
  // GxEPD2_213_B74 window — not Seeed's (x+1)/8 variant.
  epaper_.writecommand(0x11);
  epaper_.writedata(0x03); // x++, y++
  epaper_.writecommand(0x44);
  epaper_.writedata(0x00);
  epaper_.writedata((EPD_WIDTH / 8) - 1);
  epaper_.writecommand(0x45);
  epaper_.writedata(0x00);
  epaper_.writedata(0x00);
  epaper_.writedata((EPD_HEIGHT - 1) & 0xFF);
  epaper_.writedata((EPD_HEIGHT - 1) >> 8);
  epaper_.writecommand(0x4E);
  epaper_.writedata(0x00);
  epaper_.writecommand(0x4F);
  epaper_.writedata(0x00);
  epaper_.writedata(0x00);
}

void PalaSeeedDisplay::pushRam(uint8_t cmd, const uint8_t* data) {
  epaper_.writecommand(cmd);
  for (size_t i = 0; i < kBufBytes; i++) {
    epaper_.writedata(data[i]);
  }
}

void PalaSeeedDisplay::syncPrevFromSprite() {
  const uint8_t* img = (const uint8_t*)epaper_.getPointer();
  if (!img) return;
  memcpy(prev_, img, kBufBytes);
  prev_valid_ = true;
}

void PalaSeeedDisplay::refreshFull() {
  // Library full refresh: OTP waveform 0xF7 — flashes hard (panel physics).
  // Needed to clear ghosting; cannot be made "chill" without skipping a real clear.
  epaper_.update();
  syncPrevFromSprite();
  awake_ = false; // update() deep-sleeps
}

void PalaSeeedDisplay::refreshPartial() {
  const uint8_t* img = (const uint8_t*)epaper_.getPointer();
  if (!img) {
    refreshFull();
    return;
  }

  // After deep sleep, controller RAM is gone — wake + rewrite both planes.
  // Stay awake across consecutive partials (Seeed's bug: sleep every frame).
  if (!awake_) {
    epaper_.wake();
    awake_ = true;
  }

  setFullWindow();
  // Differential: 0x26 = what's on glass, 0x24 = new frame.
  pushRam(0x26, prev_valid_ ? prev_ : img);
  pushRam(0x24, img);

  // GxEPD2_213_B74 _Update_Part — NOT Seeed's 0xFF (full-flash sequence).
  epaper_.writecommand(0x22);
  epaper_.writedata(0xFC);
  epaper_.writecommand(0x20);
  waitBusy();

  // Keep previous plane in sync for the next differential update.
  memcpy(prev_, img, kBufBytes);
  prev_valid_ = true;
  // Do not deep-sleep here.
}

void PalaSeeedDisplay::begin() {
  if (begun_) return;

  pinMode(TFT_ENABLE, OUTPUT);
  digitalWrite(TFT_ENABLE, HIGH);
  delay(50);

  epaper_.begin();
  // init() ends with setViewport(0,0, EPD_WIDTH-COL_OFFSET, …) = 122×250.
  // Do NOT call setRotation(): TFT_eSprite::setRotation always resetViewport()
  // and drops COL_OFFSET. Landscape UI is mapped in HeltecGFXAdapter.
  begun_ = true;

  epaper_.fillSprite(TFT_WHITE);
  refreshFull();
}

void PalaSeeedDisplay::landscape() {
  begin();
}

void PalaSeeedDisplay::clearMemory() {
  begin();
  epaper_.fillSprite(TFT_WHITE);
}

void PalaSeeedDisplay::clear() {
  begin();
  clearMemory();
  fast_ = false;
  refreshFull();
}

void PalaSeeedDisplay::fastmodeOn()  { fast_ = true; }
void PalaSeeedDisplay::fastmodeOff() { fast_ = false; }

void PalaSeeedDisplay::update() {
  begin();
  if (fast_) refreshPartial();
  else       refreshFull();
}

void PalaSeeedDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  begin();
  epaper_.drawPixel(x, y, color);
}

void PalaSeeedDisplay::powerOff() {
  if (begun_) {
    epaper_.sleep();
    awake_ = false;
  }
  pinMode(TFT_ENABLE, OUTPUT);
  digitalWrite(TFT_ENABLE, LOW);
}

#endif  // SEEED_EE05
