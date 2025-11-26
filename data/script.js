// Small helper UI notifiers (non-blocking)
function setMsg(id, text, ok) {
  if (ok === void 0) ok = true;
  var el = document.getElementById(id);
  if (!el) return;
  el.textContent = text || '';
  el.classList.remove('ok','err');
  el.classList.add(ok ? 'ok' : 'err');
  setTimeout(function(){ el.textContent=''; el.classList.remove('ok','err'); }, 2500);
}
function pad2(n){ return (n<10?'0':'')+n; }

function minutesToTimeString(mins){
  var value = Number(mins);
  if (!isFinite(value)) value = 0;
  value = ((value % 1440) + 1440) % 1440;
  var h = Math.floor(value / 60);
  var m = value % 60;
  return pad2(h) + ':' + pad2(m);
}

var alarmClockIs24 = true;

function clamp(value, min, max){
  var v = Number(value);
  if (!isFinite(v)) v = min;
  if (v < min) v = min;
  if (v > max) v = max;
  return v;
}

function isClock24Selected(){
  var el = document.getElementById('uClock');
  var val = parseInt(el && el.value, 10);
  if (!isFinite(val)) val = 1;
  return val === 1;
}

function setAlarmHourInputs(idx, hour24, targetClock24h){
  var hourEl = document.getElementById('alarm' + idx + 'Hour');
  var periodEl = document.getElementById('alarm' + idx + 'Period');

  var showPeriod = !targetClock24h;
  if (periodEl) periodEl.style.display = showPeriod ? '' : 'none';

  if (!hourEl) return;

  if (targetClock24h){
    var h24 = clamp(hour24, 0, 23);
    hourEl.min = 0;
    hourEl.max = 23;
    hourEl.placeholder = 'Hour (0-23)';
    hourEl.value = h24;
    return;
  }

  var h = ((hour24 % 24) + 24) % 24;
  var period = (h >= 12) ? 'pm' : 'am';
  var h12 = h % 12;
  if (h12 === 0) h12 = 12;
  hourEl.min = 1;
  hourEl.max = 12;
  hourEl.placeholder = 'Hour (1-12)';
  hourEl.value = h12;
  if (periodEl) periodEl.value = period;
}

function readAlarmHourFromInputs(idx, sourceClock24h){
  var hourEl = document.getElementById('alarm' + idx + 'Hour');
  var raw = parseInt(hourEl && hourEl.value, 10);
  if (!isFinite(raw)) raw = sourceClock24h ? 0 : 12;

  if (sourceClock24h){
    var h24 = clamp(raw, 0, 23);
    if (hourEl) hourEl.value = h24;
    return h24;
  }

  var periodEl = document.getElementById('alarm' + idx + 'Period');
  var period = (periodEl && periodEl.value === 'pm') ? 'pm' : 'am';
  var h12 = clamp(raw, 1, 12);
  if (hourEl) hourEl.value = h12;
  if (period === 'pm' && h12 !== 12) return h12 + 12;
  if (period === 'am' && h12 === 12) return 0;
  return h12 % 12;
}

function readAlarmMinute(idx){
  var minuteEl = document.getElementById('alarm' + idx + 'Minute');
  var raw = parseInt(minuteEl && minuteEl.value, 10);
  var minute = clamp(raw, 0, 59);
  if (minuteEl) minuteEl.value = minute;
  return minute;
}

function applyAlarmHourFormat(targetClock24h){
  [1,2,3].forEach(function(idx){
    var hour24 = readAlarmHourFromInputs(idx, alarmClockIs24);
    setAlarmHourInputs(idx, hour24, targetClock24h);
  });
  alarmClockIs24 = targetClock24h;
}

function timeStringToMinutes(str, fallback){
  if (fallback === undefined) fallback = 0;
  if (typeof str !== 'string') str = '';
  var parts = str.split(':');
  var h = parseInt(parts[0], 10);
  var m = parseInt(parts[1], 10);
  if (!isFinite(h)) h = Math.floor(fallback / 60);
  if (!isFinite(m)) m = fallback % 60;
  h = Math.min(23, Math.max(0, h));
  m = Math.min(59, Math.max(0, m));
  return h * 60 + m;
}

function sendRemoteCommand(action){
  if (!action) return;
  if (action === 'screen') {
    toggleScreenRemote();
    return;
  }
  fetch('/ir?btn=' + encodeURIComponent(action))
    .then(function(r){
      if (!r.ok) throw new Error('Failed');
      return r.json().catch(function(){ return null; });
    })
    .then(function(){
      setMsg('remoteMsg','Command sent.', true);
    })
    .catch(function(){
      setMsg('remoteMsg','Command failed.', false);
    });
}

function toggleScreenRemote(){
  fetch('/screen?toggle=1')
    .then(function(r){
      if (!r.ok) throw new Error('Failed');
      return r.json().catch(function(){ return null; });
    })
    .then(function(payload){
      if (!payload || typeof payload !== 'object') throw new Error('Bad response');
      var msg = (payload.state === 'off') ? 'Screen turned off.' : 'Screen turned on.';
      setMsg('remoteMsg', msg, true);
      if (typeof loadIndexStatus === 'function') {
        loadIndexStatus();
      }
      if (typeof loadFullStatus === 'function') {
        loadFullStatus();
      }
    })
    .catch(function(){
      setMsg('remoteMsg','Screen toggle failed.', false);
    });
}

function setupRemoteControls(){
  var buttons = document.querySelectorAll('[data-remote]');
  if (!buttons || buttons.length === 0) return;
  Array.prototype.forEach.call(buttons, function(btn){
    btn.addEventListener('click', function(ev){
      ev.preventDefault();
      var action = btn.getAttribute('data-remote');
      sendRemoteCommand(action);
    });
  });
}

function toDisplayText(value){
  if (value === undefined || value === null || value === '') return '--';
  return String(value);
}

function setText(id, value){
  var el = document.getElementById(id);
  if (!el) return;
  var next = toDisplayText(value);
  if (el.textContent === next) return;
  el.textContent = next;
}

function formatTempValue(value){
  if (value === undefined || value === null || value === '' || value === '--') return '--';
  return String(value);
}

function formatHumidityValue(value){
  if (value === undefined || value === null || value === '' || value === '--') return '--';
  value = String(value);
  if (value.indexOf('%') === -1) value += '%';
  return value;
}

function formatBytes(value){
  var num = Number(value);
  if (!isFinite(num) || num < 0) return null;
  var units = ['B','KB','MB','GB'];
  var idx = 0;
  while (num >= 1024 && idx < units.length - 1){
    num /= 1024;
    idx++;
  }
  var precision;
  if (idx === 0 || num >= 100) {
    precision = 0;
  } else if (num >= 10) {
    precision = 1;
  } else {
    precision = 2;
  }
  return num.toFixed(precision) + ' ' + units[idx];
}

function formatCapacity(total, free){
  var totalStr = formatBytes(total);
  var freeStr = formatBytes(free);
  if (!totalStr || !freeStr) return null;
  return freeStr + ' free / ' + totalStr;
}

function formatUsage(used, percent){
  var usedStr = formatBytes(used);
  if (!usedStr) return null;
  var pct = Number(percent);
  if (isFinite(pct)) {
    return usedStr + ' (' + pct + '% used)';
  }
  return usedStr;
}

function describeTempC(value){
  var temp = Number(value);
  if (!isFinite(temp)) return null;
  if (temp < 18) return 'Cool';
  if (temp < 25) return 'Comfortable';
  if (temp < 30) return 'Warm';
  return 'Hot';
}

function describeHumidityPct(value){
  var hum = Number(value);
  if (!isFinite(hum)) return null;
  if (hum < 30) return 'Dry';
  if (hum <= 60) return 'Comfortable';
  if (hum <= 70) return 'Humid';
  return 'Very humid';
}

function describeCo2(ppm){
  var value = Number(ppm);
  if (!isFinite(value) || value <= 0) return null;
  if (value <= 600) return 'Excellent';
  if (value <= 1000) return 'Good';
  if (value <= 1400) return 'Fair';
  if (value <= 2000) return 'Poor';
  return 'Very poor';
}

function describePressure(value){
  var press = Number(value);
  if (!isFinite(press)) return null;
  if (press < 1000) return 'Low';
  if (press <= 1020) return 'Normal';
  if (press <= 1030) return 'High';
  return 'Very high';
}

var wifiScanBusy = false;

function wifiScanSetStatus(text, ok){
  var el = document.getElementById('wifiScanStatus');
  if (!el) return;
  el.textContent = text || '';
  el.classList.remove('ok','err');
  if (!text) return;
  el.classList.add(ok ? 'ok' : 'err');
}

function formatWifiRssi(value){
  var rssi = Number(value);
  if (!isFinite(rssi)) return '--';
  var quality;
  if (rssi >= -55) quality = 'Excellent';
  else if (rssi >= -65) quality = 'Good';
  else if (rssi >= -75) quality = 'Fair';
  else quality = 'Weak';
  return rssi + ' dBm (' + quality + ')';
}

function applyWifiSelection(ssid){
  if (!ssid) return;
  var input = document.getElementById('wifiSSID');
  if (input) {
    input.value = ssid;
    input.focus();
  }
  wifiScanSetStatus('SSID set to "' + ssid + '". Enter the password and save.', true);
}

function renderWifiScanResults(payload){
  var list = document.getElementById('wifiScanList');
  if (!list) return;
  list.innerHTML = '';
  var networks = (payload && Array.isArray(payload.networks)) ? payload.networks.slice() : [];
  var seen = {};
  var filtered = [];
  networks.forEach(function(net, idx){
    if (!net) return;
    var ssid = (net.ssid || '').trim();
    if (!ssid) return;
    if (seen[ssid]) return;
    seen[ssid] = true;
    filtered.push(net);
  });
  filtered.sort(function(a, b){
    var ar = Number(a && a.rssi);
    var br = Number(b && b.rssi);
    if (!isFinite(ar)) ar = -999;
    if (!isFinite(br)) br = -999;
    return br - ar;
  });

  if (filtered.length === 0) {
    var empty = document.createElement('li');
    empty.className = 'wifi-scan-empty';
    empty.textContent = 'No WiFi networks found.';
    list.appendChild(empty);
    return;
  }

  filtered.forEach(function(net){
    var li = document.createElement('li');
    li.className = 'wifi-scan-item';
    var info = document.createElement('div');
    info.className = 'wifi-scan-info';
    var title = document.createElement('strong');
    title.textContent = net.ssid || '(hidden)';
    info.appendChild(title);
    var meta = document.createElement('div');
    meta.className = 'meta';
    if (net.security) {
      var secSpan = document.createElement('span');
      secSpan.textContent = net.security;
      meta.appendChild(secSpan);
    }
    var rssiSpan = document.createElement('span');
    rssiSpan.textContent = formatWifiRssi(net.rssi);
    meta.appendChild(rssiSpan);
    info.appendChild(meta);

    var actions = document.createElement('div');
    actions.className = 'wifi-scan-actions';
    if (payload && payload.connected && payload.connectedSSID && net.ssid &&
        payload.connectedSSID === net.ssid) {
      li.classList.add('current');
      var badge = document.createElement('span');
      badge.className = 'tag';
      badge.textContent = 'Connected';
      actions.appendChild(badge);
    }
    var btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'btn small';
    btn.textContent = 'Use';
    btn.addEventListener('click', function(){
      applyWifiSelection(net.ssid);
    });
    actions.appendChild(btn);

    li.appendChild(info);
    li.appendChild(actions);
    list.appendChild(li);
  });
}

function startWifiScan(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  if (wifiScanBusy) return;
  var btn = document.getElementById('wifiScanBtn');
  wifiScanBusy = true;
  if (btn) btn.disabled = true;
  wifiScanSetStatus('Scanning for WiFi...', true);
  fetch('/wifi/scan?ts=' + Date.now(), { cache: 'no-store' })
    .then(function(r){
      if (!r.ok) throw new Error('Scan failed');
      return r.json();
    })
    .then(function(data){
      renderWifiScanResults(data);
      var count = (data && typeof data.count === 'number') ? data.count : 0;
      wifiScanSetStatus(count > 0 ? ('Found ' + count + ' network' + (count === 1 ? '' : 's') + '.') : 'No WiFi networks detected.', count > 0);
    })
    .catch(function(err){
      console.warn('WiFi scan failed', err);
      wifiScanSetStatus('WiFi scan failed. Ensure the radio is enabled.', false);
    })
    .finally(function(){
      wifiScanBusy = false;
      if (btn) btn.disabled = false;
    });
}

function initWifiScanUI(){
  var btn = document.getElementById('wifiScanBtn');
  if (!btn) return;
  btn.addEventListener('click', startWifiScan);
}

var fullStatusPending = false;
var fullStatusTimer = null;
var FULL_STATUS_INTERVAL = 5000;

function formatUptimeLocal(sec){
  sec = Number(sec);
  if (!isFinite(sec) || sec < 0) return '--';
  var days = Math.floor(sec / 86400);
  sec = sec % 86400;
  var hours = Math.floor(sec / 3600);
  sec = sec % 3600;
  var minutes = Math.floor(sec / 60);
  var seconds = Math.floor(sec % 60);
  var base = pad2(hours) + ':' + pad2(minutes) + ':' + pad2(seconds);
  return days > 0 ? (days + ' d ' + base) : base;
}

function renderFullStatus(st){
  setText("fs-wifi-status", st.wifiStatus || st.wifiSSID || "--");
  setText("fs-ssid", st.wifiSSID || "--");
  setText("fs-ip", st.ip || "--");
  setText("fs-mac", st.mac || "--");
  setText("fs-rssi", (typeof st.rssi === "number") ? st.rssi + " dBm" : "--");
  setText("fs-datasource", st.dataSourceLabel || (st.dataSource !== undefined ? st.dataSource : "--"));
  var screenLabel = st.screenOff ? "Screen Off" : (st.screenLabel || (st.screen !== undefined ? st.screen : "--"));
  setText("fs-screen", screenLabel);
  setText("fs-uptime", st.uptime || formatUptimeLocal(st.uptimeSec));

  var heapTotal = st.heapTotal !== undefined ? Number(st.heapTotal) : undefined;
  var heapFree = st.heapFree !== undefined ? Number(st.heapFree) : (st.freeHeap !== undefined ? Number(st.freeHeap) : undefined);
  var heapUsed = st.heapUsed !== undefined ? Number(st.heapUsed) : (isFinite(heapTotal) && isFinite(heapFree) ? heapTotal - heapFree : undefined);
  var heapUsagePct = st.heapUsedPercent;
  setText("fs-ram", formatCapacity(heapTotal, heapFree));
  setText("fs-ram-usage", formatUsage(heapUsed, heapUsagePct));

  var storageTotal = st.fsTotal !== undefined ? Number(st.fsTotal) : undefined;
  var storageFree = st.fsFree !== undefined ? Number(st.fsFree) : (isFinite(storageTotal) && st.fsUsed !== undefined ? storageTotal - Number(st.fsUsed) : undefined);
  var storageUsed = st.fsUsed !== undefined ? Number(st.fsUsed) : (isFinite(storageTotal) && isFinite(storageFree) ? storageTotal - storageFree : undefined);
  var storageUsagePct = st.fsUsedPercent;
  setText("fs-storage", formatCapacity(storageTotal, storageFree));
  setText("fs-storage-usage", formatUsage(storageUsed, storageUsagePct));

  var flashTotal = st.flashTotal !== undefined ? Number(st.flashTotal) : undefined;
  var flashFree = st.flashFree !== undefined ? Number(st.flashFree) : (isFinite(flashTotal) && st.flashUsed !== undefined ? flashTotal - Number(st.flashUsed) : undefined);
  var flashUsed = st.flashUsed !== undefined ? Number(st.flashUsed) : (isFinite(flashTotal) && isFinite(flashFree) ? flashTotal - flashFree : undefined);
  var flashUsagePct = st.flashUsedPercent;
  setText("fs-flash", formatCapacity(flashTotal, flashFree));
  setText("fs-flash-usage", formatUsage(flashUsed, flashUsagePct));

  setText("fs-out-temp", formatTempValue(st.temp));
  var outHum = (window.DATA_SOURCE === 1) ? formatHumidityValue(st.humidity) : formatHumidityValue(st.humidity);
  setText("fs-out-hum", outHum);
  setText("fs-conditions", st.conditions || "--");
  setText("fs-indoor-temp", st.indoorTemp || "--");
  setText("fs-indoor-hum", formatHumidityValue(st.indoorHumidity));
  setText("fs-co2", (st.co2 !== undefined) ? st.co2 + " ppm" : "--");
  setText("fs-aht-temp", st.ahtTemp || "--");
  setText("fs-aht-hum", formatHumidityValue(st.ahtHumidity));
  setText("fs-pressure", st.pressure || "--");

  setText("fs-indoor-temp-desc", describeTempC(st.indoorTempRaw));
  setText("fs-indoor-hum-desc", describeHumidityPct(st.indoorHumidityRaw));
  setText("fs-co2-desc", describeCo2(st.co2));
  setText("fs-aht-temp-desc", describeTempC(st.ahtTempRaw));
  setText("fs-aht-hum-desc", describeHumidityPct(st.ahtHumidityRaw));
  setText("fs-pressure-desc", describePressure(st.pressureRaw));
}


function loadFullStatus(){
  if (fullStatusPending) return;
  fullStatusPending = true;
  fetch('/status.json')
    .then(function(r){
      if (!r.ok) throw new Error('Request failed');
      return r.json();
    })
    .then(function(st){
      renderFullStatus(st);
      setMsg('fullStatusMsg','',true);
    })
    .catch(function(){
      setMsg('fullStatusMsg','Unable to load status',false);
    })
    .finally(function(){
      fullStatusPending = false;
      if (fullStatusTimer) clearTimeout(fullStatusTimer);
      fullStatusTimer = setTimeout(loadFullStatus, FULL_STATUS_INTERVAL);
    });
}

var tzList = [];
var currentEpoch = 0;
var tzOffset = 0;
var tickTimer = null;
var startMs = Date.now();

const COUNTRY_OPTIONS = [
  { label: 'Vietnam (VN)', code: 'VN' },
  { label: 'United States (US)', code: 'US' },
  { label: 'Japan (JP)', code: 'JP' },
  { label: 'Germany (DE)', code: 'DE' },
  { label: 'India (IN)', code: 'IN' },
  { label: 'France (FR)', code: 'FR' },
  { label: 'Canada (CA)', code: 'CA' },
  { label: 'United Kingdom (GB)', code: 'GB' },
  { label: 'Australia (AU)', code: 'AU' },
  { label: 'Brazil (BR)', code: 'BR' },
  { label: 'Custom', code: '' }
];

function populateCountryDropdown(selectEl) {
  if (!selectEl || selectEl.options.length > 0) return;
  COUNTRY_OPTIONS.forEach(function(option, index) {
    var opt = document.createElement('option');
    opt.value = String(index);
    opt.textContent = option.label;
    selectEl.appendChild(opt);
  });
}

function applyCountryCustomAvailability() {
  var selectEl = document.getElementById('owmCountryIndex');
  var customEl = document.getElementById('owmCountryCustom');
  if (!selectEl || !customEl) return;
  var idx = parseInt(selectEl.value, 10);
  if (!isFinite(idx)) idx = 0;
  var isCustom = idx === COUNTRY_OPTIONS.length - 1;
  customEl.disabled = !isCustom;
  customEl.title = isCustom ? '' : 'Selected country already provides its code';
  if (!isCustom) {
    customEl.placeholder = COUNTRY_OPTIONS[idx].code || 'Code';
  } else {
    customEl.placeholder = 'e.g. VN';
  }
}

function applyDataSourceVisibility() {
  var selectEl = document.getElementById('dataSource');
  if (!selectEl) return;
  var value = parseInt(selectEl.value, 10);
  if (!isFinite(value)) value = 0;
  var isOwm = value === 0;
  var isWeatherFlow = value === 1;
  var isNone = value === 2;

  var owmCard = document.getElementById('card-owmap');
  if (owmCard) owmCard.classList.toggle('hidden', !isOwm);
  var wfCard = document.getElementById('card-tempest');
  if (wfCard) wfCard.classList.toggle('hidden', !isWeatherFlow);

  var toggleDisable = function(selector, disabled) {
    var nodes = document.querySelectorAll(selector);
    Array.prototype.forEach.call(nodes, function(el){
      el.disabled = !!disabled;
    });
  };

  toggleDisable('#card-owmap input, #card-owmap select, #card-owmap button', !isOwm);
  toggleDisable('#card-tempest input, #card-tempest select, #card-tempest button', !isWeatherFlow);

  if (isOwm) {
    applyCountryCustomAvailability();
  }
}

function fmtUtc(offsetMin){
  var sign = offsetMin >= 0 ? '+' : '-';
  var m = Math.abs(offsetMin);
  var h = Math.floor(m/60);
  var mi = m % 60;
  return 'UTC' + sign + pad2(h) + ':' + pad2(mi);
}
function formatDate(d, fmt){
  var y=d.getFullYear(), mo=pad2(d.getMonth()+1), da=pad2(d.getDate());
  if (fmt==1) return mo+'/'+da+'/'+y;
  if (fmt==2) return da+'/'+mo+'/'+y;
  return y+'-'+mo+'-'+da;
}
function renderClock(){
  var localMs = (currentEpoch + tzOffset*60) * 1000 + (Date.now() - startMs);
  var d = new Date(localMs);
  var hh = pad2(d.getHours()), mm = pad2(d.getMinutes()), ss = pad2(d.getSeconds());
  var fmtEl = document.getElementById('dateFmt');
  var fmt = fmtEl ? +fmtEl.value : 0;
  var el = document.getElementById('deviceTime');
  if (el) el.textContent = hh+':'+mm+':'+ss+'  '+formatDate(d, fmt);
}

function applyAutoBrightnessUI(){
  var abEl = document.getElementById('autoBrightness');
  var b = document.getElementById('brightness');
  if (!abEl || !b) return;
  var ab = +abEl.value;
  b.disabled = (ab === 1);
  b.title = (ab === 1) ? 'Disabled when Auto Brightness is ON' : '';
}

function applyAutoThemeUI(){
  var modeEl = document.getElementById('autoThemeSchedule');
  var dayEl = document.getElementById('dayThemeStart');
  var nightEl = document.getElementById('nightThemeStart');
  var luxEl = document.getElementById('themeLightThreshold');
  var themeEl = document.getElementById('theme');
  var rowManual = document.querySelectorAll('[data-theme-mode="manual"]');
  var rowScheduled = document.querySelectorAll('[data-theme-mode="scheduled"]');
  var rowAmbient = document.querySelectorAll('[data-theme-mode="ambient"]');
  if (!modeEl) return;
  var modeVal = parseInt(modeEl.value, 10);
  if (!isFinite(modeVal)) modeVal = 0;
  var scheduled = (modeVal === 1);
  var ambient = (modeVal === 2);
  var manual = (modeVal === 0);

  var toggleRows = function(nodes, show){
    Array.prototype.forEach.call(nodes, function(node){
      if (!node) return;
      node.classList.toggle('hidden', !show);
      var input = node.querySelector('input, select');
      if (input) {
        input.disabled = !show;
        input.title = show ? '' : 'Hidden for this Theme Mode';
      }
    });
  };

  toggleRows(rowManual, manual);
  toggleRows(rowScheduled, scheduled);
  toggleRows(rowAmbient, ambient);

  if (dayEl) {
    dayEl.disabled = !scheduled;
    dayEl.title = scheduled ? '' : 'Enable Theme Mode = Scheduled to edit times';
  }
  if (nightEl) {
    nightEl.disabled = !scheduled;
    nightEl.title = scheduled ? '' : 'Enable Theme Mode = Scheduled to edit times';
  }
  if (luxEl) {
    luxEl.disabled = !ambient;
    luxEl.title = ambient ? '' : 'Enable Theme Mode = Light Sensor to edit threshold';
  }
  if (themeEl) {
    themeEl.disabled = !manual;
    themeEl.title = manual ? '' : 'Theme is editable only in Manual mode (still shows current selection)';
  }
}

function applyAutoDstAvailability(){
  var tzSel = document.getElementById('tzSelect');
  var autoDstEl = document.getElementById('autoDst');
  if (!tzSel || !autoDstEl) return;
  var idx = tzSel.selectedIndex;
  var supports = false;
  if (idx >= 0 && idx < tzList.length){
    supports = !!tzList[idx].supportsDst;
  }
  if (!supports){
    autoDstEl.value = '0';
    autoDstEl.disabled = true;
    autoDstEl.title = 'DST not observed in this timezone';
  } else {
    autoDstEl.disabled = false;
    autoDstEl.title = '';
  }
}

function loadIndexStatus(){
  fetch('/status.json')
    .then(function(r){
      if (!r.ok) throw new Error("Status fetch failed");
      return r.json();
    })
    .then(function(st){
      var el;
      el = document.getElementById('st-ssid');
      if (el) el.innerText = st.wifiSSID || '--';
      el = document.getElementById('st-ip');
      if (el) el.innerText = st.ip || '--';
      el = document.getElementById('st-temp');
        if (el) el.innerText = formatTempValue(st.temp);
        el = document.getElementById('st-humd');
        if (el) el.innerText = formatHumidityValue(st.humidity);
      el = document.getElementById('st-time');
      if (el) el.innerText = st.time || '--';
    })
    .catch(function(e){
      console.warn("Status load failed:", e);
    });
}
function loadAll(){
  Promise.all([
    fetch('/settings.json').then(function(r){return r.json();}),
    fetch('/time.json').then(function(r){return r.json();}).catch(function(){return {epoch:0,tzOffset:0,tzName:'Custom',dateFmt:0,ntpServer:'pool.ntp.org'};}),
    fetch('/timezones.json').then(function(r){return r.json();}).catch(function(){return [];})
  ])
  .then(function(results){
    var s = results[0], t = results[1];
    tzList = (results[2] || []).map(function(item){
      if (!item) return item;
      item.supportsDst = !!item.supportsDst;
      return item;
    });

    var wifiSSIDEl = document.getElementById('wifiSSID');
    if (wifiSSIDEl) wifiSSIDEl.value = s.wifiSSID || '';
    var wifiPassEl = document.getElementById('wifiPass');
    if (wifiPassEl) wifiPassEl.value = s.wifiPass || '';
    var dayFormatEl = document.getElementById('dayFormat');
    if (dayFormatEl) dayFormatEl.value = (typeof s.dayFormat !== 'undefined' ? s.dayFormat : 0);
    var dataSourceEl = document.getElementById('dataSource');
    if (dataSourceEl) {
      var dsValue = (typeof s.dataSource !== 'undefined')
        ? s.dataSource
        : (typeof s.forecastSrc !== 'undefined' ? s.forecastSrc : 0);
      dsValue = parseInt(dsValue, 10);
      if (!isFinite(dsValue)) dsValue = 0;
      if (dsValue < 0 || dsValue > 2) dsValue = 0;
      dataSourceEl.value = String(dsValue);
      applyDataSourceVisibility();
    }
    var autoRotateEl = document.getElementById('autoRotate');
    if (autoRotateEl) autoRotateEl.value = (typeof s.autoRotate !== 'undefined' ? s.autoRotate : 1);
    var autoRotateIntervalEl = document.getElementById('autoRotateInterval');
    if (autoRotateIntervalEl) {
      var autoRotateIntervalVal = parseInt(s.autoRotateInterval, 10);
      if (!isFinite(autoRotateIntervalVal)) {
        autoRotateIntervalVal = parseInt(autoRotateIntervalEl.getAttribute('placeholder') || '15', 10);
      }
      if (!isFinite(autoRotateIntervalVal)) autoRotateIntervalVal = 15;
      if (autoRotateIntervalVal < 5) autoRotateIntervalVal = 5;
      if (autoRotateIntervalVal > 300) autoRotateIntervalVal = 300;
      autoRotateIntervalEl.value = autoRotateIntervalVal;
    }
    var manualScreenEl = document.getElementById('manualScreen');
    if (manualScreenEl) manualScreenEl.value = (typeof s.manualScreen !== 'undefined' ? s.manualScreen : 0);

    if (s.units){
      var uTempEl = document.getElementById('uTemp');
      if (uTempEl) uTempEl.value = (typeof s.units.temp !== 'undefined' ? s.units.temp : 0);
      var uWindEl = document.getElementById('uWind');
      if (uWindEl) uWindEl.value = (typeof s.units.wind !== 'undefined' ? s.units.wind : 0);
      var uPressEl = document.getElementById('uPress');
      if (uPressEl) uPressEl.value = (typeof s.units.press !== 'undefined' ? s.units.press : 0);
      var uPrecipEl = document.getElementById('uPrecip');
      if (uPrecipEl) uPrecipEl.value = (typeof s.units.precip !== 'undefined' ? s.units.precip : 0);
      var uClockEl = document.getElementById('uClock');
      if (uClockEl) uClockEl.value = (s.units.clock24h ? 1 : 0);
      alarmClockIs24 = !!s.units.clock24h;
      applyAlarmHourFormat(alarmClockIs24);
  }

  var themeEl = document.getElementById('theme');
  if (themeEl) themeEl.value = (typeof s.theme !== 'undefined' ? s.theme : 0);
  var autoThemeEl = document.getElementById('autoThemeSchedule');
  if (autoThemeEl) {
    var modeVal = (typeof s.autoThemeMode !== 'undefined') ? s.autoThemeMode : (s.autoThemeSchedule ? 1 : 0);
    modeVal = parseInt(modeVal, 10);
    if (!isFinite(modeVal) || modeVal < 0 || modeVal > 2) modeVal = 0;
    autoThemeEl.value = modeVal;
  }
  var dayThemeEl = document.getElementById('dayThemeStart');
  if (dayThemeEl) dayThemeEl.value = minutesToTimeString((typeof s.dayThemeStart !== 'undefined') ? s.dayThemeStart : 360);
  var nightThemeEl = document.getElementById('nightThemeStart');
  if (nightThemeEl) nightThemeEl.value = minutesToTimeString((typeof s.nightThemeStart !== 'undefined') ? s.nightThemeStart : 1200);
  var lightThrEl = document.getElementById('themeLightThreshold');
  if (lightThrEl) {
    var thr = (typeof s.themeLightThreshold !== 'undefined') ? s.themeLightThreshold : 20;
    lightThrEl.value = thr;
  }
  var luxLabel = document.getElementById('currentLuxLabel');
  setCurrentLuxLabel((s.currentLux !== undefined) ? s.currentLux : s.lux);
  var brightnessEl = document.getElementById('brightness');
  if (brightnessEl) brightnessEl.value = (typeof s.brightness !== 'undefined' ? s.brightness : 10);
    var autoBrightnessEl = document.getElementById('autoBrightness');
    if (autoBrightnessEl) autoBrightnessEl.value = (s.autoBrightness ? 1 : 0);
    var splashDurationEl = document.getElementById('splashDuration');
    if (splashDurationEl) splashDurationEl.value = (typeof s.splashDuration !== 'undefined' ? s.splashDuration : 3);
    var scrollLevelEl = document.getElementById('scrollLevel');
    if (scrollLevelEl) scrollLevelEl.value = (typeof s.scrollLevel !== 'undefined' ? s.scrollLevel : 7);
    var customMsgEl = document.getElementById('customMsg');
    if (customMsgEl) customMsgEl.value = (typeof s.customMsg !== 'undefined' ? s.customMsg : '');
    applyAutoBrightnessUI();
    applyAutoThemeUI();

    var owmCityEl = document.getElementById('owmCity');
    if (owmCityEl) owmCityEl.value = s.owmCity || '';
    var owmApiKeyEl = document.getElementById('owmApiKey');
    if (owmApiKeyEl) owmApiKeyEl.value = s.owmApiKey || '';
    var owmCountryIndexEl = document.getElementById('owmCountryIndex');
    if (owmCountryIndexEl) {
      populateCountryDropdown(owmCountryIndexEl);
      var storedIndex = (typeof s.owmCountryIndex !== 'undefined' ? s.owmCountryIndex : 0);
      if (storedIndex < 0 || storedIndex >= COUNTRY_OPTIONS.length) storedIndex = 0;
      owmCountryIndexEl.value = String(storedIndex);
    }
    var owmCountryCustomEl = document.getElementById('owmCountryCustom');
    if (owmCountryCustomEl) owmCountryCustomEl.value = s.owmCountryCustom || '';
    applyCountryCustomAvailability();

    var wfTokenEl = document.getElementById('wfToken');
    if (wfTokenEl) wfTokenEl.value = s.wfToken || '';
    var wfStationIdEl = document.getElementById('wfStationId');
    if (wfStationIdEl) wfStationIdEl.value = s.wfStationId || '';

    var tempOffsetEl = document.getElementById('tempOffset');
    var tempOffsetLabel = document.querySelector('label[for="tempOffset"]');
    var tempUnit = (s.units && typeof s.units.temp !== 'undefined') ? s.units.temp : 0;
    if (tempOffsetLabel) tempOffsetLabel.innerHTML = tempUnit === 1 ? 'Temp Offset (&deg;F)' : 'Temp Offset (&deg;C)';
    if (tempOffsetEl) {
      var displayOffset = (typeof s.tempOffset !== 'undefined') ? Number(s.tempOffset) : 0;
      var minRange = tempUnit === 1 ? (-10 * 9 / 5) : -10;
      var maxRange = tempUnit === 1 ? (10 * 9 / 5) : 10;
      tempOffsetEl.step = '0.1';
      tempOffsetEl.min = minRange.toFixed(1);
      tempOffsetEl.max = maxRange.toFixed(1);
      tempOffsetEl.value = isFinite(displayOffset) ? displayOffset.toFixed(1) : '0.0';
    }
    var humOffsetEl = document.getElementById('humOffset');
    if (humOffsetEl) humOffsetEl.value = (typeof s.humOffset !== 'undefined' ? s.humOffset : 0);
    var lightGainEl = document.getElementById('lightGain');
    if (lightGainEl) lightGainEl.value = (typeof s.lightGain !== 'undefined' ? s.lightGain : 100);
    var buzzVolEl = document.getElementById('buzzerVolume');
    if (buzzVolEl) buzzVolEl.value = (typeof s.buzzerVolume !== 'undefined' ? s.buzzerVolume : 100);
    var buzzToneEl = document.getElementById('buzzerToneSet');
    if (buzzToneEl) buzzToneEl.value = (typeof s.buzzerTone !== 'undefined' ? s.buzzerTone : 0);

    // Alarms
    if (Array.isArray(s.alarms))
    {
      s.alarms.forEach(function(a, idx){
        if (!a) return;
        var i = idx + 1;
        var en = document.getElementById('alarm'+i+'Enabled');
        if (en) en.value = a.enabled ? '1' : '0';
        var hh = document.getElementById('alarm'+i+'Hour');
        var hourVal = (typeof a.hour === 'number') ? a.hour : 0;
        setAlarmHourInputs(i, hourVal, alarmClockIs24);
        var mm = document.getElementById('alarm'+i+'Minute');
        if (mm) mm.value = (typeof a.minute === 'number') ? a.minute : 0;
        var rep = document.getElementById('alarm'+i+'Repeat');
        if (rep) rep.value = (typeof a.repeat === 'number') ? a.repeat : 0;
        var wd = document.getElementById('alarm'+i+'Weekday');
        if (wd) wd.value = (typeof a.weekDay === 'number') ? a.weekDay : 0;
      });
    }
    var alarmSoundEl = document.getElementById('alarmSoundMode');
    if (alarmSoundEl) alarmSoundEl.value = (typeof s.alarmSound !== 'undefined') ? s.alarmSound : 0;
    // NOAA
    if (s.noaa) {
      var nEn = document.getElementById('noaaEnabled');
      if (nEn) nEn.value = s.noaa.enabled ? '1' : '0';
      var nLat = document.getElementById('noaaLat');
      if (nLat) nLat.value = (typeof s.noaa.lat === 'number') ? s.noaa.lat : 0;
      var nLon = document.getElementById('noaaLon');
      if (nLon) nLon.value = (typeof s.noaa.lon === 'number') ? s.noaa.lon : 0;
    }

    currentEpoch = t.epoch || Math.floor(Date.now()/1000);
    tzOffset = (typeof t.tzOffset !== 'undefined' ? t.tzOffset : 0);
    var dfEl = document.getElementById('dateFmt');
    if (dfEl) dfEl.value = (typeof t.dateFmt !== 'undefined' ? t.dateFmt : 0);

    var tzSel = document.getElementById('tzSelect');
    if (tzSel) {
      tzSel.innerHTML = '';
      tzList.forEach(function(z){
        var opt = document.createElement('option');
        opt.value = z.id || '';
        opt.textContent = z.label || (z.city + ' (' + fmtUtc(z.offset) + ')');
        opt.dataset.supportsDst = z.supportsDst ? '1' : '0';
        tzSel.appendChild(opt);
      });
      var tzName = t.tzName || '';
      var idx = -1;
      if (tzName) {
        var lower = tzName.toLowerCase();
        for (var i=0;i<tzList.length;i++){ if ((tzList[i].id || '').toLowerCase() === lower) { idx=i; break; } }
      }
      if (idx < 0) { for (var j=0;j<tzList.length;j++){ if (tzList[j].offset === tzOffset) { idx=j; break; } } }
      if (idx >= 0) tzSel.selectedIndex = idx;
      var tzNameTag = document.getElementById('tzNameTag');
      if (tzNameTag) { tzNameTag.textContent = 'TZ: ' + (t.tzLabel || tzName || 'Custom'); }
      var tzOffsetTag = document.getElementById('tzOffsetTag');
      if (tzOffsetTag) tzOffsetTag.textContent = fmtUtc(tzOffset);
    }
    var autoDstEl = document.getElementById('autoDst');
    if (autoDstEl) {
      autoDstEl.value = (t.tzAutoDst ? '1' : '0');
    }
    applyAutoDstAvailability();

    startMs = Date.now();
    if (tickTimer) clearInterval(tickTimer);
    tickTimer = setInterval(renderClock, 1000);
    renderClock();

    // Refresh lux from status shortly after load to display live value
    setTimeout(refreshCurrentLuxFromStatus, 1000);

    // OTA upload handler
    var otaBtn = document.getElementById('btnUploadOta');
    if (otaBtn) {
      otaBtn.addEventListener('click', uploadOtaFirmware);
    }
  });
}

function setCurrentLuxLabel(value){
  var luxLabel = document.getElementById('currentLuxLabel');
  if (!luxLabel) return;
  var luxVal = Number(value);
  var luxText = (isFinite(luxVal)) ? luxVal.toFixed(1) : '--';
  luxLabel.textContent = 'Current: ' + luxText;
}

function refreshCurrentLuxFromStatus(){
  fetch('/status.json')
    .then(function(r){ return r.ok ? r.json() : null; })
    .then(function(data){
      if (!data) return;
      if (data.lux !== undefined) setCurrentLuxLabel(data.lux);
    })
    .catch(function(){});
}

async function uploadOtaFirmware(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  var fileInput = document.getElementById('otaFile');
  if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
    setMsg('otaMsg', 'Choose a firmware .bin file first.', false);
    return;
  }
  var file = fileInput.files[0];
  setMsg('otaMsg', 'Uploading...', true);
  var btn = document.getElementById('btnUploadOta');
  if (btn) btn.disabled = true;
  var progRow = document.getElementById('otaProgressRow');
  var progBar = document.getElementById('otaProgressBar');
  if (progRow) progRow.style.display = '';
  if (progBar) progBar.style.width = '0%';
  try {
    var res = await fetch('/update', {
      method: 'POST',
      body: (() => {
        // Manually stream to track progress
        var form = new FormData();
        form.append('firmware', file, file.name);
        return form;
      })()
    });
    if (res.body && progBar) {
      // Track progress if browser supports it
      const reader = res.body.getReader();
      let received = 0;
      const total = file.size;
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        received += value.length;
        const pct = Math.min(100, Math.round(received / total * 100));
        progBar.style.width = pct + '%';
      }
    }
    var text = await res.text();
    if (!res.ok) throw new Error(text || 'Upload failed');
    if (progBar) progBar.style.width = '100%';
    setMsg('otaMsg', 'Upload ok. Device will reboot...', true);
    setTimeout(function(){ location.reload(); }, 2000);
  } catch (err) {
    console.error('OTA upload failed', err);
    setMsg('otaMsg', 'OTA failed: ' + (err.message || 'error'), false);
    if (btn) btn.disabled = false;
    if (progRow) progRow.style.display = 'none';
    if (progBar) progBar.style.width = '0%';
  }
}


function readSettingsForm() {
  function byId(id) {
    return document.getElementById(id);
  }
  function getTimeMinutes(id, fallback){
    var el = byId(id);
    var value = el ? el.value : '';
    if (!value && el && el.getAttribute('value')) value = el.getAttribute('value');
    return timeStringToMinutes(value, fallback);
  }

  var autoRotateIntervalEl = byId('autoRotateInterval');
  var autoRotateInterval = parseInt(autoRotateIntervalEl && autoRotateIntervalEl.value, 10);
  if (!isFinite(autoRotateInterval)) autoRotateInterval = parseInt(autoRotateIntervalEl && autoRotateIntervalEl.getAttribute('placeholder') || '15', 10);
  if (!isFinite(autoRotateInterval)) autoRotateInterval = 15;
  autoRotateInterval = Math.min(300, Math.max(5, autoRotateInterval));
  if (autoRotateIntervalEl) autoRotateIntervalEl.value = autoRotateInterval;

  var splashDurationEl = byId('splashDuration');
  var splashDuration = parseInt(splashDurationEl && splashDurationEl.value, 10);
  if (!isFinite(splashDuration)) splashDuration = parseInt(splashDurationEl && splashDurationEl.getAttribute('placeholder') || '3', 10);
  if (!isFinite(splashDuration)) splashDuration = 3;
  splashDuration = Math.min(10, Math.max(1, splashDuration));
  if (splashDurationEl) splashDurationEl.value = splashDuration;

  return {
    wifiSSID: (byId('wifiSSID')?.value || '').trim(),
    wifiPass: byId('wifiPass')?.value || '',
    units: {
      temp:   +(byId('uTemp')?.value ?? 0),
      wind:   +(byId('uWind')?.value ?? 0),
      press:  +(byId('uPress')?.value ?? 0),
      precip: +(byId('uPrecip')?.value ?? 0),
      clock24h: +(byId('uClock')?.value ?? 1) === 1
    },
    dayFormat:   +(byId('dayFormat')?.value ?? 0),
    dataSource:  (function(){
      var val = parseInt(byId('dataSource')?.value, 10);
      if (!isFinite(val)) val = 0;
      if (val < 0 || val > 2) val = 0;
      return val;
    })(),
    autoRotate:  +(byId('autoRotate')?.value ?? 1),
    autoRotateInterval: autoRotateInterval,
    manualScreen:+(byId('manualScreen')?.value ?? 0),
    theme:       +(byId('theme')?.value ?? 0),
    autoThemeMode: +(byId('autoThemeSchedule')?.value ?? 0),
    autoThemeSchedule: (+(byId('autoThemeSchedule')?.value ?? 0) === 1) ? 1 : 0, // legacy fallback for older firmware
    themeLightThreshold: (function(){
      var thr = +(byId('themeLightThreshold')?.value ?? 20);
      if (!isFinite(thr)) thr = 20;
      thr = clamp(thr, 1, 5000);
      var el = byId('themeLightThreshold');
      if (el) el.value = thr;
      return thr;
    })(),
    dayThemeStart: getTimeMinutes('dayThemeStart', 360),
    nightThemeStart: getTimeMinutes('nightThemeStart', 1200),
    brightness:  +(byId('brightness')?.value ?? 10),
    autoBrightness: +(byId('autoBrightness')?.value ?? 0),
    splashDuration: splashDuration,
    scrollLevel: +(byId('scrollLevel')?.value ?? 7),
    customMsg:   byId('customMsg')?.value || '',
    owmCity:          (byId('owmCity')?.value || '').trim(),
    owmCountryIndex:  +(byId('owmCountryIndex')?.value ?? 0),
    owmCountryCustom: (byId('owmCountryCustom')?.value || '').trim(),
    owmApiKey:        (byId('owmApiKey')?.value || '').trim(),
    wfToken:          (byId('wfToken')?.value || '').trim(),
    wfStationId:      (byId('wfStationId')?.value || '').trim(),
    tempOffset:  +(byId('tempOffset')?.value ?? 0),
    humOffset:   +(byId('humOffset')?.value ?? 0),
    lightGain:   (function(){
      var lg = +(byId('lightGain')?.value ?? 100);
      if (!isFinite(lg)) lg = 100;
      lg = clamp(lg, 1, 300);
      var el = byId('lightGain');
      if (el) el.value = lg;
      return lg;
    })(),
    buzzerVolume: +(byId('buzzerVolume')?.value ?? 100),
    buzzerTone:  +(byId('buzzerToneSet')?.value ?? 0),
    alarmSound:  +(byId('alarmSoundMode')?.value ?? 0),
    alarms: [0,1,2].map(function(i){
      var idx = i + 1;
      return {
        enabled: +(byId('alarm'+idx+'Enabled')?.value ?? 0) === 1,
        hour: readAlarmHourFromInputs(idx, alarmClockIs24),
        minute: readAlarmMinute(idx),
        repeat: +(byId('alarm'+idx+'Repeat')?.value ?? 0),
        weekDay: +(byId('alarm'+idx+'Weekday')?.value ?? 0)
      };
    }),
    noaa: {
      enabled: +(byId('noaaEnabled')?.value ?? 0) === 1,
      lat: parseFloat(byId('noaaLat')?.value ?? 0) || 0,
      lon: parseFloat(byId('noaaLon')?.value ?? 0) || 0
    }
  };
}

function pickSettings(all, keys) {
  var out = {};
  keys.forEach(function(key){
    if (Object.prototype.hasOwnProperty.call(all, key)) {
      out[key] = all[key];
    }
  });
  return out;
}

async function submitSettings(payload, msgId) {
  if (!payload || Object.keys(payload).length === 0) {
    setMsg(msgId, 'Nothing to save.', false);
    return false;
  }
  try {
    const res = await fetch('/settings', {
      method: 'POST',
      headers: { 'Content-Type':'application/json' },
      body: JSON.stringify(payload)
    });
    if (!res.ok) {
      const text = await res.text();
      setMsg(msgId, text || 'Save failed.', false);
      return false;
    }
    setMsg(msgId, 'Saved.', true);
    loadAll();
    return true;
  } catch (err) {
    console.error('Save failed', err);
    setMsg(msgId, 'Network error', false);
    return false;
  }
}

async function saveAllSettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = readSettingsForm();
  await submitSettings(payload, 'saveAllMsg');
}

async function saveDeviceSettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), [
    'wifiSSID','wifiPass','dayFormat','dataSource','autoRotate','autoRotateInterval','manualScreen'
  ]);
  await submitSettings(payload, 'saveDeviceMsg');
}

async function saveUnitsSettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), ['units']);
  await submitSettings(payload, 'saveUnitsMsg');
}

async function saveDisplaySettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), [
    'theme','autoThemeMode','themeLightThreshold','dayThemeStart','nightThemeStart','brightness','autoBrightness','splashDuration','scrollLevel','customMsg'
  ]);
  await submitSettings(payload, 'saveDisplayMsg');
}

async function saveOwmSettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), [
    'owmCity','owmCountryIndex','owmCountryCustom','owmApiKey'
  ]);
  await submitSettings(payload, 'saveOWMMsg');
}

async function saveTempestSettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), ['wfToken','wfStationId']);
  await submitSettings(payload, 'saveTempestMsg');
}

async function saveCalibrationSettings(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), ['tempOffset','humOffset','lightGain']);
  await submitSettings(payload, 'saveCalibrationMsg');
}

async function saveAlarmSettingsWeb(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), ['alarms','alarmSound','buzzerVolume','buzzerTone']);
  await submitSettings(payload, 'saveAlarmsMsg');
}

async function saveNoaaSettingsWeb(event){
  if (event && typeof event.preventDefault === 'function') {
    event.preventDefault();
  }
  const payload = pickSettings(readSettingsForm(), ['noaa']);
  await submitSettings(payload, 'saveNoaaMsg');
}

function parseManualToEpoch() {
  var d = document.getElementById('manualDate').value;
  var t = document.getElementById('manualTime').value;
  if (!d || !t) return null;
  var ts = t.length === 5 ? t + ':00' : t;

  // Construct local datetime
  var local = new Date(d + 'T' + ts);

  if (isNaN(local.getTime())) return null;

  // Convert to UTC epoch
  return Math.floor(local.getTime() / 1000);
}

function saveTimeSettings(opts){
  opts = opts || {};
  var withEpoch = opts.withEpoch || false;
  var tzSelect = document.getElementById('tzSelect');
  var tzName = tzSelect ? tzSelect.value : '';
  var dateFmt = +document.getElementById('dateFmt').value;
  var autoDstEl = document.getElementById('autoDst');
  var autoDstEnabled = !!(autoDstEl && !autoDstEl.disabled && autoDstEl.value === '1');
  var body = { tzName: tzName, dateFmt: dateFmt, autoDst: autoDstEnabled };
  if (withEpoch) {
    var epoch = parseManualToEpoch();
    if (!epoch) { setMsg('saveTimeMsg','Invalid manual date/time',false); return; }
    body.epoch = epoch; // always UTC now
  }
  fetch('/time', {
    method:'POST',
    headers: { 'Content-Type':'application/json' },
    body: JSON.stringify(body)
  })
  .then(function(r){
    setMsg('saveTimeMsg', r.ok ? 'Time saved.' : 'Failed to save time.', r.ok);
    if (r.ok) loadAll();
  })
  .catch(function(){
    setMsg('saveTimeMsg','Network error',false);
  });
}


function syncNTP(){
  var custom = document.getElementById('ntpCustom').value.trim();
  var preset = document.getElementById('ntpPreset').value;
  var server = encodeURIComponent(custom || preset);
  fetch('/syncntp?server=' + server)
    .then(function(r){
      setMsg('timeMsg', r.ok ? 'NTP sync requested.' : 'NTP sync failed.', r.ok);
    })
    .catch(function(){
      setMsg('timeMsg','Network error',false);
    });
}

window.addEventListener('load', function(){
  if (document.getElementById('btnSaveAll')) {
    loadAll();
    var btn = document.getElementById('btnSaveAll');
    if (btn) btn.addEventListener('click', saveAllSettings);
    btn = document.getElementById('btnSaveDevice');
    if (btn) btn.addEventListener('click', saveDeviceSettings);
    btn = document.getElementById('btnSaveUnits');
    if (btn) btn.addEventListener('click', saveUnitsSettings);
    btn = document.getElementById('btnSaveDisplay');
    if (btn) btn.addEventListener('click', saveDisplaySettings);
    btn = document.getElementById('btnSaveOWM');
    if (btn) btn.addEventListener('click', saveOwmSettings);
    btn = document.getElementById('btnSaveTempest');
    if (btn) btn.addEventListener('click', saveTempestSettings);
    btn = document.getElementById('btnSaveCalibration');
    if (btn) btn.addEventListener('click', saveCalibrationSettings);
    btn = document.getElementById('btnSaveAlarms');
    if (btn) btn.addEventListener('click', saveAlarmSettingsWeb);
    btn = document.getElementById('btnSaveNoaa');
    if (btn) btn.addEventListener('click', saveNoaaSettingsWeb);
    var clockFormatEl = document.getElementById('uClock');
    if (clockFormatEl) clockFormatEl.addEventListener('change', function(){
      applyAlarmHourFormat(isClock24Selected());
    });
    var autoBrightnessEl = document.getElementById('autoBrightness');
    if (autoBrightnessEl) autoBrightnessEl.addEventListener('change', applyAutoBrightnessUI);
    var autoThemeModeEl = document.getElementById('autoThemeSchedule');
    if (autoThemeModeEl) autoThemeModeEl.addEventListener('change', applyAutoThemeUI);
    var dataSourceEl = document.getElementById('dataSource');
    if (dataSourceEl) dataSourceEl.addEventListener('change', applyDataSourceVisibility);
    btn = document.getElementById('btnSaveTime');
    if (btn) btn.addEventListener('click', function(){saveTimeSettings({withEpoch:false});});
    btn = document.getElementById('btnSetManual');
    if (btn) btn.addEventListener('click', function(){saveTimeSettings({withEpoch:true});});
    btn = document.getElementById('btnSyncNTP');
    if (btn) btn.addEventListener('click', syncNTP);
    var tzSelectEl = document.getElementById('tzSelect');
    if (tzSelectEl) tzSelectEl.addEventListener('change', applyAutoDstAvailability);
    var countrySelectEl = document.getElementById('owmCountryIndex');
    if (countrySelectEl) countrySelectEl.addEventListener('change', applyCountryCustomAvailability);
    applyDataSourceVisibility();
    initWifiScanUI();
  } else {
    loadIndexStatus();
  }
});

// after loadIndexStatus() in script.js
window.addEventListener('load', function(){
  if (!document.getElementById('st-ssid')) {
    return;
  }
  loadIndexStatus();
  setupRemoteControls();
  setInterval(loadIndexStatus, 5000); // refresh every 5s
});

window.addEventListener('load', function(){
  var full = document.getElementById('full-status');
  if (!full) return;
  loadFullStatus();
  setInterval(loadFullStatus, 3000); // refresh every 3s for snappier status page
});





