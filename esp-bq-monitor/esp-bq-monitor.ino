#include <WiFi.h>
#include <WebServer.h>

HardwareSerial diag(1);
WebServer server(80);

static const char *WIFI_SSID = "Tangles";
static const char *WIFI_PASS = "Jiblet!1337";

static const uint8_t SYNC_BYTE = 0xA5;
static const uint8_t SYNC2_BYTE = 0x5A;
static const uint8_t PKT_LEN = 27;
static const int DIAG_RX_PIN = 16;
static uint8_t pkt[PKT_LEN];
static uint8_t pktIdx = 0;

static uint16_t frame = 0;
static uint16_t c1 = 0;
static uint16_t c2 = 0;
static int16_t currentRaw = 0;
static int16_t intTempC = 0;
static uint16_t tsRaw = 0;
static uint8_t soc = 0;
static uint16_t batteryStatus = 0;
static uint16_t alarmStatus = 0;
static uint16_t alarmRawStatus = 0;
static uint8_t safetyAlertA = 0;
static uint8_t safetyStatusA = 0;
static uint8_t safetyAlertB = 0;
static uint8_t safetyStatusB = 0;
static uint8_t flags = 0;
static uint16_t alarmSeen = 0;
static uint8_t safetyASeen = 0;
static uint8_t safetyBSeen = 0;
static uint16_t batterySeen = 0;
static uint32_t lastGoodPacketMs = 0;
static uint32_t goodPackets = 0;
static uint32_t badPackets = 0;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>BQ Monitor</title>
  <style>
    body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#111;color:#eee;margin:16px}
    h1{font-size:1.3rem}
    h2{font-size:1.05rem;margin-top:18px}
    table{border-collapse:collapse;width:100%;max-width:980px}
    td{padding:8px 10px;border-bottom:1px solid #333}
    td:first-child, td:nth-child(3){color:#aaa;width:24%}
    td:nth-child(2), td:nth-child(4){width:26%}
    .mono{font-family:ui-monospace,Consolas,Monaco,monospace}
    .tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px}
    .tile{border:1px solid #333;border-radius:8px;padding:10px;background:#1b1b1b;position:relative}
    .tile h3{margin:0 0 6px 0;font-size:0.95rem}
    .ok{border-color:#1f6b2d;background:#122015}
    .bad{border-color:#8b1e1e;background:#2a1313}
    .neutral{border-color:#555;background:#1a1a1a}
    .state{font-weight:700}
    .state.ok{color:#71d98b}
    .state.bad{color:#ff7f7f}
    .state.neutral{color:#b3b3b3}
    .latched{
      position:absolute;right:8px;bottom:8px;
      width:18px;height:18px;border-radius:50%;
      display:flex;align-items:center;justify-content:center;
      background:#f0b429;color:#111;font-weight:800;font-size:12px;
      box-shadow:0 0 0 1px #7a5a11 inset;
    }
  </style>
</head>
<body>
  <h1>BQ76905 Telemetry</h1>
  <table class="mono">
    <tr><td>FRAME / OK / BAD / AGE</td><td id="link">-</td><td>Cell 1 (mV)</td><td id="c1">-</td></tr>
    <tr><td>Cell 2 (mV)</td><td id="c2">-</td><td>Current (raw)</td><td id="iRaw">-</td></tr>
    <tr><td>Internal Temp (C)</td><td id="tInt">-</td><td>TS Measurement (raw)</td><td id="tsRaw">-</td></tr>
    <tr><td>SoC (%)</td><td id="soc">-</td><td>Flags</td><td id="flags">-</td><td></td></tr>
    <tr><td>BatteryStatus (0x12)</td><td id="bstat">-</td><td>AlarmStatus (0x62)</td><td id="astat">-</td></tr>
    <tr><td>AlarmRawStatus (0x64)</td><td id="araw">-</td><td>SafetyAlertA (0x02)</td><td id="safeaAlertRaw">-</td></tr>
    <tr><td>SafetyStatusA (0x03)</td><td id="safeaRaw">-</td><td>SafetyAlertB (0x04)</td><td id="safebAlertRaw">-</td></tr>
    <tr><td>SafetyStatusB (0x05)</td><td id="safebRaw">-</td><td>Mode</td><td id="mode">-</td></tr>
    <tr><td>Balance Gate</td><td id="gate">-</td><td>BQ Balancing</td><td id="bqbal">-</td></tr>
    <tr><td>Alarm</td><td id="alarm">-</td><td>Safety A / B Active</td><td><span id="safea">-</span> / <span id="safeb">-</span></td></tr>
  </table>

  <h2>Alarm Status</h2>
  <div id="alarmTiles" class="tiles"></div>
  <h2>Safety A</h2>
  <div id="safetyATiles" class="tiles"></div>
  <h2>Safety B</h2>
  <div id="safetyBTiles" class="tiles"></div>
  <h2>Battery Status</h2>
  <div id="batteryTiles" class="tiles"></div>

  <script>
    function hx(v,w){ return '0x' + v.toString(16).toUpperCase().padStart(w,'0'); }
    // kind: "alert" => red ALERT / green OK, "state" => green TRUE / gray FALSE
    const alarmDefs = [
      [15, 'Safety Fault Group A', 'alert'],
      [14, 'Safety Fault Group B', 'alert'],
      [13, 'Safety Alert Group A', 'alert'],
      [12, 'Safety Alert Group B', 'alert'],
      [11, 'Charge FET Disabled', 'alert'],
      [10, 'Discharge FET Disabled', 'alert'],
      [9,  'Shutdown Voltage Triggered', 'alert'],
      [8,  'Cell Balancing Active', 'state'],
      [7,  'Full Measurement Scan Complete', 'state'],
      [6,  'Voltage Scan Complete', 'state'],
      [5,  'Wake Event', 'state'],
      [4,  'Sleep Event', 'state'],
      [3,  'Timer Alarm', 'state'],
      [2,  'Initialization Complete', 'state'],
      [1,  'Charge Detector Toggled', 'state'],
      [0,  'Power-On Reset Flag', 'state']
    ];

    const safetyADefs = [
      [7, 'Cell Overvoltage Fault', 'alert'],
      [6, 'Cell Undervoltage Fault', 'alert'],
      [5, 'Short-Circuit Discharge Fault', 'alert'],
      [4, 'Overcurrent Discharge 1 Fault', 'alert'],
      [3, 'Overcurrent Discharge 2 Fault', 'alert'],
      [2, 'Overcurrent Charge Fault', 'alert'],
      [1, 'Current Protection Latch Fault', 'alert'],
      [0, 'REGOUT Fault', 'alert']
    ];

    const safetyBDefs = [
      [7, 'Overtemperature Discharge Fault', 'alert'],
      [6, 'Overtemperature Charge Fault', 'alert'],
      [5, 'Undertemperature Discharge Fault', 'alert'],
      [4, 'Undertemperature Charge Fault', 'alert'],
      [3, 'Internal Overtemperature Fault', 'alert'],
      [2, 'Host Watchdog Fault', 'alert'],
      [1, 'VREF Diagnostic Fault', 'alert'],
      [0, 'VSS Diagnostic Fault', 'alert']
    ];

    const batteryDefs = [
      [15, 'Sleep Mode Active', 'state'],
      [14, 'Deep Sleep Mode Active', 'state'],
      [13, 'Safety Alert Present', 'state'],
      [12, 'Safety Fault Present', 'state'],
      [11, 'Security State bit 1', 'state'],
      [10, 'Security State bit 0', 'state'],
      [8,  'Autonomous FET Control Enabled', 'state'],
      [7,  'Power-On Reset Flag', 'state'],
      [6,  'Sleep Allowed', 'state'],
      [5,  'Config Update Mode', 'state'],
      [4,  'ALERT Pin Asserted', 'state'],
      [3,  'CHG Driver Enabled', 'state'],
      [2,  'DSG Driver Enabled', 'state'],
      [1,  'Charge Detector High', 'state']
    ];

    function makeTiles(host, defs, currentValue, latchedValue){
      let html = '';
      for(const [bit, label, kind] of defs){
        const set = (currentValue & (1 << bit)) !== 0;
        const latched = (latchedValue & (1 << bit)) !== 0;
        let tileClass = 'neutral';
        let stateClass = 'neutral';
        let stateText = 'FALSE';

        if (kind === 'alert') {
          tileClass = set ? 'bad' : 'ok';
          stateClass = set ? 'bad' : 'ok';
          stateText = set ? '! ALERT' : 'OK';
        } else {
          tileClass = set ? 'ok' : 'neutral';
          stateClass = set ? 'ok' : 'neutral';
          stateText = set ? 'TRUE' : 'FALSE';
        }

        const latchBadge = (!set && latched) ? '<div class="latched">!</div>' : '';
        html += `<div class="tile ${tileClass}"><h3>${label}</h3><div class="state ${stateClass}">${stateText}</div><div>Bit ${bit}</div>${latchBadge}</div>`;
      }
      host.innerHTML = html;
    }
    async function poll(){
      try{
        const r = await fetch('/api', {cache:'no-store'});
        const d = await r.json();
        link.textContent = `${d.frame} / ${d.good_packets} / ${d.bad_packets} / ${d.last_packet_age_ms}ms`;
        c1.textContent = d.c1_mV;
        c2.textContent = d.c2_mV;
        iRaw.textContent = d.current_raw;
        tInt.textContent = d.int_temp_c;
        tsRaw.textContent = d.ts_raw;
        soc.textContent = d.soc_pct;
        bstat.textContent = hx(d.battery_status,4);
        astat.textContent = hx(d.alarm_status,4);
        araw.textContent = hx(d.alarm_raw_status,4);
        safeaAlertRaw.textContent = hx(d.safety_alert_a,2);
        safeaRaw.textContent = hx(d.safety_status_a,2);
        safebAlertRaw.textContent = hx(d.safety_alert_b,2);
        safebRaw.textContent = hx(d.safety_status_b,2);
        flags.textContent = hx(d.flags,2);
        mode.textContent = d.deep_sleep ? 'DEEPSLEEP' : 'NORMAL';
        gate.textContent = d.balance_gate ? 'ON' : 'OFF';
        bqbal.textContent = d.bq_balancing ? 'ON' : 'OFF';
        alarm.textContent = d.alarm ? 'ON' : 'OFF';
        safea.textContent = d.safety_a ? 'ON' : 'OFF';
        safeb.textContent = d.safety_b ? 'ON' : 'OFF';
        // Alarm tiles: current=AlarmRawStatus, latched=AlarmStatus.
        makeTiles(alarmTiles, alarmDefs, d.alarm_raw_status, d.alarm_seen);
        // Safety tiles: current=SafetyAlert, latched=SafetyStatus.
        makeTiles(safetyATiles, safetyADefs, d.safety_alert_a, d.safety_a_seen);
        makeTiles(safetyBTiles, safetyBDefs, d.safety_alert_b, d.safety_b_seen);
        makeTiles(batteryTiles, batteryDefs, d.battery_status, d.battery_seen);
      }catch(e){}
    }
    setInterval(poll, 500);
    poll();
  </script>
</body>
</html>
)HTML";

static void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

static void handleApi() {
  const uint32_t age = (lastGoodPacketMs == 0) ? 0xFFFFFFFFUL : (millis() - lastGoodPacketMs);
  char body[520];
  snprintf(
    body, sizeof(body),
    "{\"frame\":%u,\"c1_mV\":%u,\"c2_mV\":%u,\"current_raw\":%d,\"int_temp_c\":%d,\"ts_raw\":%u,\"soc_pct\":%u,"
    "\"battery_status\":%u,\"alarm_status\":%u,\"alarm_raw_status\":%u,"
    "\"safety_alert_a\":%u,\"safety_status_a\":%u,\"safety_alert_b\":%u,\"safety_status_b\":%u,"
    "\"alarm_seen\":%u,\"safety_a_seen\":%u,\"safety_b_seen\":%u,\"battery_seen\":%u,"
    "\"flags\":%u,\"deep_sleep\":%s,\"balance_gate\":%s,"
    "\"bq_balancing\":%s,\"alarm\":%s,\"safety_a\":%s,\"safety_b\":%s,"
    "\"good_packets\":%lu,\"bad_packets\":%lu,\"last_packet_age_ms\":%lu}",
    frame, c1, c2, currentRaw, intTempC, tsRaw, soc, batteryStatus, alarmStatus, alarmRawStatus,
    safetyAlertA, safetyStatusA, safetyAlertB, safetyStatusB,
    alarmSeen, safetyASeen, safetyBSeen, batterySeen, flags,
    (flags & 0x01) ? "true" : "false",
    (flags & 0x02) ? "true" : "false",
    (flags & 0x04) ? "true" : "false",
    (flags & 0x08) ? "true" : "false",
    (flags & 0x10) ? "true" : "false",
    (flags & 0x20) ? "true" : "false",
    (unsigned long)goodPackets, (unsigned long)badPackets, (unsigned long)age
  );
  server.send(200, "application/json", body);
}

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(DIAG_RX_PIN, INPUT);
  diag.begin(2400, SERIAL_8N1, DIAG_RX_PIN, -1); // RX only
  diag.setTimeout(5);
  delay(200);
  Serial.println("ESP32 BQ monitor starting...");

  connectWifi();
  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  while (diag.available()) {
    const uint8_t b = (uint8_t)diag.read();

    // Resync on two-byte sync word: A5 5A, then collect fixed-length packet.
    if (pktIdx == 0) {
      if (b != SYNC_BYTE) continue;
      pkt[pktIdx++] = b;
      continue;
    }
    if (pktIdx == 1) {
      if (b == SYNC2_BYTE) {
        pkt[pktIdx++] = b;
      } else if (b == SYNC_BYTE) {
        pkt[0] = SYNC_BYTE;
      } else {
        pktIdx = 0;
      }
      continue;
    }

    pkt[pktIdx++] = b;
    if (pktIdx < PKT_LEN) continue;

    uint8_t cs = 0;
    for (uint8_t i = 0; i < PKT_LEN - 1; i++) cs ^= pkt[i];
    if (cs != pkt[PKT_LEN - 1]) {
      badPackets++;
      pktIdx = 0;
      continue;
    }

    frame = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
    c1 = (uint16_t)pkt[4] | ((uint16_t)pkt[5] << 8);
    c2 = (uint16_t)pkt[6] | ((uint16_t)pkt[7] << 8);
    currentRaw = (int16_t)((uint16_t)pkt[8] | ((uint16_t)pkt[9] << 8));
    intTempC = (int16_t)((uint16_t)pkt[10] | ((uint16_t)pkt[11] << 8));
    tsRaw = (uint16_t)pkt[12] | ((uint16_t)pkt[13] << 8);
    soc = pkt[14];
    batteryStatus = (uint16_t)pkt[15] | ((uint16_t)pkt[16] << 8);
    alarmStatus = (uint16_t)pkt[17] | ((uint16_t)pkt[18] << 8);
    alarmRawStatus = (uint16_t)pkt[19] | ((uint16_t)pkt[20] << 8);
    safetyAlertA = pkt[21];
    safetyStatusA = pkt[22];
    safetyAlertB = pkt[23];
    safetyStatusB = pkt[24];
    flags = pkt[25];
    alarmSeen |= alarmStatus;
    safetyASeen |= (uint8_t)(safetyAlertA | safetyStatusA);
    safetyBSeen |= (uint8_t)(safetyAlertB | safetyStatusB);
    batterySeen |= batteryStatus;
    lastGoodPacketMs = millis();
    goodPackets++;
    pktIdx = 0;
  }

  server.handleClient();
}
