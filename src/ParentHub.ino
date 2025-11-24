#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>

// ---------- Pin Definitions (Defined here for hardware access) ----------
static const int PIN_BOOT_BTN = 0;    
static const int PIN_STATUS_LED = 38; 

// ---------- External Variables (Linked from other modules) ----------
extern WiFiUDP udp;
extern WebServer server;
extern String macA, macB;
extern bool scanInProgress;
extern struct Wearable W1, W2;

// Functions linked from other modules
extern bool waitForForceSetupWindow();
extern void enterConfigPortal();
extern void autoConnectOrPortal();
extern void startMDNS();
extern void startWeb();
extern void udpBegin();
extern void processUDP();
extern void loadPairing();
extern void maintainConnection(Wearable& W, uint8_t addrType);
extern void alertMaintenance();
extern void updateBaselineHR();


void setup() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("ParentHub Modular Firmware starting");

  // Quick LED blink
  for (int i = 0; i < 2; ++i) {
    digitalWrite(PIN_STATUS_LED, HIGH);
    delay(80);
    digitalWrite(PIN_STATUS_LED, LOW);
    delay(80);
  }

  if (waitForForceSetupWindow()) {
    enterConfigPortal();
  } else {
    autoConnectOrPortal();
  }

  startMDNS();
  startWeb();

  udpBegin(); // Starts UDP listening
  
  NimBLEDevice::init("ParentHub");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  loadPairing();

  // Initialize connection state variables
  W1.shouldConnect = (macA.length() >= 17);
  W2.shouldConnect = (macB.length() >= 17);
  W1.retryDelayMs = 4000;
  W2.retryDelayMs = 4000;
  W1.nextRetryMs = millis();
  W2.nextRetryMs = millis();
}

void loop() {
  // Network/Web
  server.handleClient();
  processUDP(); 
  
  // Alert/Haptic Maintenance
  alertMaintenance(); 

  // Connection Maintenance (Using Random Address Type 1 for better stability)
  uint8_t addrType = 1; // Changed from 0 (Public) for potential stability improvement
  maintainConnection(W1, addrType); 
  maintainConnection(W2, addrType);

  // Baseline HR Maintenance
  updateBaselineHR();

  delay(10);
}