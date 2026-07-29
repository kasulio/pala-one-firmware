#include "src/ui/screens/upload_screen.h"

#include "src/config.h"
#include "src/hal/display.h"
#include "src/hal/input.h"        // resetInputFrontend() — used on exit only
#include "src/hal/wifi.h"
#include "src/hal/wifi_provisioning.h"
#include "src/state.h"            // server
#include "src/storage/library.h"
#include "src/storage/wifi_creds.h"
#include "src/ui/font.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/widgets.h"
#include "src/web/apps_upload.h"  // resetAppUpload()
#include "src/web/upload.h"       // resetBookUpload() / resetSleepUpload()
#include "src/ui/reader_actions.h"

// How long to wait for an STA association (+DHCP) before falling back to AP.
// Matches UpdateScreen: cold Wi-Fi + EE05 post-refresh power recover often
// needs >5s; unreachable nets still fail via WL_NO_SSID_AVAIL sooner.
static constexpr uint32_t kStaTimeoutMs = 15000;

// ---- Drawing --------------------------------------------------------------
static void drawConnecting(const String& ssid) {
  prepareMenuFrame();
  int y = drawSectionHeader(D_UPLOAD_HEADER);

  Font::useBold();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(D_UPLOAD_CONNECTING);
  y += 14;

  Font::useBody();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(ssid.c_str());
  y += 18;

  Font::useBody();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(D_UPLOAD_HOTSPOT_HINT_L1);
  y += 14;
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(D_UPLOAD_HOTSPOT_HINT_L2);

  display.update();
}

void UploadScreen::draw() {
  prepareMenuFrame();

  int y = drawSectionHeader(D_UPLOAD_HEADER);

  if (net_.mode == WifiMode::Station) {
    Font::useBold();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(D_UPLOAD_CONNECTED);
    y += 14;

    Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(net_.staSsid.c_str());
    y += 18;

    Font::useBold();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(D_UPLOAD_OPEN);
    y += 14;

    Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(net_.primaryUrl.c_str());
    y += 14;

    if (net_.fallbackUrl.length() > 0) {
      u8g2.setCursor(MARGIN_X, y);
      u8g2.print(net_.fallbackUrl.c_str());
    }
  } else {
    Font::useBold();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(D_UPLOAD_WIFI);
    y += 14;

    Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(net_.apSsid);
    y += 16;

    Font::useBold();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(D_UPLOAD_PASSWORD);
    y += 14;

    Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(net_.apPass);
    y += 16;

    Font::useBold();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(D_UPLOAD_OPEN);
    y += 14;

    Font::useBody();
    u8g2.setCursor(MARGIN_X, y);
    u8g2.print(net_.primaryUrl.c_str());
  }

  display.update();
}

// ---- Lifecycle ------------------------------------------------------------
void UploadScreen::onEnter() {
  beginSession();
}

// Paint a one-line splash before touching the radio. SoftAP/STA + a full
// e-ink refresh on EE05 spike current hard enough to hang BUSY or brownout-
// reboot — panel then never leaves the library screen ("upload won't open").
static void drawUploadStarting() {
  prepareMenuFrame();
  int y = drawSectionHeader(D_UPLOAD_HEADER);
  Font::useBold();
  u8g2.setCursor(MARGIN_X, y);
  u8g2.print(D_UPLOAD_CONNECTING);
  display.update();
}

void UploadScreen::beginSession() {
  resetBookUpload();
  resetSleepUpload();
  resetAppUpload();

  // Tell WifiProvisioning to keep its hands off the radio for the duration
  // of the session — set BEFORE wifiStaBegin so a same-tick
  // WifiProvisioning::loop() can't race the WiFi.begin() we're about to fire.
  WifiProvisioning::notifyUploadSession(true);

  Serial.printf("[DEBUG-up1] enter heap=%u\n", ESP.getFreeHeap());

  if (WifiCreds::has()) {
    // Splash first (no radio), settle, then STA. onIdleTick falls back to AP.
    phase_ = Phase::ConnectingSta;
    drawConnecting(WifiCreds::ssid());
    Serial.printf("[DEBUG-up1] splash-sta heap=%u\n", ESP.getFreeHeap());
    delay(50);  // same settle as AP path — panel current before radio
    if (!wifiStaBegin()) {
      Serial.println("[DEBUG-up1] STA begin failed → AP");
      fallbackToAp();
      return;
    }
    staStartedMs_ = millis();
    Serial.println("[DEBUG-up1] STA begun");
  } else {
    drawUploadStarting();
    Serial.printf("[DEBUG-up1] splash-ap heap=%u\n", ESP.getFreeHeap());
    delay(50);  // let panel current settle before SoftAP
    net_ = wifiBeginAccessPoint();
    Serial.printf("[DEBUG-up1] AP up ssid=%s\n", net_.apSsid);
    delay(100);
    enterReady();
  }
}

void UploadScreen::enterReady() {
  phase_     = Phase::Ready;
  startedMs_ = millis();
  server.begin();
  Serial.printf("[DEBUG-up1] ready mode=%d heap=%u\n",
                (int)net_.mode, ESP.getFreeHeap());
  draw();
  Serial.println("[DEBUG-up1] ready draw done");
}

void UploadScreen::fallbackToAp() {
  Serial.println("[DEBUG-up1] fallback AP");
  wifiStaAbort();
  delay(50);
  net_ = wifiBeginAccessPoint();
  delay(100);
  enterReady();
}

void UploadScreen::stopSessionToLibrary() {
  server.stop();
  wifiEnd();
  WifiProvisioning::notifyUploadSession(false);

  resetBookUpload();
  resetSleepUpload();
  resetAppUpload();

  loadBooks();
  // Discard the exit-click so it doesn't leak into the library screen as a
  // menu selection.
  resetInputFrontend();
  nextScreen = &g_libraryScreen;
}

// ---- Per-iteration input + tick -------------------------------------------
void UploadScreen::onButton(const ButtonEvent& e) {
  if (!e.any()) return;

  // Any tap during the connecting splash means "use the hotspot instead".
  if (phase_ == Phase::ConnectingSta) {
    fallbackToAp();
    return;
  }

  if (Gestures::legacyControlsOn() && (e.kind == ButtonEvent::Short || e.kind == ButtonEvent::Triple)) {
    stopSessionToLibrary();
  }

  if (!Gestures::legacyControlsOn()) {
    if (Gestures::actionFor(e.kind) == ACTION_HOME) {
      stopSessionToLibrary();
    }
  }
}

void UploadScreen::onIdleTick() {
  if (phase_ == Phase::ConnectingSta) {
    WifiStaResult r = wifiStaPoll(net_);
    if (r == WifiStaResult::Connected) {
      enterReady();
      return;
    }
    if (r == WifiStaResult::Failed ||
        (uint32_t)(millis() - staStartedMs_) > kStaTimeoutMs) {
      fallbackToAp();
      return;
    }
    return;   // still associating — server isn't up yet
  }

  server.handleClient();
  if ((uint32_t)(millis() - startedMs_) > UPLOAD_AUTO_EXIT_MS) {
    stopSessionToLibrary();
  }
}
