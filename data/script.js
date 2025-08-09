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

var tzList = [];
var currentEpoch = 0;
var tzOffset = 0;
var tickTimer = null;
var startMs = Date.now();

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

function loadIndexStatus(){
  fetch('/status.json')
    .then(function(r){
      if (!r.ok) throw new Error("Status fetch failed");
      return r.json();
    })
    .then(function(st){
      var el;
      el = document.getElementById('st-ssid');
      if (el) el.innerText = st.wifiSSID || '—';
      el = document.getElementById('st-ip');
      if (el) el.innerText = st.ip || '—';
      el = document.getElementById('st-temp');
      if (el) el.innerText = st.temp ? (st.temp + '°') : '—';
      el = document.getElementById('st-humd');
      if (el) el.innerText = st.humidity ? (st.humidity + '%') : '—';
      el = document.getElementById('st-time');
      if (el) el.innerText = st.time || '—';
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
    tzList = results[2];

    if (typeof wifiSSID !== 'undefined') wifiSSID.value = s.wifiSSID || '';
    if (typeof wifiPass !== 'undefined') wifiPass.value = s.wifiPass || '';
    if (typeof dayFormat !== 'undefined') dayFormat.value = (typeof s.dayFormat !== 'undefined' ? s.dayFormat : 0);
    if (typeof forecastSrc !== 'undefined') forecastSrc.value = (typeof s.forecastSrc !== 'undefined' ? s.forecastSrc : 0);
    if (typeof autoRotate !== 'undefined') autoRotate.value = (typeof s.autoRotate !== 'undefined' ? s.autoRotate : 1);
    if (typeof manualScreen !== 'undefined') manualScreen.value = (typeof s.manualScreen !== 'undefined' ? s.manualScreen : 0);

    if (s.units){
      if (typeof uTemp !== 'undefined')   uTemp.value   = (typeof s.units.temp !== 'undefined' ? s.units.temp : 0);
      if (typeof uWind !== 'undefined')   uWind.value   = (typeof s.units.wind !== 'undefined' ? s.units.wind : 0);
      if (typeof uPress !== 'undefined')  uPress.value  = (typeof s.units.press !== 'undefined' ? s.units.press : 0);
      if (typeof uPrecip !== 'undefined') uPrecip.value = (typeof s.units.precip !== 'undefined' ? s.units.precip : 0);
      if (typeof uClock !== 'undefined')  uClock.value  = (s.units.clock24h ? 1 : 0);
    }

    if (typeof theme !== 'undefined')          theme.value        = (typeof s.theme !== 'undefined' ? s.theme : 0);
    if (typeof brightness !== 'undefined')     brightness.value   = (typeof s.brightness !== 'undefined' ? s.brightness : 10);
    if (typeof autoBrightness !== 'undefined') autoBrightness.value = (s.autoBrightness ? 1 : 0);
    if (typeof scrollLevel !== 'undefined')    scrollLevel.value  = (typeof s.scrollLevel !== 'undefined' ? s.scrollLevel : 7);
    if (typeof customMsg !== 'undefined')      customMsg.value    = (typeof s.customMsg !== 'undefined' ? s.customMsg : '');
    applyAutoBrightnessUI();

    if (typeof owmCity !== 'undefined')          owmCity.value          = s.owmCity || '';
    if (typeof owmApiKey !== 'undefined')        owmApiKey.value        = s.owmApiKey || '';
    if (typeof wfToken !== 'undefined')          wfToken.value          = s.wfToken || '';
    if (typeof wfStationId !== 'undefined')      wfStationId.value      = s.wfStationId || '';
    if (typeof owmCountryIndex !== 'undefined')  owmCountryIndex.value  = (typeof s.owmCountryIndex !== 'undefined' ? s.owmCountryIndex : 0);
    if (typeof owmCountryCustom !== 'undefined') owmCountryCustom.value = s.owmCountryCustom || '';

    if (typeof tempOffset !== 'undefined') tempOffset.value = (typeof s.tempOffset !== 'undefined' ? s.tempOffset : 0);
    if (typeof humOffset !== 'undefined')  humOffset.value  = (typeof s.humOffset !== 'undefined' ? s.humOffset : 0);
    if (typeof lightGain !== 'undefined')  lightGain.value  = (typeof s.lightGain !== 'undefined' ? s.lightGain : 100);

    currentEpoch = t.epoch || Math.floor(Date.now()/1000);
    tzOffset = (typeof t.tzOffset !== 'undefined' ? t.tzOffset : 0);
    var dfEl = document.getElementById('dateFmt');
    if (dfEl) dfEl.value = (typeof t.dateFmt !== 'undefined' ? t.dateFmt : 0);

    var tzSel = document.getElementById('tzSelect');
    if (tzSel) {
      tzSel.innerHTML = '';
      tzList.forEach(function(z){
        var opt = document.createElement('option');
        opt.value = z.id;
        opt.textContent = z.label || (z.city + ' (' + fmtUtc(z.offset) + ')');
        tzSel.appendChild(opt);
      });
      var tzName = t.tzName || 'Custom';
      var idx = -1;
      for (var i=0;i<tzList.length;i++){ if (tzList[i].id === tzName) { idx=i; break; } }
      if (idx < 0) { for (var j=0;j<tzList.length;j++){ if (tzList[j].offset === tzOffset) { idx=j; break; } } }
      if (idx >= 0) tzSel.selectedIndex = idx;
      var tzNameTag = document.getElementById('tzNameTag');
      if (tzNameTag) tzNameTag.textContent = 'TZ: ' + tzName;
      var tzOffsetTag = document.getElementById('tzOffsetTag');
      if (tzOffsetTag) tzOffsetTag.textContent = fmtUtc(tzOffset);
    }

    startMs = Date.now();
    if (tickTimer) clearInterval(tickTimer);
    tickTimer = setInterval(renderClock, 1000);
    renderClock();
  });
}

function saveAllSettings(){
  var payload = {
    wifiSSID: wifiSSID.value.trim(),
    wifiPass: wifiPass.value,
    units: {
      temp:   +uTemp.value,
      wind:   +uWind.value,
      press:  +uPress.value,
      precip: +uPrecip.value,
      clock24h: (+uClock.value) === 1
    },
    dayFormat:   +dayFormat.value,
    forecastSrc: +forecastSrc.value,
    autoRotate:  +autoRotate.value,
    manualScreen:+manualScreen.value,
    theme:       +theme.value,
    brightness:  +brightness.value,
    autoBrightness: +autoBrightness.value,
    scrollLevel: +scrollLevel.value,
    customMsg:   customMsg.value,
    owmCity:          owmCity.value,
    owmCountryIndex:  +owmCountryIndex.value,
    owmCountryCustom: owmCountryCustom.value,
    owmApiKey:        owmApiKey.value,
    wfToken:          wfToken.value,
    wfStationId:      wfStationId.value,
    tempOffset:  +tempOffset.value,
    humOffset:   +humOffset.value,
    lightGain:   +lightGain.value
  };
  fetch('/settings', {method:'POST', body: JSON.stringify(payload)})
    .then(function(r){
      setMsg('saveAllMsg', r.ok ? 'Saved.' : 'Save failed.', r.ok);
    })
    .catch(function(){
      setMsg('saveAllMsg', 'Network error', false);
    });

    fetch('/settings', {
    method: 'POST',
    headers: { 'Content-Type':'application/json' },
    body: JSON.stringify(payload)
    });


}

function parseManualToEpoch() {
  var d = document.getElementById('manualDate').value;
  var t = document.getElementById('manualTime').value;
  if (!d || !t) return null;
  var ts = t.length === 5 ? t + ':00' : t;
  var local = new Date(d + 'T' + ts);
  if (isNaN(local.getTime())) return null;
  return Math.floor(local.getTime()/1000);
}

function saveTimeSettings(opts){
  opts = opts || {};
  var withEpoch = opts.withEpoch || false;
  var tzName = document.getElementById('tzSelect').value;
  var dateFmt = +document.getElementById('dateFmt').value;
  var body = { tzName: tzName, dateFmt: dateFmt };
  if (withEpoch) {
    var epoch = parseManualToEpoch();
    if (!epoch) { setMsg('saveTimeMsg','Invalid manual date/time',false); return; }
    body.epoch = epoch;
  }
  fetch('/time', { method:'POST', body: JSON.stringify(body) })
    .then(function(r){
      setMsg('saveTimeMsg', r.ok ? 'Time saved.' : 'Failed to save time.', r.ok);
      if (r.ok) loadAll();
    })
    .catch(function(){
      setMsg('saveTimeMsg','Network error',false);
    });

    fetch('/time', {
    method: 'POST',
    headers: { 'Content-Type':'application/json' },
    body: JSON.stringify(body)
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
    document.getElementById('btnSaveAll').addEventListener('click', saveAllSettings);
    document.getElementById('autoBrightness').addEventListener('change', applyAutoBrightnessUI);
    document.getElementById('btnSaveTime').addEventListener('click', function(){saveTimeSettings({withEpoch:false});});
    document.getElementById('btnSetManual').addEventListener('click', function(){saveTimeSettings({withEpoch:true});});
    document.getElementById('btnSyncNTP').addEventListener('click', syncNTP);
  } else {
    loadIndexStatus();
  }
});

// after loadIndexStatus() in script.js
window.addEventListener('load', function(){
  if (document.getElementById('st-ssid')) {
    loadIndexStatus();
    setInterval(loadIndexStatus, 5000); // refresh every 5s
  } else {
    // settings page
    loadAll();
    document.getElementById('btnSaveAll').addEventListener('click', saveAllSettings);
    document.getElementById('autoBrightness').addEventListener('change', applyAutoBrightnessUI);
    document.getElementById('btnSaveTime').addEventListener('click', function(){saveTimeSettings({withEpoch:false});});
    document.getElementById('btnSetManual').addEventListener('click', function(){saveTimeSettings({withEpoch:true});});
    document.getElementById('btnSyncNTP').addEventListener('click', syncNTP);
  }
});
