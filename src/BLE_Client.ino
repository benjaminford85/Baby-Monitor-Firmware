// External dependencies
extern int activeParent; 
extern void stopAllHaptics(); 
extern bool scanInProgress;
extern void acknowledgeAlert(); // Function linked from Logic_Core.ino

// ---------- BLE UUIDs ----------
static NimBLEUUID UUID_HRS("180D");
static NimBLEUUID UUID_HRMC("2A37");

// Custom UUIDs for Wearable Client Characteristics
static NimBLEUUID UUID_CUSTOM_SVC ("00000000-0000-1000-8000-00805f9b90ab");
static NimBLEUUID UUID_MOTION_CHAR("00000000-0000-1000-8000-00805f9b90ac");
static NimBLEUUID UUID_HAPTIC_CHAR("00000000-0000-1000-8000-00805f9b90ad");

// NEW: UUID for the Acknowledge Characteristic on the Parent Hub (as a Server)
static NimBLEUUID UUID_ACK_CHAR("00000000-0000-1000-8000-00805f9b90ae"); 

// NEW: Declarations for the Parent Hub's GATT Server (GLOBAL SETUP)
NimBLEServer* pServer = nullptr;
NimBLEService* pService = nullptr;
NimBLECharacteristic* pAckChar = nullptr;

// NEW: Callback class to handle the Acknowledge command from the Wearable
class AckCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    // When the wearable writes to this characteristic (i.e., button pressed)
    uint8_t* data = pCharacteristic->getValue();
    if (data && data[0] == 1) { // Check for a non-null data pointer and a '1' signal
      Serial.println("[ACKNOWLEDGE] Wearable button pressed!");
      // Call the acknowledge handler defined in Logic_Core.ino
      acknowledgeAlert(); 
    }
  }
};

// NEW: Function to set up the Parent Hub as a GATT Server
void startGattServer() {
  Serial.println("Starting Parent Hub as BLE Server...");
  pServer = NimBLEDevice::createServer();
  
  // Reuse the existing custom service UUID for the server side
  pService = pServer->createService(UUID_CUSTOM_SVC); 
  
  // Create the characteristic the wearable will write to
  pAckChar = pService->createCharacteristic(
                      UUID_ACK_CHAR,
                      NIMBLE_PROPERTY::WRITE_NO_RSP | NIMBLE_PROPERTY::WRITE
                  );
  pAckChar->setCallbacks(new AckCallback());
  pService->start();
  
  Serial.println("GATT Server ready for Acknowledge signal (ACK_CHAR).");
}


// ---------- Wearable client capsule (Full Definition) ----------
struct Wearable {
  String label; // "A" or "B"
  String mac;
  NimBLEAddress addr;
  NimBLEClient* client = nullptr;
  NimBLERemoteService* hrSvc = nullptr;
  NimBLERemoteCharacteristic* hrChar = nullptr;
  NimBLERemoteService* custSvc = nullptr;
  NimBLERemoteCharacteristic* motionChar = nullptr;
  NimBLERemoteCharacteristic* hapticChar = nullptr;

  volatile uint8_t lastHR = 0;
  volatile uint32_t lastMotion = 0;
  uint32_t lastUpdateMs = 0;

  bool connected = false;
  bool shouldConnect = true;
  uint32_t nextRetryMs = 0;
  uint32_t retryDelayMs = 4000; 

  bool connectOnce(uint8_t addrType) {
    if (mac.length() < 17) {
      Serial.printf("BLE %s: no MAC configured\n", label.c_str());
      return false;
    }

    if (client && client->isConnected()) {
      connected = true;
      return true;
    }

    if (client) {
      NimBLEDevice::deleteClient(client);
      client = nullptr;
    }

    addr = NimBLEAddress(std::string(mac.c_str()), addrType);
    client = NimBLEDevice::createClient();
    client->setConnectionParams(12, 24, 0, 60);
    client->setConnectTimeout(6);

    Serial.printf("BLE %s connect -> %s\n", label.c_str(), mac.c_str());
    if (!client->connect(addr, false) || !client->isConnected()) {
      Serial.printf("BLE %s connect FAIL %s\n", label.c_str(), mac.c_str());
      connected = false;
      return false;
    }

    hrSvc = client->getService(UUID_HRS);
    if (hrSvc) {
      hrChar = hrSvc->getCharacteristic(UUID_HRMC);
    }

    custSvc = client->getService(UUID_CUSTOM_SVC);
    if (custSvc) {
      motionChar = custSvc->getCharacteristic(UUID_MOTION_CHAR);
      hapticChar = custSvc->getCharacteristic(UUID_HAPTIC_CHAR);
    }

    // Subscription logic for HR and Motion (retained from original sketch)
    if (hrChar && hrChar->canNotify()) {
      hrChar->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
        if (len >= 2) {
          this->lastHR = data[1];
          this->lastUpdateMs = millis();
        }
      });
      Serial.printf("BLE %s: subscribed HR\n", label.c_str());
    } else {
      Serial.printf("BLE %s: HR missing or not notifiable\n", label.c_str());
    }

    if (motionChar && motionChar->canNotify()) {
      motionChar->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
        if (len >= 4) {
          this->lastMotion = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
          this->lastUpdateMs = millis();
        }
      });
      Serial.printf("BLE %s: subscribed Motion\n", label.c_str());
    } else {
      Serial.printf("BLE %s: Motion missing or not notifiable\n", label.c_str());
    }

    connected = true;
    Serial.printf("BLE %s connected %s\n", label.c_str(), mac.c_str());
    return true;
  }

  bool sendHaptic(bool on) {
    if (!connected || !hapticChar || !hapticChar->canWrite()) {
      Serial.printf("BLE %s: haptic unavailable (conn=%d)\n", label.c_str(), connected);
      return false;
    }
    uint8_t cmd = on ? 1 : 0;
    bool ok = hapticChar->writeValue(&cmd, 1, false);
    Serial.printf("BLE %s: haptic %s -> %s\n",
                  label.c_str(), on ? "ON" : "OFF", ok ? "OK" : "FAIL");
    if (!ok) {
      connected = client && client->isConnected();
    }
    return ok;
  }
};

Wearable W1, W2;

// Simple connection maintenance with fixed retry, respecting scanInProgress
void maintainConnection(Wearable& W, uint8_t addrType) {
  if (!W.shouldConnect || W.mac.length() < 17) return;

  unsigned long now = millis();

  bool isNowConnected = (W.client && W.client->isConnected());
  if (isNowConnected) {
    if (!W.connected) {
      W.connected = true;
      Serial.printf("BLE %s state: CONNECTED\n", W.label.c_str());
    }
    W.retryDelayMs = 4000; 
    return;
  }

  if (W.connected) {
    W.connected = false;
    Serial.printf("BLE %s state: DISCONNECTED\n", W.label.c_str());
  }

  if (scanInProgress) {
    return;
  }

  if (now < W.nextRetryMs) {
    return;
  }

  bool ok = W.connectOnce(addrType);
  if (ok) {
    W.connected = true;
    W.retryDelayMs = 4000; // Success sets delay for next maintenance check
    W.nextRetryMs = now + W.retryDelayMs;
  } else {
    // Use fixed 4s delay on failure instead of exponential backoff
    W.retryDelayMs = 4000; 
    W.nextRetryMs = now + W.retryDelayMs;
  }
}


void forceDisconnect(Wearable& W) {
  W.shouldConnect = false;
  if (W.client && W.client->isConnected()) {
    Serial.printf("BLE %s: force disconnect\n", W.label.c_str());
    W.client->disconnect();
  }
  W.connected = false;
  W.nextRetryMs = millis();
}


void enableReconnect(Wearable& W) {
  W.shouldConnect = true;
  W.nextRetryMs = millis();
  W.retryDelayMs = 4000; // Reset delay to standard
  Serial.printf("BLE %s: reconnect enabled\n", W.label.c_str());
}