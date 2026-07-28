#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/html_ui.h"
#pragma once
#include <Arduino.h>

/** Multi-tab web UI: BQ76905 (from esp-bq-monitor), charger JSON, flash uploads */
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Cyberdeck Unified Service</title>
  <style>
    body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#111;color:#eee;margin:16px}
    h1{font-size:1.2rem}
    h2{font-size:1.05rem;margin-top:16px}
    nav{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0}
    nav a, nav button{
      padding:8px 12px;border-radius:8px;border:1px solid #444;background:#222;color:#eee;cursor:pointer;
      text-decoration:none;font-size:0.9rem
    }
    nav a.active, nav button.active{background:#2a4a2a;border-color:#5a8}
    section.tab{display:none}
    section.tab.active{display:block}
    table{border-collapse:collapse;width:100%;max-width:980px}
    td{padding:8px 10px;border-bottom:1px solid #333}
    td:first-child, td:nth-child(3){color:#aaa;width:24%}
    td:nth-child(2), td:nth-child(4){width:26%}
    .mono{font-family:ui-monospace,Consolas,Monaco,monospace;font-size:12px}
    pre.mono{background:#1a1a1a;padding:12px;border-radius:8px;overflow:auto;max-height:70vh;white-space:pre-wrap}
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
    .latched{position:absolute;right:8px;bottom:8px;width:18px;height:18px;border-radius:50%;
      display:flex;align-items:center;justify-content:center;background:#f0b429;color:#111;font-weight:800;font-size:12px;
      box-shadow:0 0 0 1px #7a5a11 inset}
    .row{margin:8px 0}
    .btnBar{display:flex;flex-wrap:wrap;gap:8px}
    button:disabled{opacity:.45;cursor:not-allowed}
    .notice{border:1px solid #3b5f7a;background:#13202a;color:#b9def7;border-radius:8px;padding:10px;margin:8px 0}
    .hero{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px;max-width:980px;margin:10px 0}
    .card{border:1px solid #333;border-radius:12px;background:#1b1b1b;padding:14px}
    .card.good{border-color:#1f6b2d;background:#122015}
    .card.warn{border-color:#8a6a17;background:#2b230f}
    .card.bad{border-color:#8b1e1e;background:#2a1313}
    .card h3{margin:0 0 8px 0;font-size:.9rem;color:#aaa;font-weight:600}
    .big{font-size:1.7rem;font-weight:800;line-height:1.1}
    .subtle{color:#aaa}
    .meter{height:14px;border-radius:99px;background:#262626;overflow:hidden;margin-top:10px;border:1px solid #333}
    .meter > div{height:100%;width:0;background:linear-gradient(90deg,#4ade80,#facc15,#fb923c);transition:width .25s ease}
    .kv{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin-top:8px}
    .kv span:nth-child(odd){color:#aaa}
  </style>
</head>
<body>
  <h1>Cyberdeck unified factory / diagnostic</h1>
  <nav>
    <button type="button" class="active" data-tab="tabBq">BQ76905</button>
    <button type="button" data-tab="tabChg">Charger / PD</button>
    <button type="button" data-tab="tabTd">ATtiny Debug</button>
    <button type="button" data-tab="tabFl">Flash</button>
  </nav>

  <section id="tabBq" class="tab active">
    <div id="bqRoleNotice" class="notice mono" style="display:none"></div>
    <div class="btnBar row">
      <button type="button" id="btnReconfig">Re-configure BQ</button>
      <button type="button" id="btnSleep">Toggle deep sleep</button>
      <button type="button" id="btnBalance">Start balancing</button>
      <button type="button" id="btnShip">Ship / shutdown</button>
    </div>
    <h2>BQ76905</h2>
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
      <tr><td>I2C OK</td><td id="i2cok">-</td><td></td><td></td></tr>
    </table>
    <h2>Alarm Status</h2>
    <div id="alarmTiles" class="tiles"></div>
    <h2>Safety A</h2>
    <div id="safetyATiles" class="tiles"></div>
    <h2>Safety B</h2>
    <div id="safetyBTiles" class="tiles"></div>
    <h2>Battery Status</h2>
    <div id="batteryTiles" class="tiles"></div>
  </section>

  <section id="tabChg" class="tab">
    <h2>TPS25751 + BQ25792</h2>
    <div id="chgNotice" class="notice mono" style="display:none"></div>
    <div class="hero">
      <div id="chgStateCard" class="card neutral">
        <h3>Charge State</h3>
        <div id="chgState" class="big">-</div>
        <div id="chgAdapter" class="subtle mono">-</div>
      </div>
      <div id="chgPowerCard" class="card neutral">
        <h3>Input Power</h3>
        <div id="chgPower" class="big">-</div>
        <div class="meter"><div id="chgPowerMeter"></div></div>
        <div id="chgPowerNote" class="subtle mono">20 W scale</div>
      </div>
      <div class="card">
        <h3>Electrical</h3>
        <div class="kv mono">
          <span>VBUS</span><strong id="chgVbus">-</strong>
          <span>IBUS</span><strong id="chgIbus">-</strong>
          <span>VBAT</span><strong id="chgVbat">-</strong>
          <span>IBAT</span><strong id="chgIbat">-</strong>
          <span>VSYS</span><strong id="chgVsys">-</strong>
        </div>
      </div>
      <div id="chgBridgeCard" class="card neutral">
        <h3>TPS Bridge</h3>
        <div id="chgBridge" class="big">-</div>
        <div id="chgBridgeAge" class="subtle mono">-</div>
      </div>
    </div>
    <div id="chgStatusTiles" class="tiles"></div>
    <p class="mono" style="color:#888">Raw JSON snapshot (1s page refresh, BQ bridge samples every ~5s)</p>
    <pre class="mono" id="chgOut">{}</pre>
  </section>

  <section id="tabTd" class="tab">
    <h2>ATtiny404 debug stream</h2>
    <p class="mono" style="color:#888">Decoded from the BMS firmware's TX-only debug pin (Tiny PB2 / BATT_LVL_2 net → ESP GPIO, 115200 8N1). Requires the Tiny built with DEBUG=1.</p>
    <div id="tdNotice" class="notice mono" style="display:none"></div>
    <div class="hero">
      <div id="tdLinkCard" class="card neutral">
        <h3>Serial Link</h3>
        <div id="tdLink" class="big">-</div>
        <div id="tdLinkAge" class="subtle mono">-</div>
      </div>
      <div id="tdBqCard" class="card neutral">
        <h3>BQ Reads</h3>
        <div id="tdBq" class="big">-</div>
        <div id="tdI2cErr" class="subtle mono">-</div>
      </div>
      <div class="card">
        <h3>Cells</h3>
        <div class="kv mono">
          <span>Cell 1</span><strong id="tdC1">-</strong>
          <span>Cell 2</span><strong id="tdC2">-</strong>
          <span>Delta</span><strong id="tdDelta">-</strong>
        </div>
      </div>
      <div id="tdStateCard" class="card neutral">
        <h3>Device</h3>
        <div id="tdPower" class="big">-</div>
        <div id="tdBal" class="subtle mono">-</div>
      </div>
    </div>
    <div class="btnBar row">
      <button type="button" id="tdClear">Clear log</button>
      <label class="mono"><input type="checkbox" id="tdScroll" checked> Auto-scroll</label>
    </div>
    <h2>Event log</h2>
    <pre class="mono" id="tdLog"></pre>
  </section>

  <section id="tabFl" class="tab">
    <h2>Upload &amp; flash</h2>
    <p class="mono" style="color:#888">Stage a file, then start. Jobs run in background; keep this page open for status.</p>
    <p class="mono" style="color:#c98">EEPROM: power the TPS25751 down, then flash — the ESP switches its second I2C port from <b>I2Ct</b> (monitor) to <b>I2Cc</b> (EEPROM) only for the job.</p>
    <div class="row">
      <label class="mono">EEPROM image → AT24 on I2Cc</label><br>
      <input type="file" id="fEep">
      <button type="button" id="uEep">Upload to ESP</button>
      <button type="button" id="sEep">Start EEPROM flash</button>
    </div>
    <div class="row">
      <label class="mono">ATtiny404 application .bin</label><br>
      <input type="file" id="fAtt">
      <button type="button" id="uAtt">Upload to ESP</button>
      <button type="button" id="sAtt">Start UPDI flash</button>
    </div>
    <h3>Status</h3>
    <pre class="mono" id="flOut">{}</pre>
  </section>

  <script>
    function hx(v,w){ return '0x' + v.toString(16).toUpperCase().padStart(w,'0'); }
    function fmtV(mv){ return Number.isFinite(mv) && mv > 0 ? (mv / 1000).toFixed(3) + ' V' : '-'; }
    function fmtA(ma){ return Number.isFinite(ma) ? (ma / 1000).toFixed(3) + ' A' : '-'; }
    function fmtW(mw){ return Number.isFinite(mw) && mw > 0 ? (mw / 1000).toFixed(2) + ' W' : '-'; }
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
    const bqChgLabels = ['Not charging','Trickle charge','Pre-charge','Fast charge (CC)','Taper charge (CV)','Reserved','Top-off timer','Charge done'];
    const bqVbusLabels = [
      'No input / BHOT / BCOLD','USB SDP 500 mA','USB CDP 1.5 A','USB DCP 3.25 A',
      'HVDCP 1.5 A','Unknown adapter 3 A','Non-standard adapter','OTG mode',
      'Not qualified adapter','Reserved','Reserved','Direct VBUS','Reserved','Reserved','Reserved','Reserved'
    ];
    function setCardState(el, state){
      el.classList.remove('ok','good','warn','bad','neutral');
      el.classList.add(state);
    }
    function renderCharger(d){
      if (d.charger_monitor_suspended) {
        chgNotice.style.display = 'block';
        chgNotice.textContent = 'Charger monitor suspended while the flash job owns the shared I2C peripheral.';
        chgState.textContent = 'Suspended';
        chgAdapter.textContent = d.reason || '-';
        setCardState(chgStateCard, 'warn');
        return;
      }

      chgNotice.style.display = d.bq_present ? 'none' : 'block';
      chgNotice.textContent = d.bq_present
        ? ''
        : 'BQ25792 is not directly on the ESP monitor bus; readings below come through the TPS25751 I2C bridge every ~5 seconds.';

      const chgCode = d.bq_chg_stat ?? 0;
      const adapterCode = d.bq_vbus_stat ?? 0;
      const activeCharging = [1,2,3,4,6].includes(chgCode) || (d.ibat_mA ?? 0) > 20;
      const faults = (d.fault0 || 0) | (d.fault1 || 0);
      chgState.textContent = bqChgLabels[chgCode] || 'Unknown';
      chgAdapter.textContent = `${bqVbusLabels[adapterCode] || 'Unknown adapter'} · TPS addr ${d.tps_addr || '-'}`;
      setCardState(chgStateCard, faults ? 'bad' : (activeCharging ? 'good' : 'neutral'));

      const inputMw = d.input_power_mW || 0;
      chgPower.textContent = fmtW(inputMw);
      chgPowerMeter.style.width = Math.max(0, Math.min(100, inputMw / 20000 * 100)).toFixed(0) + '%';
      setCardState(chgPowerCard, inputMw > 500 ? 'good' : 'neutral');
      chgPowerNote.textContent = d.charge_power_mW ? `Battery charge estimate ${fmtW(d.charge_power_mW)}` : '20 W scale';

      chgVbus.textContent = fmtV(d.vbus_mV);
      chgIbus.textContent = fmtA(d.ibus_mA);
      chgVbat.textContent = fmtV(d.vbat_mV);
      chgIbat.textContent = fmtA(d.ibat_mA);
      chgVsys.textContent = fmtV(d.vsys_mV);

      chgBridge.textContent = d.bq_via_tps_ok ? 'OK' : 'Waiting';
      chgBridgeAge.textContent = d.bq_via_tps_ok
        ? `last sample ${d.last_i2cr_age_ms} ms ago`
        : `last attempt ${d.last_i2cr_attempt_age_ms} ms ago`;
      setCardState(chgBridgeCard, d.bq_via_tps_ok ? 'good' : 'warn');

      const statusTiles = [
        ['VBUS present', d.bq_vbus_present, 'Adapter voltage detected'],
        ['Power good', d.bq_power_good, 'BQ25792 PG_STAT'],
        ['Battery present', d.bq_vbat_present, 'VBAT above UVLO'],
        ['Input current limit', d.bq_iindpm, 'IINDPM regulation active'],
        ['Input voltage limit', d.bq_vindpm, 'VINDPM regulation active'],
        ['Thermal regulation', d.bq_treg, 'Charge current may be reduced'],
        ['BQ ADC', d.bq_adc_ok, 'Voltage/current ADC readings available'],
        ['Faults', faults !== 0, `REG20=${hx(d.fault0 || 0,2)} REG21=${hx(d.fault1 || 0,2)}`]
      ];
      chgStatusTiles.innerHTML = statusTiles.map(([label, on, detail]) => {
        const isFaultTile = label === 'Faults';
        const tileClass = isFaultTile ? (on ? 'bad' : 'ok') : (on ? 'ok' : 'neutral');
        const stateClass = isFaultTile ? (on ? 'bad' : 'ok') : (on ? 'ok' : 'neutral');
        const stateText = isFaultTile ? (on ? 'FAULT' : 'OK') : (on ? 'YES' : 'NO');
        return `<div class="tile ${tileClass}"><h3>${label}</h3><div class="state ${stateClass}">${stateText}</div><div class="mono">${detail}</div></div>`;
      }).join('');
    }
    document.querySelectorAll('nav button[data-tab]').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        document.querySelectorAll('section.tab').forEach(s => s.classList.remove('active'));
        document.getElementById(btn.dataset.tab).classList.add('active');
      });
    });
    async function pollBq(){
      try{
        const r = await fetch('/api/bq', {cache:'no-store'});
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
        mode.textContent = d.actions_enabled ? (d.deep_sleep ? 'ESP MASTER / DEEPSLEEP' : 'ESP MASTER') : 'ATTINY MASTER / ESP DISCOVERY TARGET';
        gate.textContent = d.balance_gate ? 'ON' : 'OFF';
        btnBalance.textContent = d.balance_gate ? 'Stop balancing' : 'Start balancing';
        bqbal.textContent = d.bq_balancing ? 'ON' : 'OFF';
        alarm.textContent = d.alarm ? 'ON' : 'OFF';
        safea.textContent = d.safety_a ? 'ON' : 'OFF';
        safeb.textContent = d.safety_b ? 'ON' : 'OFF';
        i2cok.textContent = d.i2c_ok ? 'yes' : 'no';
        const readonlyBq = d.actions_enabled === false;
        bqRoleNotice.style.display = readonlyBq ? 'block' : 'none';
        bqRoleNotice.textContent = readonlyBq ? 'ESP is waiting as an I2C discovery target. BQ control and balancing are owned by the ATTiny404 until it hands master mode to the ESP.' : '';
        [btnReconfig, btnSleep, btnBalance, btnShip].forEach(btn => { btn.disabled = readonlyBq; });
        makeTiles(alarmTiles, alarmDefs, d.alarm_raw_status, d.alarm_seen);
        makeTiles(safetyATiles, safetyADefs, d.safety_alert_a, d.safety_a_seen);
        makeTiles(safetyBTiles, safetyBDefs, d.safety_alert_b, d.safety_b_seen);
        makeTiles(batteryTiles, batteryDefs, d.battery_status, d.battery_seen);
      }catch(e){}
    }
    async function pollChg(){
      if (!document.getElementById('tabChg').classList.contains('active')) return;
      try{
        const r = await fetch('/api/charger', {cache:'no-store'});
        const t = await r.text();
        try {
          const d = JSON.parse(t);
          renderCharger(d);
          chgOut.textContent = JSON.stringify(d, null, 2);
        }
        catch { chgOut.textContent = t; }
      }catch(e){}
    }
    let tdSince = 0;
    document.getElementById('tdClear').addEventListener('click', () => { tdLog.textContent = ''; });
    async function pollTd(){
      if (!document.getElementById('tabTd').classList.contains('active')) return;
      try{
        const r = await fetch('/api/tinydbg?since=' + tdSince, {cache:'no-store'});
        const d = await r.json();

        const linkUp = d.line_seen && d.last_line_age_ms < 5000;
        tdLink.textContent = d.line_seen ? (linkUp ? 'RECEIVING' : 'STALE') : 'NO DATA';
        tdLinkAge.textContent = d.line_seen ? `last record ${d.last_line_age_ms} ms ago` : 'nothing received yet — check wiring and DEBUG=1 build';
        setCardState(tdLinkCard, linkUp ? 'good' : (d.line_seen ? 'warn' : 'bad'));

        const haveStatus = d.status_seen && d.status_age_ms < 5000;
        tdBq.textContent = haveStatus ? (d.bq_valid ? 'OK' : 'FAILING') : '-';
        tdI2cErr.textContent = haveStatus ? `last I2C error code ${d.i2c_err}` : 'no status record yet';
        setCardState(tdBqCard, haveStatus ? (d.bq_valid ? 'good' : 'bad') : 'neutral');

        const cellsValid = haveStatus && d.bq_valid;
        tdC1.textContent = cellsValid ? fmtV(d.c1_mV) : '-';
        tdC2.textContent = cellsValid ? fmtV(d.c2_mV) : '-';
        tdDelta.textContent = cellsValid ? Math.abs(d.c1_mV - d.c2_mV) + ' mV' : '-';

        tdPower.textContent = haveStatus ? (d.power_on ? 'POWER ON' : 'POWER OFF') : '-';
        tdBal.textContent = haveStatus ? (d.balancing ? 'balancing active' : 'not balancing') : '-';
        setCardState(tdStateCard, haveStatus ? (d.power_on ? 'good' : 'neutral') : 'neutral');

        tdNotice.style.display = 'none';
        for (const ev of (d.events || [])) {
          const t = new Date(Date.now() - ev.age_ms).toLocaleTimeString();
          tdLog.textContent += `[${t}] ${ev.text}\n`;
          tdSince = Math.max(tdSince, ev.seq);
        }
        if (d.events && d.events.length && tdScroll.checked) {
          tdLog.scrollTop = tdLog.scrollHeight;
        }
      }catch(e){}
    }
    async function pollFl(){
      try{
        const r = await fetch('/api/flash/status', {cache:'no-store'});
        const t = await r.text();
        try { flOut.textContent = JSON.stringify(JSON.parse(t), null, 2); }
        catch { flOut.textContent = t; }
      }catch(e){}
    }
    function bindBtn(id, cmd){
      document.getElementById(id).addEventListener('click', async () => {
        try{
          await fetch('/api/bq/action?cmd=' + encodeURIComponent(cmd));
          pollBq();
        }catch(e){ alert(e); }
      });
    }
    bindBtn('btnReconfig','reconfig');
    bindBtn('btnSleep','toggle_sleep');
    bindBtn('btnBalance','toggle_balance');
    bindBtn('btnShip','ship');
    document.getElementById('uEep').addEventListener('click', async () => {
      const f = document.getElementById('fEep').files[0];
      if (!f) { alert('Pick file'); return; }
      const fd = new FormData();
      fd.append('eeprom', f);
      const r = await fetch('/upload/eeprom', { method:'POST', body: fd });
      alert('Upload HTTP ' + r.status);
      pollFl();
    });
    document.getElementById('uAtt').addEventListener('click', async () => {
      const f = document.getElementById('fAtt').files[0];
      if (!f) { alert('Pick file'); return; }
      const fd = new FormData();
      fd.append('attiny', f);
      const r = await fetch('/upload/attiny', { method:'POST', body: fd });
      alert('Upload HTTP ' + r.status);
      pollFl();
    });
    document.getElementById('sEep').addEventListener('click', async () => {
      await fetch('/flash/eeprom/start', { method:'POST' });
      pollFl();
    });
    document.getElementById('sAtt').addEventListener('click', async () => {
      await fetch('/flash/attiny/start', { method:'POST' });
      pollFl();
    });
    setInterval(pollBq, 500);
    setInterval(pollChg, 1000);
    setInterval(pollTd, 500);
    setInterval(pollFl, 1500);
    pollBq(); pollChg(); pollTd(); pollFl();
  </script>
</body>
</html>
)HTML";
