// External dependencies
extern Preferences prefs;
extern struct Wearable W1, W2;
extern const int PIN_BOOT_BTN;

Preferences prefs;
String macA, macB;

bool waitForForceSetupWindow() {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  const unsigned long WINDOW = 5000;
  const unsigned long HOLD = 2000;
  unsigned long t0 = millis();
  unsigned long lowStart = 0;

  Serial.println("Hold BOOT about 2 s in first 5 s to enter ParentHub WiFi setup");

  while (millis() - t0 < WINDOW) {
    if (digitalRead(PIN_BOOT_BTN) == LOW) {
      if (!lowStart) lowStart = millis();
      if (millis() - lowStart >= HOLD) {
        Serial.println("BOOT held, entering WiFi config portal");
        return true;
      }
    } else {
      lowStart = 0;
    }
    delay(20);
  }
  return false;
}

void loadPairing() {
  prefs.begin("pair", true);
  macA = prefs.getString("macA", "");
  macB = prefs.getString("macB", "");
  prefs.end();

  W1.label = "A";
  W2.label = "B";
  W1.mac = macA;
  W2.mac = macB;

  Serial.printf("Loaded A=%s  B=%s\n", macA.c_str(), macB.c_str());
}

void savePairing(const String& A, const String& B) {
  prefs.begin("pair", false);
  prefs.putString("macA", A);
  prefs.putString("macB", B);
  prefs.end();
  macA = A;
  macB = B;
  W1.mac = macA;
  W2.mac = macB;

  W1.shouldConnect = true;
  W2.shouldConnect = true;
  W1.nextRetryMs = millis();
  W2.nextRetryMs = millis();

  Serial.printf("Saved A=%s  B=%s\n", macA.c_str(), macB.c_str());
}