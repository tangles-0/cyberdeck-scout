#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

/*
  EDID writer for WEMOS D1 Mini ESP32.

  Wiring for a 24LC02B:
    ESP32 GPIO21/SDA -> EEPROM SDA
    ESP32 GPIO22/SCL -> EEPROM SCL
    3V3             -> EEPROM VCC, A0, A1, A2
    GND             -> EEPROM GND and WP for writing

  The sketch joins the configured Wi-Fi network and prints its IP address.
  It generates one base 128-byte EDID block, can download it as hex,
  and can flash/verify bytes 0x00-0x7f on an EEPROM responding at 0x50.
*/

static const char *WIFI_SSID = "Tangles";
static const char *WIFI_PASSWORD = "Jiblet!1337";
static const char *WIFI_HOSTNAME = "edid-writer";

static const uint8_t I2C_SDA_PIN = 21;
static const uint8_t I2C_SCL_PIN = 22;
static const uint8_t EEPROM_I2C_ADDR = 0x50;
static const uint8_t EEPROM_PAGE_SIZE = 8;
static const size_t EDID_SIZE = 128;
static const uint8_t MAX_TIMINGS = 4;

WebServer server(80);

struct EdidTiming {
  uint16_t hActive;
  uint16_t vActive;
  uint32_t pixelClockKHz;
  uint16_t hFrontPorch;
  uint16_t hSyncWidth;
  uint16_t hBackPorch;
  uint16_t vFrontPorch;
  uint16_t vSyncWidth;
  uint16_t vBackPorch;
  bool hSyncPositive;
  bool vSyncPositive;
};

struct EdidConfig {
  String manufacturer;
  String monitorName;
  uint16_t productCode;
  uint32_t serialNumber;
  uint8_t week;
  uint16_t year;
  uint8_t widthCm;
  uint8_t heightCm;
  uint8_t timingCount;
  EdidTiming timings[MAX_TIMINGS];
  uint8_t minRefreshHz;
  uint8_t maxRefreshHz;
  uint8_t minHorizontalKHz;
  uint8_t maxHorizontalKHz;
};

struct FlashJob {
  bool active;
  bool done;
  bool success;
  uint8_t progress;
  uint8_t state;
  size_t offset;
  unsigned long waitUntilMs;
  String message;
  uint8_t edid[EDID_SIZE];
};

FlashJob flashJob = {};

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 EDID Writer</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #09111f;
      --card: #121c2d;
      --card2: #182438;
      --accent: #6ee7b7;
      --accent2: #60a5fa;
      --bad: #fb7185;
      --text: #e5eefc;
      --muted: #9db0ca;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background: radial-gradient(circle at top left, #1e3a5f 0, transparent 32rem), var(--bg);
      color: var(--text);
    }
    main { width: min(1120px, calc(100vw - 28px)); margin: 0 auto; padding: 28px 0 48px; }
    header { display: flex; justify-content: space-between; gap: 18px; align-items: flex-start; margin-bottom: 20px; }
    h1 { margin: 0; font-size: clamp(2rem, 6vw, 4rem); line-height: 0.95; letter-spacing: -0.06em; }
    h2 { margin: 0 0 16px; font-size: 1rem; color: var(--accent); letter-spacing: 0.08em; text-transform: uppercase; }
    p { color: var(--muted); line-height: 1.55; margin: 8px 0 0; }
    .pill {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      border: 1px solid rgba(255,255,255,0.12);
      background: rgba(255,255,255,0.06);
      border-radius: 999px;
      padding: 10px 14px;
      white-space: nowrap;
      font-weight: 650;
    }
    .dot { width: 10px; height: 10px; border-radius: 999px; background: #f59e0b; box-shadow: 0 0 22px #f59e0b; }
    .ok .dot { background: var(--accent); box-shadow: 0 0 22px var(--accent); }
    .bad .dot { background: var(--bad); box-shadow: 0 0 22px var(--bad); }
    .grid { display: grid; grid-template-columns: repeat(12, 1fr); gap: 16px; }
    .card {
      grid-column: span 12;
      padding: 18px;
      border: 1px solid rgba(255,255,255,0.10);
      border-radius: 24px;
      background: linear-gradient(180deg, rgba(255,255,255,0.07), rgba(255,255,255,0.025)), var(--card);
      box-shadow: 0 20px 70px rgba(0,0,0,0.28);
    }
    .half { grid-column: span 6; }
    .third { grid-column: span 4; }
    .fields { display: grid; grid-template-columns: repeat(12, 1fr); gap: 12px; }
    label { display: grid; gap: 7px; grid-column: span 4; color: var(--muted); font-size: 0.82rem; font-weight: 650; }
    label.wide { grid-column: span 6; }
    .tabs {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
      margin-bottom: 14px;
    }
    .tab {
      border: 1px solid rgba(255,255,255,0.12);
      background: rgba(255,255,255,0.06);
      color: var(--text);
      padding: 9px 12px;
      border-radius: 999px;
      font-size: 0.9rem;
    }
    .tab.active {
      background: var(--accent);
      color: #06111f;
    }
    .timing-panel[hidden], .tab[hidden] { display: none; }
    input, select {
      width: 100%;
      border: 1px solid rgba(255,255,255,0.12);
      border-radius: 14px;
      background: var(--card2);
      color: var(--text);
      padding: 12px;
      font: inherit;
      outline: none;
    }
    input:focus, select:focus { border-color: var(--accent2); box-shadow: 0 0 0 3px rgba(96,165,250,0.18); }
    .actions { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; margin-top: 16px; }
    button {
      border: 0;
      border-radius: 16px;
      padding: 13px 18px;
      color: #06111f;
      background: var(--accent);
      font: inherit;
      font-weight: 800;
      cursor: pointer;
    }
    button.secondary { background: var(--accent2); color: #07111f; }
    button:disabled { cursor: not-allowed; opacity: 0.42; filter: grayscale(1); }
    .hint { font-size: 0.9rem; color: var(--muted); }
    .meter {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      margin-top: 12px;
    }
    .meter div {
      border-radius: 16px;
      background: rgba(255,255,255,0.06);
      padding: 12px;
      color: var(--muted);
    }
    .meter strong { display: block; color: var(--text); font-size: 1.15rem; margin-top: 4px; }
    .progress {
      width: 100%;
      height: 14px;
      overflow: hidden;
      border-radius: 999px;
      background: rgba(255,255,255,0.08);
      margin-top: 16px;
    }
    .bar {
      width: 0;
      height: 100%;
      border-radius: inherit;
      background: linear-gradient(90deg, var(--accent2), var(--accent));
      transition: width 180ms ease;
    }
    .message {
      min-height: 1.4em;
      margin-top: 10px;
      color: var(--muted);
      font-weight: 650;
    }
    .message.ok { color: var(--accent); }
    .message.bad { color: var(--bad); }
    .formula {
      grid-column: span 12;
      border-radius: 16px;
      background: rgba(96,165,250,0.10);
      border: 1px solid rgba(96,165,250,0.22);
      color: var(--muted);
      padding: 12px;
      margin: 0;
    }
    details {
      margin-top: 14px;
      color: var(--muted);
    }
    summary {
      cursor: pointer;
      font-weight: 800;
      color: var(--accent2);
    }
    pre {
      overflow: auto;
      border-radius: 16px;
      background: rgba(0,0,0,0.28);
      border: 1px solid rgba(255,255,255,0.08);
      color: var(--text);
      padding: 14px;
      white-space: pre-wrap;
    }
    @media (max-width: 760px) {
      header { display: grid; }
      .half, .third, label, label.wide { grid-column: span 12; }
      .meter { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <div>
        <h1>ESP32 EDID Writer</h1>
        <p>Edit a base EDID timing block, download the generated hex, or flash a 24LC02B over I2C.</p>
      </div>
      <div id="status" class="pill"><span class="dot"></span><span>Checking EEPROM...</span></div>
    </header>

    <form id="edidForm" method="post">
      <div class="grid">
        <section class="card half">
          <h2>Display Identity</h2>
          <div class="fields">
            <label>Manufacturer ID
              <input name="manufacturer" maxlength="3" pattern="[A-Za-z]{3}" value="AOC" required>
            </label>
            <label>Product Code
              <input name="product" type="number" min="0" max="65535" value="4" required>
            </label>
            <label>Serial Number
              <input name="serial" type="number" min="0" max="4294967295" value="1" required>
            </label>
            <label class="wide">Monitor Name
              <input name="name" maxlength="13" value="ESP32 EDID">
            </label>
            <label>Week
              <input name="week" type="number" min="1" max="53" value="1">
            </label>
            <label>Year
              <input name="year" type="number" min="1990" max="2245" value="2024">
            </label>
            <label>Width (cm)
              <input name="widthCm" type="number" min="1" max="255" value="15">
            </label>
            <label>Height (cm)
              <input name="heightCm" type="number" min="1" max="255" value="10">
            </label>
          </div>
        </section>

        <section class="card half">
          <h2>Detailed Timing</h2>
          <input id="timingCount" name="timingCount" type="hidden" value="1">
          <div class="tabs">
            <button class="tab active" type="button" data-tab="0">Timing 1</button>
            <button class="tab" type="button" data-tab="1" hidden>Timing 2</button>
            <button class="tab" type="button" data-tab="2" hidden>Timing 3</button>
            <button class="tab" type="button" data-tab="3" hidden>Timing 4</button>
            <button class="secondary" id="addTimingButton" type="button">Add Timing</button>
            <button class="secondary" id="removeTimingButton" type="button" disabled>Remove Timing</button>
          </div>
          <div class="fields timing-panel" data-panel="0">
            <label>Horizontal Active
              <input name="hActive0" type="number" min="1" max="4095" value="800" required>
            </label>
            <label>Vertical Active
              <input name="vActive0" type="number" min="1" max="4095" value="480" required>
            </label>
            <label>Refresh Hz
              <input name="refresh0" type="number" min="1" max="240" step="0.01" value="60" disabled>
            </label>
            <label>Pixel Clock (kHz)
              <input name="pixelClock0" type="number" min="1000" max="655350" value="32000" required>
            </label>
            <p class="formula" data-formula="0">Refresh = pixel clock / total pixels.</p>
            <label>H Front Porch
              <input name="hFront0" type="number" min="0" max="1023" value="40">
            </label>
            <label>H Sync Width
              <input name="hSync0" type="number" min="1" max="1023" value="48">
            </label>
            <label>H Back Porch
              <input name="hBack0" type="number" min="0" max="1023" value="40">
            </label>
            <label>V Front Porch
              <input name="vFront0" type="number" min="0" max="63" value="13">
            </label>
            <label>V Sync Width
              <input name="vSync0" type="number" min="1" max="63" value="3">
            </label>
            <label>V Back Porch
              <input name="vBack0" type="number" min="0" max="63" value="29">
            </label>
            <label>H Sync Polarity
              <select name="hPol0"><option value="0" selected>Negative</option><option value="1">Positive</option></select>
            </label>
            <label>V Sync Polarity
              <select name="vPol0"><option value="0" selected>Negative</option><option value="1">Positive</option></select>
            </label>
          </div>
          <div class="fields timing-panel" data-panel="1" hidden></div>
          <div class="fields timing-panel" data-panel="2" hidden></div>
          <div class="fields timing-panel" data-panel="3" hidden></div>
        </section>

        <section class="card third">
          <h2>Range Limits</h2>
          <div class="fields">
            <label class="wide">Min Refresh Hz
              <input name="minRefresh" type="number" min="1" max="255" value="50">
            </label>
            <label class="wide">Max Refresh Hz
              <input name="maxRefresh" type="number" min="1" max="255" value="75">
            </label>
            <label class="wide">Min H kHz
              <input name="minH" type="number" min="1" max="255" value="30">
            </label>
            <label class="wide">Max H kHz
              <input name="maxH" type="number" min="1" max="255" value="85">
            </label>
          </div>
        </section>

        <section class="card">
          <h2>Output</h2>
          <p>The generated EDID is checksummed on the ESP32 each time. Flashing writes and verifies the first 128 bytes at EEPROM offset 0x00.</p>
          <div class="meter">
            <div>Total pixels/frame<strong id="totalPixels">-</strong></div>
            <div>Calculated refresh<strong id="calcRefresh">-</strong></div>
            <div>EEPROM target<strong>0x50 / 24LC02B</strong></div>
          </div>
          <div class="actions">
            <button class="secondary" type="submit" formaction="/download">Download Hex</button>
            <button class="secondary" id="loadButton" type="button" disabled>Load from EEPROM</button>
            <button id="flashButton" type="button" disabled>Flash EEPROM</button>
            <span class="hint">WP must be tied low for writes. Refresh this page after changing wiring.</span>
          </div>
          <div class="progress" aria-label="Flash progress"><div id="progressBar" class="bar"></div></div>
          <div id="message" class="message"></div>
          <details id="loadedHexDetails" hidden>
            <summary>Loaded EEPROM hex</summary>
            <pre id="loadedHex"></pre>
          </details>
        </section>
      </div>
    </form>
  </main>

  <script>
    const form = document.getElementById('edidForm');
    const statusEl = document.getElementById('status');
    const flashButton = document.getElementById('flashButton');
    const loadButton = document.getElementById('loadButton');
    const progressBar = document.getElementById('progressBar');
    const messageEl = document.getElementById('message');
    const loadedHexDetails = document.getElementById('loadedHexDetails');
    const loadedHexEl = document.getElementById('loadedHex');
    const totalPixelsEl = document.getElementById('totalPixels');
    const calcRefreshEl = document.getElementById('calcRefresh');
    const timingCountInput = document.getElementById('timingCount');
    const addTimingButton = document.getElementById('addTimingButton');
    const removeTimingButton = document.getElementById('removeTimingButton');
    const tabs = [...document.querySelectorAll('[data-tab]')];
    const panels = [...document.querySelectorAll('[data-panel]')];
    const timingTemplate = panels[0].innerHTML;
    let eepromDetected = false;
    let timingCount = 1;
    let activeTiming = 0;

    function setMessage(text, kind = '') {
      messageEl.className = kind ? 'message ' + kind : 'message';
      messageEl.textContent = text;
    }

    function setBusy(busy) {
      flashButton.disabled = busy || !eepromDetected;
      loadButton.disabled = busy || !eepromDetected;
    }

    async function refreshStatus() {
      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        const status = await response.json();
        eepromDetected = !!status.detected;
        statusEl.className = status.detected ? 'pill ok' : 'pill bad';
        statusEl.querySelector('span:last-child').textContent =
          status.detected ? 'EEPROM detected at 0x50' : 'EEPROM not detected';
        setBusy(false);
      } catch (error) {
        eepromDetected = false;
        statusEl.className = 'pill bad';
        statusEl.querySelector('span:last-child').textContent = 'Status unavailable';
        setBusy(false);
      }
    }

    function timingName(base, index) {
      return base + index;
    }

    function timingField(index, base) {
      return form.querySelector('[name="' + timingName(base, index) + '"]');
    }

    function makeTimingPanel(index) {
      panels[index].innerHTML = timingTemplate;
      panels[index].querySelectorAll('[name]').forEach((field) => {
        field.name = field.name.replace(/0$/, String(index));
      });
      const formula = panels[index].querySelector('[data-formula]');
      if (formula) {
        formula.dataset.formula = String(index);
      }
    }

    function updateTimingTabs() {
      timingCountInput.value = timingCount;
      tabs.forEach((tab, index) => {
        tab.hidden = index >= timingCount;
        tab.classList.toggle('active', index === activeTiming);
      });
      panels.forEach((panel, index) => {
        panel.hidden = index !== activeTiming;
      });
      addTimingButton.disabled = timingCount >= 4;
      removeTimingButton.disabled = timingCount <= 1;
    }

    function setActiveTiming(index) {
      activeTiming = Math.max(0, Math.min(index, timingCount - 1));
      updateTimingTabs();
      updatePreview();
    }

    function copyTiming(source, target) {
      ['hActive', 'vActive', 'pixelClock', 'hFront', 'hSync', 'hBack', 'vFront', 'vSync', 'vBack', 'hPol', 'vPol'].forEach((base) => {
        const sourceField = timingField(source, base);
        const targetField = timingField(target, base);
        if (sourceField && targetField) {
          targetField.value = sourceField.value;
        }
      });
    }

    function addTiming() {
      if (timingCount >= 4) {
        return;
      }
      const next = timingCount;
      copyTiming(activeTiming, next);
      timingCount++;
      setActiveTiming(next);
    }

    function removeTiming() {
      if (timingCount <= 1) {
        return;
      }
      for (let i = activeTiming; i < timingCount - 1; i++) {
        copyTiming(i + 1, i);
      }
      timingCount--;
      setActiveTiming(Math.min(activeTiming, timingCount - 1));
    }

    function updatePreview() {
      const data = new FormData(form);
      const i = activeTiming;
      const hActive = Number(data.get(timingName('hActive', i))) || 0;
      const vActive = Number(data.get(timingName('vActive', i))) || 0;
      const hBlank = (Number(data.get(timingName('hFront', i))) || 0) + (Number(data.get(timingName('hSync', i))) || 0) + (Number(data.get(timingName('hBack', i))) || 0);
      const vBlank = (Number(data.get(timingName('vFront', i))) || 0) + (Number(data.get(timingName('vSync', i))) || 0) + (Number(data.get(timingName('vBack', i))) || 0);
      const pixelClock = Number(data.get(timingName('pixelClock', i))) || 0;
      const totalPixels = (hActive + hBlank) * (vActive + vBlank);
      const refresh = totalPixels ? pixelClock * 1000 / totalPixels : 0;
      totalPixelsEl.textContent = totalPixels ? totalPixels.toLocaleString() : '-';
      calcRefreshEl.textContent = refresh ? refresh.toFixed(2) + ' Hz' : '-';
      timingField(i, 'refresh').value = refresh ? refresh.toFixed(2) : '';
      form.querySelector('[data-formula="' + i + '"]').textContent = refresh
        ? `Refresh = ${pixelClock.toLocaleString()} kHz * 1000 / ${totalPixels.toLocaleString()} pixels = ${refresh.toFixed(2)} Hz`
        : 'Refresh = pixel clock / total pixels.';
    }

    function applyConfig(config) {
      if (!config || typeof config !== 'object') {
        throw new Error('Load succeeded, but no form values were returned.');
      }
      const timings = Array.isArray(config.timings) && config.timings.length ? config.timings.slice(0, 4) : [config];
      timingCount = timings.length;
      timings.forEach((timing, index) => {
        for (const [name, value] of Object.entries(timing)) {
          const field = timingField(index, name);
          if (field) {
            field.value = String(value);
          }
        }
      });
      for (const [name, value] of Object.entries(config)) {
        if (name === 'timings' || name === 'timingCount') {
          continue;
        }
        const field = form.querySelector('[name="' + name.replace(/["\\]/g, '\\$&') + '"]');
        if (field) {
          field.value = String(value);
        }
      }
      setActiveTiming(0);
      updatePreview();
    }

    async function loadFromEeprom() {
      setBusy(true);
      setMessage('Reading EDID from EEPROM...');
      try {
        const response = await fetch('/api/load', { cache: 'no-store' });
        const result = await response.json();
        if (!response.ok || !result.ok) {
          throw new Error(result.message || 'Load failed');
        }
        applyConfig(result.config);
        loadedHexEl.textContent = result.hex || '';
        loadedHexDetails.hidden = !result.hex;
        setMessage('Loaded a valid EDID from EEPROM.', 'ok');
      } catch (error) {
        setMessage(error.message, 'bad');
      } finally {
        setBusy(false);
      }
    }

    async function pollFlashProgress() {
      const response = await fetch('/api/flash/progress', { cache: 'no-store' });
      const progress = await response.json();
      progressBar.style.width = progress.progress + '%';
      setMessage(progress.message, progress.done ? (progress.success ? 'ok' : 'bad') : '');
      if (!progress.done) {
        setTimeout(pollFlashProgress, 180);
        return;
      }
      setBusy(false);
      refreshStatus();
    }

    async function flashEeprom() {
      setBusy(true);
      progressBar.style.width = '0%';
      setMessage('Starting EEPROM flash...');
      try {
        const response = await fetch('/api/flash/start', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams(new FormData(form))
        });
        const result = await response.json();
        if (!response.ok || !result.ok) {
          throw new Error(result.message || 'Flash failed to start');
        }
        pollFlashProgress();
      } catch (error) {
        setMessage(error.message, 'bad');
        setBusy(false);
      }
    }

    form.addEventListener('input', updatePreview);
    tabs.forEach((tab, index) => tab.addEventListener('click', () => setActiveTiming(index)));
    addTimingButton.addEventListener('click', addTiming);
    removeTimingButton.addEventListener('click', removeTiming);
    loadButton.addEventListener('click', loadFromEeprom);
    flashButton.addEventListener('click', flashEeprom);
    makeTimingPanel(1);
    makeTimingPanel(2);
    makeTimingPanel(3);
    updateTimingTabs();
    refreshStatus();
    updatePreview();
    setInterval(refreshStatus, 5000);
  </script>
</body>
</html>
)rawliteral";

uint16_t clampU16(long value, uint16_t minValue, uint16_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return static_cast<uint16_t>(value);
}

uint32_t clampU32(unsigned long value, uint32_t maxValue) {
  if (value > maxValue) {
    return maxValue;
  }
  return static_cast<uint32_t>(value);
}

String argString(const char *name, const String &fallback) {
  if (!server.hasArg(name)) {
    return fallback;
  }
  String value = server.arg(name);
  value.trim();
  return value.length() ? value : fallback;
}

uint16_t argU16(const char *name, uint16_t fallback, uint16_t minValue, uint16_t maxValue) {
  if (!server.hasArg(name)) {
    return fallback;
  }
  return clampU16(server.arg(name).toInt(), minValue, maxValue);
}

uint32_t argU32(const char *name, uint32_t fallback, uint32_t maxValue) {
  if (!server.hasArg(name)) {
    return fallback;
  }
  return clampU32(strtoul(server.arg(name).c_str(), nullptr, 10), maxValue);
}

bool argBool(const char *name, bool fallback) {
  if (!server.hasArg(name)) {
    return fallback;
  }
  return server.arg(name) == "1";
}

String indexedName(const char *base, uint8_t index) {
  return String(base) + String(index);
}

uint16_t timingArgU16(const char *base, uint8_t index, uint16_t fallback, uint16_t minValue, uint16_t maxValue) {
  String name = indexedName(base, index);
  if (server.hasArg(name)) {
    return clampU16(server.arg(name).toInt(), minValue, maxValue);
  }
  if (index == 0) {
    return argU16(base, fallback, minValue, maxValue);
  }
  return fallback;
}

uint32_t timingArgU32(const char *base, uint8_t index, uint32_t fallback, uint32_t maxValue) {
  String name = indexedName(base, index);
  if (server.hasArg(name)) {
    return clampU32(strtoul(server.arg(name).c_str(), nullptr, 10), maxValue);
  }
  if (index == 0) {
    return argU32(base, fallback, maxValue);
  }
  return fallback;
}

bool timingArgBool(const char *base, uint8_t index, bool fallback) {
  String name = indexedName(base, index);
  if (server.hasArg(name)) {
    return server.arg(name) == "1";
  }
  if (index == 0) {
    return argBool(base, fallback);
  }
  return fallback;
}

String sanitizeManufacturer(String value) {
  value.toUpperCase();
  String clean;
  for (size_t i = 0; i < value.length() && clean.length() < 3; i++) {
    char c = value[i];
    if (c >= 'A' && c <= 'Z') {
      clean += c;
    }
  }
  while (clean.length() < 3) {
    clean += 'X';
  }
  return clean;
}

String sanitizeDescriptorText(String value) {
  String clean;
  for (size_t i = 0; i < value.length() && clean.length() < 13; i++) {
    char c = value[i];
    if (c >= 32 && c <= 126) {
      clean += c;
    }
  }
  return clean;
}

EdidConfig readConfigFromRequest() {
  EdidConfig cfg;
  cfg.manufacturer = sanitizeManufacturer(argString("manufacturer", "AOC"));
  cfg.monitorName = sanitizeDescriptorText(argString("name", "ESP32 EDID"));
  cfg.productCode = argU16("product", 4, 0, 65535);
  cfg.serialNumber = argU32("serial", 1, 4294967295UL);
  cfg.week = static_cast<uint8_t>(argU16("week", 1, 1, 53));
  cfg.year = argU16("year", 2024, 1990, 2245);
  cfg.widthCm = static_cast<uint8_t>(argU16("widthCm", 15, 1, 255));
  cfg.heightCm = static_cast<uint8_t>(argU16("heightCm", 10, 1, 255));
  cfg.timingCount = static_cast<uint8_t>(argU16("timingCount", 1, 1, MAX_TIMINGS));
  for (uint8_t i = 0; i < cfg.timingCount; i++) {
    EdidTiming &timing = cfg.timings[i];
    timing.hActive = timingArgU16("hActive", i, 800, 1, 4095);
    timing.vActive = timingArgU16("vActive", i, 480, 1, 4095);
    timing.pixelClockKHz = timingArgU32("pixelClock", i, 32000, 655350UL);
    if (timing.pixelClockKHz < 1000) {
      timing.pixelClockKHz = 1000;
    }
    timing.hFrontPorch = timingArgU16("hFront", i, 40, 0, 1023);
    timing.hSyncWidth = timingArgU16("hSync", i, 48, 1, 1023);
    timing.hBackPorch = timingArgU16("hBack", i, 40, 0, 1023);
    timing.vFrontPorch = timingArgU16("vFront", i, 13, 0, 63);
    timing.vSyncWidth = timingArgU16("vSync", i, 3, 1, 63);
    timing.vBackPorch = timingArgU16("vBack", i, 29, 0, 63);
    timing.hSyncPositive = timingArgBool("hPol", i, false);
    timing.vSyncPositive = timingArgBool("vPol", i, false);
  }
  cfg.minRefreshHz = static_cast<uint8_t>(argU16("minRefresh", 50, 1, 255));
  cfg.maxRefreshHz = static_cast<uint8_t>(argU16("maxRefresh", 75, 1, 255));
  cfg.minHorizontalKHz = static_cast<uint8_t>(argU16("minH", 30, 1, 255));
  cfg.maxHorizontalKHz = static_cast<uint8_t>(argU16("maxH", 85, 1, 255));
  return cfg;
}

void encodeManufacturer(const String &manufacturer, uint8_t *edid) {
  uint16_t code = ((manufacturer[0] - 'A' + 1) << 10) |
                  ((manufacturer[1] - 'A' + 1) << 5) |
                  (manufacturer[2] - 'A' + 1);
  edid[8] = static_cast<uint8_t>(code >> 8);
  edid[9] = static_cast<uint8_t>(code & 0xff);
}

void writeLe16(uint8_t *target, uint16_t value) {
  target[0] = static_cast<uint8_t>(value & 0xff);
  target[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t *target, uint32_t value) {
  target[0] = static_cast<uint8_t>(value & 0xff);
  target[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  target[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  target[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

void writeDetailedTiming(const EdidConfig &cfg, const EdidTiming &timing, uint8_t *dtd) {
  uint16_t pixelClock10KHz = static_cast<uint16_t>(timing.pixelClockKHz / 10);
  uint16_t hBlank = timing.hFrontPorch + timing.hSyncWidth + timing.hBackPorch;
  uint16_t vBlank = timing.vFrontPorch + timing.vSyncWidth + timing.vBackPorch;
  uint16_t hImageMm = static_cast<uint16_t>(cfg.widthCm) * 10;
  uint16_t vImageMm = static_cast<uint16_t>(cfg.heightCm) * 10;

  memset(dtd, 0, 18);
  writeLe16(&dtd[0], pixelClock10KHz);
  dtd[2] = timing.hActive & 0xff;
  dtd[3] = hBlank & 0xff;
  dtd[4] = ((timing.hActive >> 8) << 4) | ((hBlank >> 8) & 0x0f);
  dtd[5] = timing.vActive & 0xff;
  dtd[6] = vBlank & 0xff;
  dtd[7] = ((timing.vActive >> 8) << 4) | ((vBlank >> 8) & 0x0f);
  dtd[8] = timing.hFrontPorch & 0xff;
  dtd[9] = timing.hSyncWidth & 0xff;
  dtd[10] = ((timing.vFrontPorch & 0x0f) << 4) | (timing.vSyncWidth & 0x0f);
  dtd[11] = ((timing.hFrontPorch >> 8) << 6) |
            ((timing.hSyncWidth >> 8) << 4) |
            ((timing.vFrontPorch >> 4) << 2) |
            (timing.vSyncWidth >> 4);
  dtd[12] = hImageMm & 0xff;
  dtd[13] = vImageMm & 0xff;
  dtd[14] = ((hImageMm >> 8) << 4) | ((vImageMm >> 8) & 0x0f);
  dtd[17] = 0x18;
  if (timing.vSyncPositive) {
    dtd[17] |= 0x04;
  }
  if (timing.hSyncPositive) {
    dtd[17] |= 0x02;
  }
}

void writeTextDescriptor(uint8_t *descriptor, uint8_t tag, const String &text) {
  memset(descriptor, 0, 18);
  descriptor[3] = tag;
  for (uint8_t i = 5; i < 18; i++) {
    descriptor[i] = ' ';
  }
  uint8_t idx = 5;
  for (size_t i = 0; i < text.length() && idx < 18; i++) {
    descriptor[idx++] = text[i];
  }
  if (idx < 18) {
    descriptor[idx] = 0x0a;
  }
}

void writeRangeDescriptor(const EdidConfig &cfg, uint8_t *descriptor) {
  memset(descriptor, 0, 18);
  uint32_t highestPixelClockKHz = 0;
  for (uint8_t i = 0; i < cfg.timingCount; i++) {
    if (cfg.timings[i].pixelClockKHz > highestPixelClockKHz) {
      highestPixelClockKHz = cfg.timings[i].pixelClockKHz;
    }
  }
  uint32_t maxClock10MHz = (highestPixelClockKHz + 9999) / 10000;
  if (maxClock10MHz > 255) {
    maxClock10MHz = 255;
  }

  descriptor[3] = 0xfd;
  descriptor[5] = cfg.minRefreshHz;
  descriptor[6] = cfg.maxRefreshHz;
  descriptor[7] = cfg.minHorizontalKHz;
  descriptor[8] = cfg.maxHorizontalKHz;
  descriptor[9] = static_cast<uint8_t>(maxClock10MHz);
  descriptor[10] = 0x0a;
  for (uint8_t i = 11; i < 18; i++) {
    descriptor[i] = ' ';
  }
}

void writeUnusedDescriptor(uint8_t *descriptor) {
  memset(descriptor, 0, 18);
  descriptor[3] = 0x10;
}

void generateEdid(const EdidConfig &cfg, uint8_t *edid) {
  memset(edid, 0, EDID_SIZE);
  const uint8_t header[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
  memcpy(edid, header, sizeof(header));

  encodeManufacturer(cfg.manufacturer, edid);
  writeLe16(&edid[10], cfg.productCode);
  writeLe32(&edid[12], cfg.serialNumber);
  edid[16] = cfg.week;
  edid[17] = static_cast<uint8_t>(cfg.year - 1990);
  edid[18] = 0x01;
  edid[19] = 0x03;
  edid[20] = 0x80;
  edid[21] = cfg.widthCm;
  edid[22] = cfg.heightCm;
  edid[23] = 0x78;
  edid[24] = 0x0a;

  for (uint8_t i = 38; i <= 53; i++) {
    edid[i] = 0x01;
  }

  uint8_t descriptorIndex = 0;
  for (uint8_t i = 0; i < cfg.timingCount && descriptorIndex < 4; i++, descriptorIndex++) {
    writeDetailedTiming(cfg, cfg.timings[i], &edid[54 + descriptorIndex * 18]);
  }
  if (descriptorIndex < 4) {
    writeTextDescriptor(&edid[54 + descriptorIndex * 18], 0xfc, cfg.monitorName);
    descriptorIndex++;
  }
  if (descriptorIndex < 4) {
    writeRangeDescriptor(cfg, &edid[54 + descriptorIndex * 18]);
    descriptorIndex++;
  }
  while (descriptorIndex < 4) {
    writeUnusedDescriptor(&edid[54 + descriptorIndex * 18]);
    descriptorIndex++;
  }

  edid[126] = 0x00;
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 127; i++) {
    sum += edid[i];
  }
  edid[127] = static_cast<uint8_t>(256 - sum);
}

String edidToHex(const uint8_t *edid) {
  String out;
  out.reserve(850);
  char byteText[8];
  for (size_t i = 0; i < EDID_SIZE; i++) {
    snprintf(byteText, sizeof(byteText), "0x%02X", edid[i]);
    out += byteText;
    if (i + 1 < EDID_SIZE) {
      out += ",";
    }
    out += ((i + 1) % 8 == 0) ? "\n" : " ";
  }
  return out;
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c >= 32) {
      escaped += c;
    }
  }
  return escaped;
}

void sendJson(uint16_t statusCode, const String &json) {
  server.send(statusCode, "application/json", json);
}

uint16_t readLe16(const uint8_t *source) {
  return static_cast<uint16_t>(source[0]) | (static_cast<uint16_t>(source[1]) << 8);
}

uint32_t readLe32(const uint8_t *source) {
  return static_cast<uint32_t>(source[0]) |
         (static_cast<uint32_t>(source[1]) << 8) |
         (static_cast<uint32_t>(source[2]) << 16) |
         (static_cast<uint32_t>(source[3]) << 24);
}

String decodeManufacturer(const uint8_t *edid) {
  uint16_t code = (static_cast<uint16_t>(edid[8]) << 8) | edid[9];
  String manufacturer;
  manufacturer += static_cast<char>(((code >> 10) & 0x1f) + 'A' - 1);
  manufacturer += static_cast<char>(((code >> 5) & 0x1f) + 'A' - 1);
  manufacturer += static_cast<char>((code & 0x1f) + 'A' - 1);
  return manufacturer;
}

bool hasValidEdidHeader(const uint8_t *edid) {
  const uint8_t header[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
  return memcmp(edid, header, sizeof(header)) == 0;
}

bool hasValidEdidChecksum(const uint8_t *edid) {
  uint8_t sum = 0;
  for (size_t i = 0; i < EDID_SIZE; i++) {
    sum += edid[i];
  }
  return sum == 0;
}

String descriptorText(const uint8_t *descriptor) {
  String text;
  for (uint8_t i = 5; i < 18; i++) {
    if (descriptor[i] == 0x0a || descriptor[i] == 0x00) {
      break;
    }
    text += static_cast<char>(descriptor[i]);
  }
  text.trim();
  return text;
}

uint16_t timingRefreshHz(const EdidTiming &timing) {
  uint16_t hBlank = timing.hFrontPorch + timing.hSyncWidth + timing.hBackPorch;
  uint16_t vBlank = timing.vFrontPorch + timing.vSyncWidth + timing.vBackPorch;
  uint32_t totalPixels = static_cast<uint32_t>(timing.hActive + hBlank) * static_cast<uint32_t>(timing.vActive + vBlank);
  if (!totalPixels) {
    return 60;
  }
  uint32_t refresh = (timing.pixelClockKHz * 1000UL + (totalPixels / 2)) / totalPixels;
  return static_cast<uint16_t>(refresh ? refresh : 1);
}

void clampTiming(EdidTiming &timing) {
  timing.hActive = clampU16(timing.hActive, 1, 4095);
  timing.vActive = clampU16(timing.vActive, 1, 4095);
  timing.hFrontPorch = clampU16(timing.hFrontPorch, 0, 1023);
  timing.hSyncWidth = clampU16(timing.hSyncWidth, 1, 1023);
  timing.hBackPorch = clampU16(timing.hBackPorch, 0, 1023);
  timing.vFrontPorch = clampU16(timing.vFrontPorch, 0, 63);
  timing.vSyncWidth = clampU16(timing.vSyncWidth, 1, 63);
  timing.vBackPorch = clampU16(timing.vBackPorch, 0, 63);
}

bool parseDetailedTiming(const uint8_t *dtd, EdidTiming &timing) {
  uint16_t pixelClock10KHz = readLe16(&dtd[0]);
  if (pixelClock10KHz == 0) {
    return false;
  }

  timing.pixelClockKHz = static_cast<uint32_t>(pixelClock10KHz) * 10;
  timing.hActive = dtd[2] | ((dtd[4] & 0xf0) << 4);
  uint16_t hBlank = dtd[3] | ((dtd[4] & 0x0f) << 8);
  timing.vActive = dtd[5] | ((dtd[7] & 0xf0) << 4);
  uint16_t vBlank = dtd[6] | ((dtd[7] & 0x0f) << 8);
  timing.hFrontPorch = dtd[8] | ((dtd[11] & 0xc0) << 2);
  timing.hSyncWidth = dtd[9] | ((dtd[11] & 0x30) << 4);
  timing.vFrontPorch = ((dtd[10] >> 4) & 0x0f) | ((dtd[11] & 0x0c) << 2);
  timing.vSyncWidth = (dtd[10] & 0x0f) | ((dtd[11] & 0x03) << 4);
  timing.hBackPorch = hBlank > (timing.hFrontPorch + timing.hSyncWidth) ? hBlank - timing.hFrontPorch - timing.hSyncWidth : 0;
  timing.vBackPorch = vBlank > (timing.vFrontPorch + timing.vSyncWidth) ? vBlank - timing.vFrontPorch - timing.vSyncWidth : 0;
  timing.hSyncPositive = (dtd[17] & 0x02) != 0;
  timing.vSyncPositive = (dtd[17] & 0x04) != 0;
  clampTiming(timing);
  return true;
}

bool parseEdid(const uint8_t *edid, EdidConfig &cfg, String &message) {
  if (!hasValidEdidHeader(edid)) {
    message = "EEPROM data does not have a valid EDID header.";
    return false;
  }
  if (!hasValidEdidChecksum(edid)) {
    message = "EEPROM data has an invalid EDID checksum.";
    return false;
  }

  cfg.manufacturer = decodeManufacturer(edid);
  cfg.productCode = readLe16(&edid[10]);
  cfg.serialNumber = readLe32(&edid[12]);
  cfg.week = edid[16];
  cfg.year = static_cast<uint16_t>(edid[17]) + 1990;
  cfg.widthCm = edid[21] ? edid[21] : 1;
  cfg.heightCm = edid[22] ? edid[22] : 1;
  cfg.timingCount = 0;
  cfg.monitorName = "Loaded EDID";
  cfg.minRefreshHz = 50;
  cfg.maxRefreshHz = 75;
  cfg.minHorizontalKHz = 30;
  cfg.maxHorizontalKHz = 85;

  for (uint8_t offset = 54; offset <= 108; offset += 18) {
    const uint8_t *descriptor = &edid[offset];
    if (readLe16(descriptor) != 0) {
      if (cfg.timingCount < MAX_TIMINGS && parseDetailedTiming(descriptor, cfg.timings[cfg.timingCount])) {
        cfg.timingCount++;
      }
      continue;
    }
    if (descriptor[3] == 0xfc) {
      cfg.monitorName = descriptorText(descriptor);
    } else if (descriptor[3] == 0xfd) {
      cfg.minRefreshHz = descriptor[5] ? descriptor[5] : 1;
      cfg.maxRefreshHz = descriptor[6] ? descriptor[6] : cfg.minRefreshHz;
      cfg.minHorizontalKHz = descriptor[7] ? descriptor[7] : 1;
      cfg.maxHorizontalKHz = descriptor[8] ? descriptor[8] : cfg.minHorizontalKHz;
    }
  }

  if (cfg.timingCount == 0) {
    message = "EEPROM EDID is valid, but it does not contain any detailed timing descriptors.";
    return false;
  }

  cfg.monitorName = sanitizeDescriptorText(cfg.monitorName);
  message = "Loaded a valid EDID from EEPROM.";
  return true;
}

String timingToJson(const EdidTiming &timing) {
  String json = F("{\"hActive\":");
  json += timing.hActive;
  json += F(",\"vActive\":");
  json += timing.vActive;
  json += F(",\"refresh\":");
  json += timingRefreshHz(timing);
  json += F(",\"pixelClock\":");
  json += timing.pixelClockKHz;
  json += F(",\"hFront\":");
  json += timing.hFrontPorch;
  json += F(",\"hSync\":");
  json += timing.hSyncWidth;
  json += F(",\"hBack\":");
  json += timing.hBackPorch;
  json += F(",\"vFront\":");
  json += timing.vFrontPorch;
  json += F(",\"vSync\":");
  json += timing.vSyncWidth;
  json += F(",\"vBack\":");
  json += timing.vBackPorch;
  json += F(",\"hPol\":");
  json += timing.hSyncPositive ? 1 : 0;
  json += F(",\"vPol\":");
  json += timing.vSyncPositive ? 1 : 0;
  json += F("}");
  return json;
}

String configToJson(const EdidConfig &cfg) {
  String json = F("{");
  json += F("\"manufacturer\":\"");
  json += jsonEscape(cfg.manufacturer);
  json += F("\",\"product\":");
  json += cfg.productCode;
  json += F(",\"serial\":");
  json += cfg.serialNumber;
  json += F(",\"name\":\"");
  json += jsonEscape(cfg.monitorName);
  json += F("\",\"week\":");
  json += static_cast<unsigned int>(cfg.week);
  json += F(",\"year\":");
  json += cfg.year;
  json += F(",\"widthCm\":");
  json += static_cast<unsigned int>(cfg.widthCm);
  json += F(",\"heightCm\":");
  json += static_cast<unsigned int>(cfg.heightCm);
  json += F(",\"timingCount\":");
  json += static_cast<unsigned int>(cfg.timingCount);
  json += F(",\"timings\":[");
  for (uint8_t i = 0; i < cfg.timingCount; i++) {
    if (i > 0) {
      json += F(",");
    }
    json += timingToJson(cfg.timings[i]);
  }
  json += F("]");
  json += F(",\"minRefresh\":");
  json += static_cast<unsigned int>(cfg.minRefreshHz);
  json += F(",\"maxRefresh\":");
  json += static_cast<unsigned int>(cfg.maxRefreshHz);
  json += F(",\"minH\":");
  json += static_cast<unsigned int>(cfg.minHorizontalKHz);
  json += F(",\"maxH\":");
  json += static_cast<unsigned int>(cfg.maxHorizontalKHz);
  json += F("}");
  return json;
}

bool detectEeprom() {
  Wire.beginTransmission(EEPROM_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

bool readEeprom(uint16_t address, uint8_t *data, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    size_t chunk = length - offset;
    if (chunk > 28) {
      chunk = 28;
    }

    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write(static_cast<uint8_t>(address + offset));
    if (Wire.endTransmission(false) != 0) {
      return false;
    }

    uint8_t received = Wire.requestFrom(EEPROM_I2C_ADDR, static_cast<uint8_t>(chunk));
    if (received != chunk) {
      return false;
    }
    for (size_t i = 0; i < chunk; i++) {
      data[offset + i] = Wire.read();
    }
    offset += chunk;
  }
  return true;
}

bool writeEeprom(uint16_t address, const uint8_t *data, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    uint8_t pageRemaining = EEPROM_PAGE_SIZE - ((address + offset) % EEPROM_PAGE_SIZE);
    size_t chunk = length - offset;
    if (chunk > pageRemaining) {
      chunk = pageRemaining;
    }

    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write(static_cast<uint8_t>(address + offset));
    for (size_t i = 0; i < chunk; i++) {
      Wire.write(data[offset + i]);
    }
    if (Wire.endTransmission() != 0) {
      return false;
    }

    delay(6);
    unsigned long deadline = millis() + 50;
    while (millis() < deadline) {
      if (detectEeprom()) {
        break;
      }
      delay(1);
    }
    offset += chunk;
  }
  return true;
}

bool writeEepromPage(uint16_t address, const uint8_t *data, size_t length) {
  Wire.beginTransmission(EEPROM_I2C_ADDR);
  Wire.write(static_cast<uint8_t>(address));
  for (size_t i = 0; i < length; i++) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission() == 0;
}

bool verifyEeprom(const uint8_t *expected, size_t length, size_t *mismatchIndex) {
  uint8_t actual[EDID_SIZE];
  if (!readEeprom(0, actual, length)) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    if (actual[i] != expected[i]) {
      if (mismatchIndex != nullptr) {
        *mismatchIndex = i;
      }
      return false;
    }
  }
  return true;
}

void finishFlashJob(bool success, const String &message) {
  flashJob.active = false;
  flashJob.done = true;
  flashJob.success = success;
  flashJob.progress = success ? 100 : flashJob.progress;
  flashJob.message = message;
}

void processFlashJob() {
  if (!flashJob.active) {
    return;
  }

  if (flashJob.state == 1) {
    size_t chunk = EDID_SIZE - flashJob.offset;
    if (chunk > EEPROM_PAGE_SIZE) {
      chunk = EEPROM_PAGE_SIZE;
    }
    if (!writeEepromPage(flashJob.offset, &flashJob.edid[flashJob.offset], chunk)) {
      finishFlashJob(false, "Flash failed while writing a page. Check wiring, pull-ups, and WP.");
      return;
    }

    flashJob.offset += chunk;
    flashJob.progress = 5 + static_cast<uint8_t>((flashJob.offset * 80) / EDID_SIZE);
    flashJob.message = "Writing EEPROM page " + String(flashJob.offset / EEPROM_PAGE_SIZE) + " of " + String(EDID_SIZE / EEPROM_PAGE_SIZE) + "...";
    flashJob.waitUntilMs = millis() + 6;
    flashJob.state = 2;
    return;
  }

  if (flashJob.state == 2) {
    if (static_cast<long>(millis() - flashJob.waitUntilMs) < 0) {
      return;
    }
    if (!detectEeprom()) {
      if (static_cast<long>(millis() - (flashJob.waitUntilMs + 50)) > 0) {
        finishFlashJob(false, "EEPROM did not acknowledge after a page write.");
      }
      return;
    }
    flashJob.state = flashJob.offset >= EDID_SIZE ? 3 : 1;
    return;
  }

  if (flashJob.state == 3) {
    flashJob.progress = 90;
    flashJob.message = "Verifying EEPROM contents...";
    size_t mismatchIndex = 0;
    if (!verifyEeprom(flashJob.edid, EDID_SIZE, &mismatchIndex)) {
      char indexText[5];
      snprintf(indexText, sizeof(indexText), "%02X", static_cast<unsigned int>(mismatchIndex));
      finishFlashJob(false, String("Verify failed near byte 0x") + indexText + ".");
      return;
    }
    finishFlashJob(true, "EEPROM flashed and verified successfully.");
  }
}

void sendResultPage(uint16_t statusCode, const String &title, const String &message) {
  String html = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>");
  html += title;
  html += F("</title><style>body{font-family:system-ui;background:#09111f;color:#e5eefc;margin:0;display:grid;place-items:center;min-height:100vh}.card{max-width:680px;margin:24px;padding:28px;border-radius:24px;background:#121c2d;border:1px solid rgba(255,255,255,.12)}a{color:#6ee7b7}</style></head><body><div class='card'><h1>");
  html += title;
  html += F("</h1><p>");
  html += message;
  html += F("</p><p><a href='/'>Back to editor</a></p></div></body></html>");
  server.send(statusCode, "text/html", html);
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  bool present = detectEeprom();
  String json = F("{\"address\":\"0x50\",\"detected\":");
  json += present ? F("true") : F("false");
  json += F("}");
  server.send(200, "application/json", json);
}

void handleDownload() {
  uint8_t edid[EDID_SIZE];
  generateEdid(readConfigFromRequest(), edid);
  server.sendHeader("Content-Disposition", "attachment; filename=\"edid.hex\"");
  server.send(200, "text/plain", edidToHex(edid));
}

void handleLoad() {
  if (!detectEeprom()) {
    sendJson(409, F("{\"ok\":false,\"message\":\"EEPROM not detected at 0x50.\"}"));
    return;
  }

  uint8_t edid[EDID_SIZE];
  if (!readEeprom(0, edid, EDID_SIZE)) {
    sendJson(500, F("{\"ok\":false,\"message\":\"Failed to read 128 bytes from EEPROM.\"}"));
    return;
  }

  EdidConfig cfg;
  String message;
  if (!parseEdid(edid, cfg, message)) {
    String json = F("{\"ok\":false,\"message\":\"");
    json += jsonEscape(message);
    json += F("\"}");
    sendJson(422, json);
    return;
  }

  String json = F("{\"ok\":true,\"message\":\"");
  json += jsonEscape(message);
  json += F("\",\"config\":");
  json += configToJson(cfg);
  json += F(",\"hex\":\"");
  json += jsonEscape(edidToHex(edid));
  json += F("\"");
  json += F("}");
  sendJson(200, json);
}

void handleFlashStart() {
  if (flashJob.active) {
    sendJson(409, F("{\"ok\":false,\"message\":\"A flash operation is already running.\"}"));
    return;
  }
  if (!detectEeprom()) {
    sendJson(409, F("{\"ok\":false,\"message\":\"EEPROM not detected at 0x50, so flashing was not started.\"}"));
    return;
  }

  generateEdid(readConfigFromRequest(), flashJob.edid);
  flashJob.active = true;
  flashJob.done = false;
  flashJob.success = false;
  flashJob.progress = 0;
  flashJob.state = 1;
  flashJob.offset = 0;
  flashJob.waitUntilMs = 0;
  flashJob.message = "Starting EEPROM write...";
  sendJson(202, F("{\"ok\":true,\"message\":\"Flash started.\"}"));
}

void handleFlashProgress() {
  String json = F("{\"active\":");
  json += flashJob.active ? F("true") : F("false");
  json += F(",\"done\":");
  json += flashJob.done ? F("true") : F("false");
  json += F(",\"success\":");
  json += flashJob.success ? F("true") : F("false");
  json += F(",\"progress\":");
  json += flashJob.progress;
  json += F(",\"message\":\"");
  json += jsonEscape(flashJob.message.length() ? flashJob.message : "Idle.");
  json += F("\"}");
  sendJson(200, json);
}

void handleNotFound() {
  sendResultPage(404, "Not found", "That endpoint does not exist on this EDID writer.");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi SSID ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/load", HTTP_GET, handleLoad);
  server.on("/api/flash/start", HTTP_POST, handleFlashStart);
  server.on("/api/flash/progress", HTTP_GET, handleFlashProgress);
  server.on("/download", HTTP_POST, handleDownload);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println();
  Serial.println("EDID writer ready");
  Serial.print("Wi-Fi SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());
  Serial.print("EEPROM at 0x50: ");
  Serial.println(detectEeprom() ? "detected" : "not detected");
}

void loop() {
  server.handleClient();
  processFlashJob();
}