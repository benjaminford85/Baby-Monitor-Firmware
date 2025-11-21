// External dependencies
extern WebServer server;
extern const int PIN_BOOT_BTN;
extern void handleRoot();
extern void handleScan();
extern void handleStatus();
extern void handlePair();
extern void handleTest();
extern void handleDisconnect();
extern void handleConnectManual();


WebServer server(80);

void enterConfigPortal() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.startConfigPortal("ParentHub_Setup")) {
    Serial.println("ParentHub portal timeout, restarting");
    delay(1000);
    ESP.restart();
  }
  Serial.print("ParentHub IP: ");
  Serial.println(WiFi.localIP());
}

void autoConnectOrPortal() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ParentHub_Setup")) {
    Serial.println("ParentHub autoConnect failed, restarting");
    delay(1000);
    ESP.restart();
  }
  Serial.print("ParentHub IP: ");
  Serial.println(WiFi.localIP());
}

void startMDNS() {
  if (MDNS.begin("parenthub")) {
    Serial.println("mDNS: http://parenthub.local");
  } else {
    Serial.println("mDNS start failed");
  }
}

void startWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/pair", HTTP_POST, handlePair);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/disconnect", HTTP_POST, handleDisconnect);
  server.on("/api/connect", HTTP_POST, handleConnectManual);
  server.begin();
  Serial.println("HTTP server started");
}