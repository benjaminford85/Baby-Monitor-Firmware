// External dependencies
extern void handleCryStart(); 
extern void handleCryEnd(); 

const uint16_t UDP_PORT_LISTEN = 42100;
WiFiUDP udp; // Definition of the global UDP object

void udpBegin() {
  udp.begin(UDP_PORT_LISTEN);
  Serial.printf("UDP listening on %u\n", UDP_PORT_LISTEN);
}

void processUDP() {
  int pkt = udp.parsePacket();
  if (!pkt) return;

  uint8_t buf[256];
  int n = udp.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = 0;

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, buf, n);
  if (error) {
    Serial.println("UDP: JSON parse error");
    return;
  }

  const char* type = doc["type"] | "";
  if (!strcmp(type, "CRY_START")) {
    Serial.println("UDP: CRY_START");
    handleCryStart();
  } else if (!strcmp(type, "CRY_END")) {
    Serial.println("UDP: CRY_END");
    handleCryEnd();
  } else if (!strcmp(type, "HEARTBEAT")) {
    // ignore
  } else {
    Serial.print("UDP: unknown type ");
    Serial.println(type);
  }
}