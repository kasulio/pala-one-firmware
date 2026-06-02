#include "src/hal/ble_keyboard.h"
#include "src/hal/ble_keyboard_internal.h"

#include <ctype.h>

#include <esp_bt.h>

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLESecurity.h>
#include <Preferences.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <host/ble_store.h>

#include "src/config.h"
#include "src/hal/wifi.h"

#ifndef CONFIG_NIMBLE_ENABLED
#error "ble_keyboard requires NimBLE (arduino-esp32 3.x, ESP32-S3)."
#endif

namespace BleKeyboard
{

  namespace
  {

    static constexpr uint16_t kHidServiceUuid = 0x1812;
    static constexpr uint16_t kHidReportUuid = 0x2A4D;
    static constexpr uint16_t kAppearanceKeyboard = 0x03C1;
    static constexpr size_t kQueueCap = 32;
    static constexpr uint32_t kScanSeconds = 5;
    static constexpr uint32_t kRescanMs = 8000;
    static constexpr uint32_t kConnectTimeoutMs = 15000;
    static constexpr int kStrongCandidateScore = 120;
    static constexpr char kPrefsNs[] = "ereader";
    static constexpr char kAddrKey[] = "ble_kb_addr";
    static constexpr char kAddrTypeKey[] = "ble_kb_typ";
    static constexpr char kSavedListKey[] = "ble_kb_lst";
    static constexpr int kMaxSavedKb = 4;

    static BLEUUID s_hidService((uint16_t)kHidServiceUuid);
    static BLEUUID s_reportChar((uint16_t)kHidReportUuid);

    static BLEClient *s_client = nullptr;
    static LinkState s_link = LinkState::Off;
    static bool s_session = false;
    static uint32_t s_lastScanMs = 0;
    static uint32_t s_connectStartedMs = 0;
    static bool s_scanning = false;
    static bool s_runConnect = false;

    static char s_lastAdvName[40] = "";
    static char s_pickAddr[24] = "";
    static char s_pickName[40] = "";
    static uint8_t s_pickAddrType = 0;

    struct KbDevice
    {
      char addr[24];
      char name[40];
      uint8_t addrType = 0;
      int score = 0;
    };

    static constexpr int kMaxKbDevices = 8;
    static KbDevice s_devices[kMaxKbDevices];
    static int s_deviceCount = 0;
    static int s_selectedIndex = 0;

    struct SavedKb
    {
      char addr[24];
      char name[40];
      uint8_t addrType = 0;
    };

    static SavedKb s_savedKb[kMaxSavedKb];
    static int s_savedKbCount = 0;

    static bool hasPickAddress() { return s_pickAddr[0] != '\0'; }

    static bool hasScannedCandidate()
    {
      return s_deviceCount > 0 && s_selectedIndex >= 0 && s_selectedIndex < s_deviceCount;
    }

    static QueueHandle_t s_keyQueue = nullptr;

    static char s_pairingCode[8] = "";
    static PairingHint s_pairingHint = PairingHint::None;

    static bool s_authComplete = false;
    static bool s_awaitingSetup = false;

    enum class WorkerCmd : uint8_t
    {
      None,
      InitSession,
      Connect,
      FinishSetup,
      EndSession,
    };
    static volatile WorkerCmd s_workerCmd = WorkerCmd::None;
    static TaskHandle_t s_workerTask = nullptr;
    static TaskHandle_t s_endWaiter = nullptr;

    static void notifyWorker(WorkerCmd cmd)
    {
      s_workerCmd = cmd;
      if (s_workerTask)
        xTaskNotifyGive(s_workerTask);
    }

#if DEBUG_BUILD
    static bool s_dbgLoggedCandidate = false;
    static char s_dbgLoggedPin[8] = "";
#endif

    static void setPairingCode(uint32_t passkey, PairingHint hint)
    {
      snprintf(s_pairingCode, sizeof(s_pairingCode), "%06lu", (unsigned long)(passkey % 1000000UL));
      s_pairingHint = hint;
    }

    static void clearPairingCode()
    {
      s_pairingCode[0] = '\0';
      s_pairingHint = PairingHint::None;
    }

    class KbSecurityCb : public BLESecurityCallbacks
    {
      void onPassKeyNotify(uint32_t pass_key) override
      {
        setPairingCode(pass_key, PairingHint::TypeOnKeyboard);
      }

      bool onConfirmPIN(uint32_t pin) override
      {
        setPairingCode(pin, PairingHint::ConfirmOnKeyboard);
        // User confirms on the keyboard; we accept the same number here.
        return true;
      }

      bool onSecurityRequest() override { return true; }

      void onAuthenticationComplete(ble_gap_conn_desc * /*desc*/) override
      {
        s_authComplete = true;
        clearPairingCode();
        notifyWorker(WorkerCmd::FinishSetup);
      }
    };

    static KbSecurityCb s_secCb;

    static void enqueueKey(KeyAction action, char ch = 0)
    {
      if (!s_keyQueue)
        return;
      KeyEvent ev = {action, ch};
      xQueueSendFromISR(s_keyQueue, &ev, nullptr);
    }

    static BLERemoteService *findHidService(BLEClient *client)
    {
      BLERemoteService *hid = client->getService(s_hidService);
      if (hid)
        return hid;
      std::map<std::string, BLERemoteService *> *svcs = client->getServices();
      if (!svcs)
        return nullptr;
      for (auto &it : *svcs)
      {
        if (it.second && it.second->getUUID().equals(s_hidService))
          return it.second;
      }
      return nullptr;
    }

    static bool subscribeHidReports(BLEClient *client)
    {
      BLERemoteService *hid = findHidService(client);
      if (!hid)
        return false;

      bool any = false;
      std::map<std::string, BLERemoteCharacteristic *> *chars = hid->getCharacteristics();
      if (!chars)
        return false;

      for (auto &it : *chars)
      {
        BLERemoteCharacteristic *c = it.second;
        if (!c || !c->canNotify())
          continue;
        const BLEUUID &uuid = c->getUUID();
        if (uuid.equals(s_reportChar) || uuid.equals(BLEUUID((uint16_t)0x2A4B)))
        {
          c->registerForNotify(Internal::onHidNotify);
          any = true;
        }
      }
      if (!any)
      {
        for (auto &it : *chars)
        {
          BLERemoteCharacteristic *c = it.second;
          if (c && c->canNotify())
          {
            c->registerForNotify(Internal::onHidNotify);
            any = true;
          }
        }
      }
      return any;
    }

    static void ensureBleSecurity()
    {
      static bool configured = false;
      if (configured)
        return;
      configured = true;

      BLEDevice::setSecurityCallbacks(&s_secCb);
      BLESecurity sec;
      // Bond + MITM + LE SC — required for keyboards that show a pairing PIN.
      sec.setAuthenticationMode(true, true, true);
      sec.setCapability(ESP_IO_CAP_IO);
      sec.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
      sec.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    }

    static int findSavedByAddr(const char *addr)
    {
      if (!addr || !addr[0])
        return -1;
      for (int i = 0; i < s_savedKbCount; i++)
      {
        if (strcmp(s_savedKb[i].addr, addr) == 0)
          return i;
      }
      return -1;
    }

    static void persistSavedList()
    {
      String list;
      for (int i = 0; i < s_savedKbCount; i++)
      {
        if (i > 0)
          list += "|";
        list += s_savedKb[i].addr;
        list += ",";
        list += String((unsigned)s_savedKb[i].addrType);
        list += ",";
        list += s_savedKb[i].name;
      }

      Preferences p;
      if (!p.begin(kPrefsNs, false))
        return;
      if (list.length() == 0)
        p.remove(kSavedListKey);
      else
        p.putString(kSavedListKey, list);
      p.end();
    }

    static void parseSavedEntry(const String &chunk, SavedKb &out)
    {
      out = {};
      int c1 = chunk.indexOf(',');
      if (c1 < 0)
        return;
      int c2 = chunk.indexOf(',', c1 + 1);
      if (c2 < 0)
        return;

      String addr = chunk.substring(0, c1);
      uint8_t addrType = (uint8_t)chunk.substring(c1 + 1, c2).toInt();
      String name = chunk.substring(c2 + 1);
      if (addr.length() == 0)
        return;

      strncpy(out.addr, addr.c_str(), sizeof(out.addr) - 1);
      out.addr[sizeof(out.addr) - 1] = '\0';
      out.addrType = addrType;
      strncpy(out.name, name.c_str(), sizeof(out.name) - 1);
      out.name[sizeof(out.name) - 1] = '\0';
    }

    static void loadSavedList()
    {
      s_savedKbCount = 0;

      Preferences p;
      if (!p.begin(kPrefsNs, true))
        return;

      if (p.isKey(kAddrKey))
      {
        SavedKb legacy = {};
        String addr = p.getString(kAddrKey, "");
        legacy.addrType = p.getUChar(kAddrTypeKey, 0);
        if (addr.length() > 0)
        {
          strncpy(legacy.addr, addr.c_str(), sizeof(legacy.addr) - 1);
          legacy.addr[sizeof(legacy.addr) - 1] = '\0';
          s_savedKb[0] = legacy;
          s_savedKbCount = 1;
        }
        p.end();

        p.begin(kPrefsNs, false);
        p.remove(kAddrKey);
        p.remove(kAddrTypeKey);
        persistSavedList();
        return;
      }

      String list = p.getString(kSavedListKey, "");
      p.end();
      if (list.length() == 0)
        return;

      int start = 0;
      while (start < (int)list.length() && s_savedKbCount < kMaxSavedKb)
      {
        int sep = list.indexOf('|', start);
        if (sep < 0)
          sep = list.length();
        SavedKb entry;
        parseSavedEntry(list.substring(start, sep), entry);
        if (entry.addr[0] != '\0')
          s_savedKb[s_savedKbCount++] = entry;
        start = sep + 1;
      }
    }

    static void rememberPeer(const BLEAddress &addr, uint8_t addrType, const char *name)
    {
      loadSavedList();

      SavedKb entry = {};
      strncpy(entry.addr, addr.toString().c_str(), sizeof(entry.addr) - 1);
      entry.addr[sizeof(entry.addr) - 1] = '\0';
      entry.addrType = addrType;
      if (name && name[0])
      {
        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
      }

      int existing = findSavedByAddr(entry.addr);
      if (existing > 0)
      {
        SavedKb keep = entry;
        for (int i = existing; i > 0; i--)
          s_savedKb[i] = s_savedKb[i - 1];
        s_savedKb[0] = keep;
      }
      else if (existing == 0)
      {
        s_savedKb[0] = entry;
      }
      else
      {
        int n = min(s_savedKbCount, kMaxSavedKb - 1);
        for (int i = n; i > 0; i--)
          s_savedKb[i] = s_savedKb[i - 1];
        s_savedKb[0] = entry;
        if (s_savedKbCount < kMaxSavedKb)
          s_savedKbCount++;
      }

      persistSavedList();
    }

    static void clearSavedList()
    {
      s_savedKbCount = 0;
      Preferences p;
      if (!p.begin(kPrefsNs, false))
        return;
      if (p.isKey(kSavedListKey))
        p.remove(kSavedListKey);
      if (p.isKey(kAddrKey))
        p.remove(kAddrKey);
      if (p.isKey(kAddrTypeKey))
        p.remove(kAddrTypeKey);
      p.end();
    }

    static void removeAllBonds()
    {
      ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
      int count = 0;
      if (ble_store_util_bonded_peers(peers, &count, CONFIG_BT_NIMBLE_MAX_BONDS) != 0)
        return;
      for (int i = 0; i < count; i++)
        ble_store_util_delete_peer(&peers[i]);
    }

    static bool nameContains(const char *hay, const char *needle)
    {
      if (!hay || !needle || !needle[0])
        return false;
      const size_t nlen = strlen(needle);
      for (const char *p = hay; *p; p++)
      {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
          i++;
        if (i == nlen)
          return true;
      }
      return false;
    }

    static int keyboardAdvertScore(BLEAdvertisedDevice &dev)
    {
      int score = 0;
      if (dev.haveName())
      {
        const char *name = dev.getName().c_str();
        if (nameContains(name, "keyboard"))
          score += 80;
      }
      for (int i = 0; i < dev.getServiceUUIDCount(); i++)
      {
        if (dev.getServiceUUID(i).equals(s_hidService))
          score += 60;
      }
      if (dev.haveAppearance() && dev.getAppearance() == kAppearanceKeyboard)
        score += 50;
      return score;
    }

    static int findDeviceByAddr(const char *addr)
    {
      for (int i = 0; i < s_deviceCount; i++)
      {
        if (strcmp(s_devices[i].addr, addr) == 0)
          return i;
      }
      return -1;
    }

    static void sortDevicesByScore()
    {
      for (int i = 0; i < s_deviceCount - 1; i++)
      {
        for (int j = i + 1; j < s_deviceCount; j++)
        {
          if (s_devices[j].score > s_devices[i].score)
          {
            KbDevice tmp = s_devices[i];
            s_devices[i] = s_devices[j];
            s_devices[j] = tmp;
          }
        }
      }
    }

    static void syncPickFromSelected()
    {
      if (s_deviceCount == 0 || s_selectedIndex < 0 || s_selectedIndex >= s_deviceCount)
      {
        s_pickAddr[0] = '\0';
        s_pickName[0] = '\0';
        s_pickAddrType = 0;
        return;
      }
      const KbDevice &d = s_devices[s_selectedIndex];
      strncpy(s_pickAddr, d.addr, sizeof(s_pickAddr) - 1);
      s_pickAddr[sizeof(s_pickAddr) - 1] = '\0';
      strncpy(s_pickName, d.name, sizeof(s_pickName) - 1);
      s_pickName[sizeof(s_pickName) - 1] = '\0';
      s_pickAddrType = d.addrType;
    }

    static void resetDeviceList()
    {
      s_deviceCount = 0;
      s_selectedIndex = 0;
      s_pickAddr[0] = '\0';
      s_pickName[0] = '\0';
      s_pickAddrType = 0;
    }

    static void upsertDevice(BLEAdvertisedDevice &dev, int score)
    {
      if (score < 40)
        return;

      const char *addr = dev.getAddress().toString().c_str();
      int idx = findDeviceByAddr(addr);
      if (idx < 0)
      {
        if (s_deviceCount >= kMaxKbDevices)
        {
          int worst = 0;
          for (int i = 1; i < s_deviceCount; i++)
          {
            if (s_devices[i].score < s_devices[worst].score)
              worst = i;
          }
          if (score <= s_devices[worst].score)
            return;
          idx = worst;
        }
        else
        {
          idx = s_deviceCount++;
        }
        strncpy(s_devices[idx].addr, addr, sizeof(s_devices[idx].addr) - 1);
        s_devices[idx].addr[sizeof(s_devices[idx].addr) - 1] = '\0';
        s_devices[idx].name[0] = '\0';
        s_devices[idx].score = 0;
      }

      s_devices[idx].addrType = dev.getAddressType();

      int savedIdx = findSavedByAddr(addr);
      if (savedIdx >= 0)
      {
        score += 250;
        if (s_devices[idx].name[0] == '\0' && s_savedKb[savedIdx].name[0] != '\0')
        {
          strncpy(s_devices[idx].name, s_savedKb[savedIdx].name, sizeof(s_devices[idx].name) - 1);
          s_devices[idx].name[sizeof(s_devices[idx].name) - 1] = '\0';
        }
      }

      if (score > s_devices[idx].score)
        s_devices[idx].score = score;

      if (dev.haveName())
      {
        strncpy(s_devices[idx].name, dev.getName().c_str(), sizeof(s_devices[idx].name) - 1);
        s_devices[idx].name[sizeof(s_devices[idx].name) - 1] = '\0';
        strncpy(s_lastAdvName, s_devices[idx].name, sizeof(s_lastAdvName) - 1);
        s_lastAdvName[sizeof(s_lastAdvName) - 1] = '\0';
      }

      char keepAddr[24];
      strncpy(keepAddr, s_pickAddr, sizeof(keepAddr) - 1);
      keepAddr[sizeof(keepAddr) - 1] = '\0';

      sortDevicesByScore();

      int keepIdx = findDeviceByAddr(keepAddr);
      if (keepIdx >= 0)
        s_selectedIndex = keepIdx;
      else if (s_selectedIndex >= s_deviceCount)
        s_selectedIndex = 0;

      syncPickFromSelected();
    }

    class ClientCb : public BLEClientCallbacks
    {
      void onConnect(BLEClient *) override {}
      void onDisconnect(BLEClient *) override
      {
        if (s_session)
        {
          s_link = LinkState::Failed;
          s_lastScanMs = 0;
        }
        else
        {
          s_link = LinkState::Off;
        }
      }
    };

    static ClientCb s_clientCb;

    static void stopScanIfStrongCandidate()
    {
      if (!s_scanning || !hasScannedCandidate())
        return;
      if (s_devices[s_selectedIndex].score < kStrongCandidateScore)
        return;
      BLEDevice::getScan()->stop();
      s_scanning = false;
    }

    static void requestConnect()
    {
      if (!hasPickAddress())
        return;
      if (s_link == LinkState::Connecting || s_link == LinkState::Connected)
        return;
      s_link = LinkState::Connecting;
      s_connectStartedMs = millis();
      s_authComplete = false;
      s_awaitingSetup = false;
      Internal::clearPrevKeys();
      notifyWorker(WorkerCmd::Connect);
    }

    static void startScan(bool clearList);
    static void tryFinishPendingSetup();

    static void runFinishSetup();

    static bool finishClientSetup(const BLEAddress &addr)
    {
      if (!s_client || !s_client->isConnected())
        return false;

      s_client->setMTU(517);

      if (!subscribeHidReports(s_client))
      {
        s_client->disconnect();
        return false;
      }

      rememberPeer(addr, s_pickAddrType, s_pickName[0] ? s_pickName : nullptr);
      s_link = LinkState::Connected;
      s_connectStartedMs = 0;
      return true;
    }

    static void runFinishSetup()
    {
      if (!s_awaitingSetup || !s_client || !s_client->isConnected() || !s_authComplete)
        return;

      s_awaitingSetup = false;
      if (!finishClientSetup(BLEAddress(s_pickAddr)))
      {
        s_link = LinkState::Failed;
        s_connectStartedMs = 0;
        if (s_client->isConnected())
          s_client->disconnect();
      }
    }

    static bool connectToPick()
    {
      if (!hasPickAddress())
      {
        s_link = LinkState::Failed;
        s_connectStartedMs = 0;
        return false;
      }

      BLEDevice::getScan()->stop();
      s_scanning = false;

      if (!s_client)
        s_client = BLEDevice::createClient();
      s_client->setClientCallbacks(&s_clientCb);

#if DEBUG_BUILD
      Serial.printf("[ble-kb] connecting %s (%s) addrType=%u\n", s_pickAddr,
                    s_pickName[0] ? s_pickName : "?", (unsigned)s_pickAddrType);
#endif

      if (!s_client->connect(BLEAddress(s_pickAddr), s_pickAddrType))
      {
#if DEBUG_BUILD
        Serial.println("[ble-kb] connect failed");
#endif
        if (s_link != LinkState::Connecting)
          return false;
        s_link = LinkState::Failed;
        s_connectStartedMs = 0;
        s_awaitingSetup = false;
        if (s_deviceCount == 0)
          startScan(true);
        return false;
      }

      if (s_link != LinkState::Connecting || !s_session)
      {
        if (s_client->isConnected())
          s_client->disconnect();
        return false;
      }

      s_awaitingSetup = true;
      tryFinishPendingSetup();
      return true;
    }

    static void workerInitSession()
    {
      BLEDevice::init("Pala-One");
      ensureBleSecurity();
      loadSavedList();
      startScan(true);
    }

    static void workerEndSession()
    {
      s_client = nullptr;
      if (BLEDevice::getInitialized())
        BLEDevice::deinit(true);
      btStop();
      setCpuFrequencyMhz(80);
      TaskHandle_t waiter = s_endWaiter;
      if (waiter)
        xTaskNotifyGive(waiter);
    }

    static void workerTask(void * /*arg*/)
    {
      for (;;)
      {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        switch (s_workerCmd)
        {
        case WorkerCmd::InitSession:
          workerInitSession();
          break;
        case WorkerCmd::Connect:
          connectToPick();
          break;
        case WorkerCmd::FinishSetup:
          runFinishSetup();
          break;
        case WorkerCmd::EndSession:
          workerEndSession();
          break;
        default:
          break;
        }
        s_workerCmd = WorkerCmd::None;
      }
    }

    static void ensureWorker()
    {
      if (s_workerTask)
        return;
      xTaskCreate(workerTask, "ble-kb", 8192, nullptr, 1, &s_workerTask);
    }

    static void tryFinishPendingSetup()
    {
      if (!s_awaitingSetup || !s_authComplete)
        return;
      notifyWorker(WorkerCmd::FinishSetup);
    }

    class ScanCb : public BLEAdvertisedDeviceCallbacks
    {
      void onResult(BLEAdvertisedDevice dev) override
      {
        if (!s_session || s_link == LinkState::Connecting || s_link == LinkState::Connected)
          return;

        if (dev.haveName())
        {
          strncpy(s_lastAdvName, dev.getName().c_str(), sizeof(s_lastAdvName) - 1);
          s_lastAdvName[sizeof(s_lastAdvName) - 1] = '\0';
        }

        upsertDevice(dev, keyboardAdvertScore(dev));
        stopScanIfStrongCandidate();
      }
    };

    static ScanCb s_scanCb;

    static void startScan(bool clearList)
    {
      if (!s_session || s_scanning)
        return;
      s_connectStartedMs = 0;
      s_runConnect = false;
      clearPairingCode();
      if (clearList)
        resetDeviceList();
#if DEBUG_BUILD
      s_dbgLoggedCandidate = false;
      s_dbgLoggedPin[0] = '\0';
#endif

      BLEScan *scan = BLEDevice::getScan();
      scan->setAdvertisedDeviceCallbacks(&s_scanCb, true);
      scan->setActiveScan(true);
      scan->setInterval(45);
      scan->setWindow(30);
      s_scanning = true;
      s_link = LinkState::Scanning;
      scan->start(kScanSeconds, false);
      s_lastScanMs = millis();
    }

#if DEBUG_BUILD
    static void logFromLoop()
    {
      if (s_link == LinkState::Scanning && hasScannedCandidate())
      {
        if (!s_dbgLoggedCandidate)
        {
          Serial.printf("[ble-kb] found %s (%s) score=%d\n", s_pickAddr,
                        s_pickName[0] ? s_pickName : "?", s_devices[s_selectedIndex].score);
          s_dbgLoggedCandidate = true;
        }
      }
      else
      {
        s_dbgLoggedCandidate = false;
      }

      if (s_pairingCode[0] != '\0' && strcmp(s_pairingCode, s_dbgLoggedPin) != 0)
      {
        strncpy(s_dbgLoggedPin, s_pairingCode, sizeof(s_dbgLoggedPin) - 1);
        s_dbgLoggedPin[sizeof(s_dbgLoggedPin) - 1] = '\0';
        Serial.printf("[ble-kb] PIN %s\n", s_pairingCode);
      }
      if (s_pairingCode[0] == '\0')
        s_dbgLoggedPin[0] = '\0';
    }
#else
    static void logFromLoop() {}
#endif

  } // namespace

  namespace Internal
  {

    void enqueue(KeyAction action, char ch)
    {
      enqueueKey(action, ch);
    }

  } // namespace Internal

  void beginSession()
  {
    if (s_session)
      return;
    if (WiFi.getMode() != WIFI_OFF)
      wifiEnd();

    setCpuFrequencyMhz(240);
    ensureWorker();

    s_session = true;
    if (!s_keyQueue)
      s_keyQueue = xQueueCreate(kQueueCap, sizeof(KeyEvent));
    else
      xQueueReset(s_keyQueue);
    Internal::clearPrevKeys();
    s_lastAdvName[0] = '\0';
    s_authComplete = false;
    s_awaitingSetup = false;
    clearPairingCode();
    s_link = LinkState::Scanning;

    notifyWorker(WorkerCmd::InitSession);
  }

  void requestEndSession()
  {
    if (!s_session)
    {
      if (BLEDevice::getInitialized() && s_workerTask)
        notifyWorker(WorkerCmd::EndSession);
      return;
    }

    s_session = false;
    s_scanning = false;
    s_runConnect = false;
    s_authComplete = false;
    s_awaitingSetup = false;
    clearPairingCode();
    if (BLEDevice::getInitialized())
      BLEDevice::getScan()->stop();

    if (s_client && s_client->isConnected())
      s_client->disconnect();
    s_link = LinkState::Off;

    if (s_workerTask)
      notifyWorker(WorkerCmd::EndSession);
    else
    {
      s_client = nullptr;
      if (BLEDevice::getInitialized())
        BLEDevice::deinit(true);
      btStop();
      setCpuFrequencyMhz(80);
    }
  }

  void endSession()
  {
    if (!s_session && s_link == LinkState::Off && !BLEDevice::getInitialized())
      return;

    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    s_endWaiter = self;
    requestEndSession();
    if (BLEDevice::getInitialized() || s_workerTask)
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8000));
    s_endWaiter = nullptr;
  }

  void cancelConnect()
  {
    if (!s_session || s_link != LinkState::Connecting)
      return;

    s_runConnect = false;
    s_awaitingSetup = false;
    s_authComplete = false;
    s_connectStartedMs = 0;
    clearPairingCode();

    if (s_client && s_client->isConnected())
      s_client->disconnect();

    if (BLEDevice::getInitialized())
    {
      BLEDevice::getScan()->stop();
      s_scanning = false;
    }

    s_link = hasScannedCandidate() ? LinkState::Scanning : LinkState::Failed;
    s_lastScanMs = 0;
    if (s_session)
      startScan(false);
  }

  void forgetPeer()
  {
    if (s_client && s_client->isConnected())
      s_client->disconnect();
    clearSavedList();
    removeAllBonds();
    s_lastScanMs = 0;
    if (s_session)
    {
      s_lastAdvName[0] = '\0';
      startScan(true);
    }
#if DEBUG_BUILD
    Serial.println("[ble-kb] bonds cleared");
#endif
  }

  bool hasCandidate() { return hasPickAddress(); }

  int deviceCount() { return s_deviceCount; }

  int selectedIndex() { return s_selectedIndex; }

  const char *candidateName() { return s_pickName[0] ? s_pickName : s_pickAddr; }

  void cycleCandidate()
  {
    if (s_deviceCount == 0)
      return;
    if (s_link == LinkState::Connecting || s_link == LinkState::Connected)
      return;
    s_selectedIndex = (s_selectedIndex + 1) % s_deviceCount;
    syncPickFromSelected();
#if DEBUG_BUILD
    s_dbgLoggedCandidate = false;
#endif
  }

  void connectCandidate()
  {
    if (!s_session || !hasCandidate())
      return;
    if (s_link == LinkState::Connecting || s_link == LinkState::Connected)
      return;
    s_runConnect = true;
  }

  void loop()
  {
    if (!s_session)
      return;

    logFromLoop();

    if (s_runConnect)
    {
      s_runConnect = false;
      requestConnect();
      return;
    }

    if (s_link == LinkState::Connected)
    {
      if (s_client && !s_client->isConnected())
      {
        s_link = LinkState::Failed;
        s_lastScanMs = 0;
      }
      return;
    }

    if (s_link == LinkState::Connecting && s_connectStartedMs != 0)
    {
      tryFinishPendingSetup();
      if (s_link == LinkState::Connected)
        return;

      uint32_t limit = kConnectTimeoutMs;
      if (s_pairingCode[0] != '\0' || s_awaitingSetup)
        limit = 45000;
      if ((uint32_t)(millis() - s_connectStartedMs) > limit)
      {
#if DEBUG_BUILD
        Serial.println("[ble-kb] timeout");
#endif
        s_awaitingSetup = false;
        if (s_client && s_client->isConnected())
          s_client->disconnect();
        s_connectStartedMs = 0;
        startScan(false);
      }
      return;
    }

    if (s_scanning && BLEDevice::getInitialized())
    {
      stopScanIfStrongCandidate();
      if (!BLEDevice::getScan()->isScanning())
      {
        s_scanning = false;
        if (s_link == LinkState::Scanning && !hasScannedCandidate())
          s_link = LinkState::Failed;
      }
      return;
    }

    if (s_link != LinkState::Connecting)
    {
      if (s_lastScanMs == 0 || (uint32_t)(millis() - s_lastScanMs) > kRescanMs)
        startScan(false);
    }
  }

  LinkState linkState() { return s_link; }

  const char *pairingCode() { return s_pairingCode; }

  PairingHint pairingHint() { return s_pairingHint; }

  const char *lastAdvertisedName() { return s_lastAdvName; }

  bool popEvent(KeyEvent &out)
  {
    if (!s_keyQueue)
      return false;
    return xQueueReceive(s_keyQueue, &out, 0) == pdTRUE;
  }

  bool isSessionActive() { return s_session; }

} // namespace BleKeyboard
