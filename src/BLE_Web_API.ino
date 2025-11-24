// External dependencies
extern WebServer server;
extern bool scanInProgress;
extern struct Wearable W1, W2;
extern String macA, macB;
extern void savePairing(const String& A, const String& B);
extern void forceDisconnect(Wearable& W);
extern void enableReconnect(Wearable& W);
extern volatile bool cryActive; 
extern int activeParent; 
extern void stopAllHaptics(); 
extern NimBLEUUID UUID_CUSTOM_SVC;


struct ScanItem {
  String name;
  String mac;
  int rssi;
  bool hasCustomService; 
};


std::vector<ScanItem> doScan(uint32_t durationMs) {
  std::vector<ScanItem> out;
  
  // Ensure clients are disconnected to clear radio resources for scanning
  if (W1.client && W1.client->isConnected()) {
    W1.client->disconnect();
    W1.connected = false;
  }
  if (W2.client && W2.client->isConnected()) {
    W2.client->disconnect();
    W2.connected = false;
  }

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  
  // Using aggressive/proven parameters from debug/RevB sketches
  scan->setInterval(45); // ms
  scan->setWindow(30);  // ms
  
  scan->setDuplicateFilter(true); // NEW: Use true to reduce clutter and stabilize list

  scanInProgress = true;
  Serial.printf("BLE scan starting for %u ms...\n", durationMs); 
  
  bool ok = scan->start(durationMs, false);
  
  Serial.printf("scan->start() returned: %s\n", ok ? "true" : "false");

  NimBLEScanResults res = scan->getResults();
  int count = res.getCount();
  Serial.printf("BLE scan complete: %d device(s)\n", count);

  for (int i = 0; i < count; ++i) {
    const NimBLEAdvertisedDevice* d = res.getDevice(i);
    if (!d) continue;

    String mac = String(d->getAddress().toString().c_str());
    int rssi = d->getRSSI();
    const char* nm = d->haveName() ? d->getName().c_str() : "";
    bool hasCust = d->haveServiceUUID() && d->isAdvertisingService(UUID_CUSTOM_SVC);

String nameStr = String(nm);
// NEW: Filter for devices that start with "BabyWatch_"
if (!nameStr.startsWith("BabyWatch_") && !hasCust) {
    continue; // Skip non-wearable devices
}

    // VITAL DEBUGGING OUTPUT: Print EVERYTHING found
    Serial.printf("ADV %02d: %s  RSSI=%d  Name=%s  Svc=%s\n",
                  i,
                  mac.c_str(),
                  rssi,
                  (nm && *nm) ? nm : "(none)",
                  hasCust ? "custom90ab" : "-");

    ScanItem item;
    item.name = nameStr;
    item.mac = mac;
    item.rssi = rssi;
    item.hasCustomService = hasCust;
    out.push_back(item);
  }

  scan->clearResults();
  scanInProgress = false;
  return out;
}

// HTML with Connect / Disconnect controls added
const char HTML_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ParentHub - Pair and Manage Wearables</title>
<style>
body{font-family:system-ui;margin:24px}
button{padding:10px 14px;border-radius:10px;border:1px solid #ddd;cursor:pointer}
.card{border:1px solid #eee;border-radius:16px;padding:16px;margin:12px 0;box-shadow:0 2px 8px rgba(0,0,0,.04)}
.badge{display:inline-block;padding:2px 8px;border-radius:999px;background:#eee;margin-left:6px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.attention{color:orange}
</style></head><body>
<h2>ParentHub - Pair and Manage Wearables</h2>
<div class="card">
  <div class="row">
    <button onclick="scan()">Scan</button>
    <span id="status" class="badge">Idle</span>
  </div>
  <div id="list"></div>
</div>

<div class="card">
  <h3>Selected</h3>
  <div>A: <span id="selA">—</span></div>
  <div>B: <span id="selB">—</span></div>
  <div class="row" style="margin-top:8px">
    <button onclick="save()">Save</button>
    <button onclick="refresh()">Refresh Status</button>
  </div>
  <pre id="stat" style="white-space:pre-wrap"></pre>
</div>

<div class="card">
  <h3>Connection Control</h3>
  <div class="row">
    <button onclick="connectWearable('A')">Connect A</button>
    <button onclick="disconnectWearable('A')">Disconnect A</button>
    <button onclick="connectWearable('B')">Connect B</button>
    <button onclick="disconnectWearable('B')">Disconnect B</button>
  </div>
  <p class="attention">Note: Disconnect wearables before scanning to avoid interference.</p>
</div>

<div class="card">
  <h3>Test Mode</h3>
  <p>Test writes directly to wearable HAPTIC characteristic (LED on these test firmwares).</p>
  <div class="row">
    <button onclick="test('A',1)">LED A ON</button>
    <button onclick="test('A',0)">LED A OFF</button>
    <button onclick="test('B',1)">LED B ON</button>
    <button onclick="test('B',0)">LED B OFF</button>
  </div>
</div>

<script>
let pickA=null,pickB=null;

function scan(){
  set('status','Scanning...');
  document.getElementById('list').innerHTML = '<p>Scanning... please wait (12s). Ensure no wearables are actively connected.</p>';
  fetch('/api/scan').then(r=>r.json()).then(js=>{
    const list=document.getElementById('list'); list.innerHTML='';
    if (js.devices.length === 0) {
        list.innerHTML = '<p>No BLE devices found. Ensure they are powered on and advertising.</p>';
    }
    js.devices.sort((a,b)=>b.rssi-a.rssi).forEach(d=>{
      const div=document.createElement('div'); div.className='card';
      const name = d.name || '(no name)';
      const serviceBadge = d.hasCustomService ? '<span class="badge">Custom Svc</span>' : '';
      div.innerHTML=`<b>${name}</b> <span class="badge">${d.mac}</span> <span class="badge">RSSI ${d.rssi}</span> ${serviceBadge}
      <div class="row" style="margin-top:8px">
        <button onclick="choose('A','${d.mac}')">Choose A</button>
        <button onclick="choose('B','${d.mac}')">Choose B</button>
      </div>`;
      list.appendChild(div);
    });
    set('status','Scan complete');
  });
}

function choose(which,mac){
  if(which==='A'){ pickA=mac; document.getElementById('selA').innerText=mac; }
  else { pickB=mac; document.getElementById('selB').innerText=mac; }
}

function save(){
  if(!pickA||!pickB){ alert('Pick both A and B'); return; }
  fetch('/api/pair',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({macA:pickA,macB:pickB})
  }).then(_=>refresh());
}

function refresh(){
  fetch('/api/status').then(r=>r.json()).then(js=>{
    document.getElementById('stat').innerText=JSON.stringify(js,null,2);
    if(js.paired.macA) document.getElementById('selA').innerText=js.paired.macA;
    if(js.paired.macB) document.getElementById('selB').innerText=js.paired.macB;
  });
}

function test(h,on){
  fetch(`/api/test?h=${h}&on=${on}`,{method:'POST'});
}

function connectWearable(h){
  fetch(`/api/connect?h=${h}`,{method:'POST'}).then(_=>refresh());
}

function disconnectWearable(h){
  fetch(`/api/disconnect?h=${h}`,{method:'POST'}).then(_=>refresh());
}

function set(id,t){ document.getElementById(id).innerText=t; }

refresh();
</script>
</body></html>
)HTML";

// Web handlers
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleScan() {
  // Pass 12000 milliseconds (12 seconds) as the duration
  auto vec = doScan(12000); 

  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.createNestedArray("devices");
  for (auto& d : vec) {
    JsonObject o = arr.createNestedObject();
    o["name"] = d.name;
    o["mac"] = d.mac;
    o["rssi"] = d.rssi;
    o["hasCustomService"] = d.hasCustomService; 
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePair() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "missing body");
    return;
  }
  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  String A = doc["macA"] | "";
  String B = doc["macB"] | "";
  if (A.length() < 17 || B.length() < 17) {
    server.send(400, "text/plain", "need macA & macB");
    return;
  }
  savePairing(A, B);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStatus() {
  StaticJsonDocument<512> doc;

  JsonObject paired = doc.createNestedObject("paired");
  paired["macA"] = macA;
  paired["macB"] = macB;

  JsonObject a = doc.createNestedObject("wearableA");
  a["connected"] = W1.connected;
  a["shouldConnect"] = W1.shouldConnect;
  a["hr"] = W1.lastHR;
  a["motion"] = W1.lastMotion;

  JsonObject b = doc.createNestedObject("wearableB");
  b["connected"] = W2.connected;
  b["shouldConnect"] = W2.shouldConnect;
  b["hr"] = W2.lastHR;
  b["motion"] = W2.lastMotion;

  JsonObject alert = doc.createNestedObject("alert");
  alert["cryActive"] = cryActive;
  if (activeParent == PARENT_A) alert["activeParent"] = "A";
  else if (activeParent == PARENT_B) alert["activeParent"] = "B";
  else alert["activeParent"] = "none";

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleTest() {
  String h = server.arg("h");
  String on = server.arg("on");
  bool val = (on == "1" || on == "true");
  bool ok = false;

  if (h == "A") ok = W1.sendHaptic(val);
  else if (h == "B") ok = W2.sendHaptic(val);

  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleDisconnect() {
  String h = server.arg("h");
  if (h == "A") {
    forceDisconnect(W1);
    // If the active parent is manually disconnected, stop the cry/alert pattern
    if (activeParent == PARENT_A) {
      cryActive = false;
      activeParent = PARENT_NONE;
      stopAllHaptics();
    }
  } else if (h == "B") {
    forceDisconnect(W2);
    // If the active parent is manually disconnected, stop the cry/alert pattern
    if (activeParent == PARENT_B) {
      cryActive = false;
      activeParent = PARENT_NONE;
      stopAllHaptics();
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleConnectManual() {
  String h = server.arg("h");
  if (h == "A") {
    enableReconnect(W1);
  } else if (h == "B") {
    enableReconnect(W2);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}