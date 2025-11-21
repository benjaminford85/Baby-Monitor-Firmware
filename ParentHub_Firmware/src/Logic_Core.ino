// External dependencies
extern struct Wearable W1, W2;

// Decision Constants
const int PARENT_NONE = -1;
const int PARENT_A = 0;
const int PARENT_B = 1;

const unsigned long REFRACTORY_MS = 5UL * 60UL * 1000UL;
const uint8_t HR_WAKE_DELTA = 8;
const uint32_t MOTION_WAKE_THRESH = 150;

const unsigned long PATTERN_PERIOD_MS = 10000;
const unsigned long PATTERN_ON_MS = 5000;

// State Variables
volatile bool cryActive = false;
int activeParent = PARENT_NONE;
int lastAlerted = PARENT_NONE;
unsigned long lastAlertTimeMs = 0;
unsigned long patternStartMs = 0;
bool lastPatternOnState = false;
uint8_t baselineHR_A = 60, baselineHR_B = 60;
unsigned long lastBaselineMs = 0;

// Helper function retained from original sketch
bool looksAwakeVals(uint8_t lastHR, uint32_t lastMotion, uint8_t baseHR) {
  bool hrUp = (lastHR >= (uint8_t)(baseHR + HR_WAKE_DELTA));
  bool moved = (lastMotion >= MOTION_WAKE_THRESH);
  return hrUp && moved;
}

// Decision logic function (retained here)
int chooseParentInt() {
  unsigned long now = millis();
  bool aLock = (lastAlerted == PARENT_A) && (now - lastAlertTimeMs < REFRACTORY_MS);
  bool bLock = (lastAlerted == PARENT_B) && (now - lastAlertTimeMs < REFRACTORY_MS);

  if (aLock && !bLock) return PARENT_B;
  if (bLock && !aLock) return PARENT_A;

  int scoreA = (int)W1.lastMotion + 3 * max(0, (int)W1.lastHR - (int)baselineHR_A);
  int scoreB = (int)W2.lastMotion + 3 * max(0, (int)W2.lastHR - (int)baselineHR_B);

  if (scoreA > scoreB) return PARENT_A;
  if (scoreB > scoreA) return PARENT_B;

  if (lastAlerted == PARENT_A) return PARENT_B;
  if (lastAlerted == PARENT_B) return PARENT_A;
  return PARENT_A;
}

void stopAllHaptics() {
  W1.sendHaptic(false);
  W2.sendHaptic(false);
}

void handleCryStart() {
  if (cryActive) {
    Serial.println("[ALERT] Cry already active, ignoring extra CRY_START");
    return;
  }

  cryActive = true;
  int who = chooseParentInt();
  lastAlerted = who;
  lastAlertTimeMs = millis();

  if (who == PARENT_A) {
    activeParent = PARENT_A;
    patternStartMs = millis();
    lastPatternOnState = false;
    Serial.println("[ALERT] Cry start -> Wake Parent A (pattern)");
    W1.sendHaptic(true);
    W2.sendHaptic(false);
  } else if (who == PARENT_B) {
    activeParent = PARENT_B;
    patternStartMs = millis();
    lastPatternOnState = false;
    Serial.println("[ALERT] Cry start -> Wake Parent B (pattern)");
    W2.sendHaptic(true);
    W1.sendHaptic(false);
  } else {
    Serial.println("[ALERT] No parent chosen, not starting pattern");
    cryActive = false;
    activeParent = PARENT_NONE;
    stopAllHaptics();
  }
}

void handleCryEnd() {
  cryActive = false;
  activeParent = PARENT_NONE;
  lastPatternOnState = false;
  stopAllHaptics();
  Serial.println("[ALERT] Cry end, stop all haptics");
}

// Maintain 5 s ON 5 s OFF pattern
void alertMaintenance() {
  if (!cryActive || activeParent == PARENT_NONE) return;

  unsigned long now = millis();
  unsigned long elapsed = (now - patternStartMs) % PATTERN_PERIOD_MS;
  bool shouldOn = (elapsed < PATTERN_ON_MS);

  if (shouldOn == lastPatternOnState) {
    return;
  }
  lastPatternOnState = shouldOn;

  if (activeParent == PARENT_A) {
    W1.sendHaptic(shouldOn);
    W2.sendHaptic(false);
    Serial.printf("[ALERT] Pattern A -> %s\n", shouldOn ? "ON" : "OFF");
  } else if (activeParent == PARENT_B) {
    W2.sendHaptic(shouldOn);
    W1.sendHaptic(false);
    Serial.printf("[ALERT] Pattern B -> %s\n", shouldOn ? "ON" : "OFF");
  }
}

// Baseline HR Maintenance
void updateBaselineHR() {
  if (!cryActive && (millis() - lastBaselineMs > 3000)) {
    if (W1.lastHR) baselineHR_A = (baselineHR_A * 7 + W1.lastHR) / 8;
    if (W2.lastHR) baselineHR_B = (baselineHR_B * 7 + W2.lastHR) / 8;
    lastBaselineMs = millis();
  }
}