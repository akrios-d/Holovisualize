#include "Hub.h"
#include "Frame.h"
#include "ISession.h"
#include "SessionModelView.h"

// Effects
#include "effects/FireEffect.h"
#include "effects/LightningEffect.h"
#include "effects/ShockwaveEffect.h"
#include "effects/VortexEffect.h"
#include "effects/ExplosionEffect.h"
#include "effects/IceSpikesEffect.h"
#include "effects/BlackHoleEffect.h"
#include "effects/SpawnedObjectEffect.h"

#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXHttpServer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#  include <windows.h>
#endif

// ─── Global state ─────────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void onSignal(int) { g_running = false; }

// ─── Input validation helpers ─────────────────────────────────────────────────

static bool isValidKey(const std::string& s) {
    if (s.empty() || s.size() > Limits::kMaxSessionKeyLen) return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c))
            && c != '-' && c != '_') return false;
    return true;
}

static bool isValidSensorId(const std::string& s) {
    if (s.empty() || s.size() > Limits::kMaxSensorIdLen) return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c))
            && c != '-' && c != '_') return false;
    return true;
}

static std::string getParam(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos < query.size()) {
        size_t eq  = query.find('=', pos);
        if (eq == std::string::npos) break;
        size_t amp = query.find('&', eq);
        std::string k = query.substr(pos, eq - pos);
        std::string v = query.substr(eq + 1,
                         amp == std::string::npos ? std::string::npos : amp - eq - 1);
        if (k == key) return v;
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

// ─── Calibration JSON parser (no dependency on nlohmann/json) ─────────────────
// Parses {"type":"calibrate","transform":[f0,...,f15]}
// Returns false if malformed or contains NaN/Inf.

static bool parseCalibration(const std::string& text,
                              std::array<float, 16>& out) {
    auto tpos = text.find("\"transform\"");
    if (tpos == std::string::npos) return false;
    auto astart = text.find('[', tpos);
    auto aend   = text.find(']', astart);
    if (astart == std::string::npos || aend == std::string::npos) return false;

    std::string arr = text.substr(astart + 1, aend - astart - 1);
    size_t pos = 0; int idx = 0;
    while (idx < 16 && pos < arr.size()) {
        size_t comma = arr.find(',', pos);
        std::string tok = arr.substr(pos,
                          comma == std::string::npos ? std::string::npos : comma - pos);
        try {
            float v = std::stof(tok);
            if (std::isnan(v) || std::isinf(v)) return false; // reject bad values
            out[idx++] = v;
        } catch (...) { return false; }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return idx == 16;
}

// Decode a GEVT gesture-event frame (see capture/include/GestureWire.h for
// the producer-side encoder — wire layout must stay in sync with it).
static bool decodeGestureEvent(const uint8_t* data, size_t len, GestureEvent& out) {
    if (len < 24) return false;
    if (data[0]!='G'||data[1]!='E'||data[2]!='V'||data[3]!='T') return false;

    uint8_t rawType = data[4];
    // Only the subset of GestureType that HandGesture actually emits is
    // accepted — anything else is rejected rather than trusted blindly.
    switch (static_cast<GestureType>(rawType)) {
        case GestureType::Fist: case GestureType::OpenHand: case GestureType::Pinch:
        case GestureType::ThumbsUp: case GestureType::PointFinger: case GestureType::Peace:
            break;
        default:
            return false;
    }
    out.type = static_cast<GestureType>(rawType);

    float x, y, z, confidence;
    std::memcpy(&x,          data + 8,  4);
    std::memcpy(&y,          data + 12, 4);
    std::memcpy(&z,          data + 16, 4);
    std::memcpy(&confidence, data + 20, 4);
    if (std::isnan(x) || std::isnan(y) || std::isnan(z) || std::isnan(confidence)) return false;

    out.position   = {x, y, z};
    out.confidence = std::clamp(confidence, 0.f, 1.f);
    return true;
}

// Extract a numeric field from a flat JSON object body — no JSON library
// linked, so this is deliberately minimal (matches parseCalibration above).
// Leaves `out` untouched and returns false if the key isn't present/valid.
static bool getJsonFloat(const std::string& text, const std::string& key, float& out) {
    auto kpos = text.find("\"" + key + "\"");
    if (kpos == std::string::npos) return false;
    auto cpos = text.find(':', kpos);
    if (cpos == std::string::npos) return false;

    size_t start = cpos + 1;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
    size_t end = start;
    while (end < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[end])) ||
            text[end] == '-' || text[end] == '.' || text[end] == 'e' || text[end] == 'E' || text[end] == '+'))
        end++;
    if (end == start) return false;

    try {
        float v = std::stof(text.substr(start, end - start));
        if (std::isnan(v) || std::isinf(v)) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}

// ─── Display config persistence (point size, bound box) ───────────────────────
// Survives restarts so the sender doesn't have to re-tune the AR/dashboard
// view every time server.exe is relaunched.

static const char* kConfigPath = "holovisualize_config.json";

static void saveConfig(const Hub& hub) {
    const auto b = hub.boundBox();
    std::ofstream f(kConfigPath, std::ios::trunc);
    if (!f) return;
    f << std::fixed << std::setprecision(4)
      << "{\"pointSize\":" << hub.pointSize()
      << ",\"minX\":" << b.minX << ",\"maxX\":" << b.maxX
      << ",\"minY\":" << b.minY << ",\"maxY\":" << b.maxY
      << ",\"minZ\":" << b.minZ << ",\"maxZ\":" << b.maxZ << "}";
}

static void loadConfig(Hub& hub) {
    std::ifstream f(kConfigPath);
    if (!f) return;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    float v;
    if (getJsonFloat(text, "pointSize", v) && v > 0.001f && v < 1.0f)
        hub.setPointSize(v);

    Hub::BoundBox b = hub.boundBox();
    getJsonFloat(text, "minX", b.minX); getJsonFloat(text, "maxX", b.maxX);
    getJsonFloat(text, "minY", b.minY); getJsonFloat(text, "maxY", b.maxY);
    getJsonFloat(text, "minZ", b.minZ); getJsonFloat(text, "maxZ", b.maxZ);
    hub.setBoundBox(b);

    std::cout << "[main] loaded display config from " << kConfigPath << "\n";
}

// ─── Dashboard ────────────────────────────────────────────────────────────────

static std::string buildStatusJson(const Hub& hub) {
    const auto s = hub.getStats();
    std::ostringstream j;
    j << std::fixed << std::setprecision(1);
    j << R"({"ws_port":)"  << s.wsPort
      << R"(,"kcp_port":)" << s.kcpPort
      << R"(,"voxel_res":)"<< s.voxelRes
      << std::setprecision(3)
      << R"(,"point_size":)" << s.pointSize
      << R"(,"bounds":{"minX":)" << s.bounds.minX << R"(,"maxX":)" << s.bounds.maxX
      << R"(,"minY":)" << s.bounds.minY << R"(,"maxY":)" << s.bounds.maxY
      << R"(,"minZ":)" << s.bounds.minZ << R"(,"maxZ":)" << s.bounds.maxZ << "}"
      << std::setprecision(1)
      << R"(,"uptime_s":)" << s.uptimeS
      << R"(,"sessions":[)";
    for (size_t i = 0; i < s.sessions.size(); ++i) {
        const auto& ss = s.sessions[i];
        if (i) j << ',';
        j << R"({"key":")" << ss.key << '"'
          << R"(,"consumers":)" << ss.consumers
          << R"(,"frame_count":)" << ss.frameCount
          << R"(,"fps":)" << ss.fps
          << R"(,"sensors":[)";
        for (size_t k = 0; k < ss.sensors.size(); ++k) {
            const auto& sn = ss.sensors[k];
            if (k) j << ',';
            j << R"({"id":")" << sn.id << '"'
              << R"(,"points":)" << sn.pointCount
              << R"(,"calibrated":)" << (sn.calibrated ? "true" : "false")
              << '}';
        }
        j << "]}";
    }
    j << "]}";
    return j.str();
}

// Dashboard HTML — self-contained, no external CDN dependencies.
static const char* kDashboardHtml = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Holovisualize</title>
<style>
  :root{--bg:#0d0f14;--card:#161b24;--border:#252d3d;--accent:#3b82f6;--green:#22c55e;--yellow:#eab308;--red:#ef4444;--text:#e2e8f0;--muted:#64748b}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;padding:24px}
  header{display:flex;align-items:center;gap:12px;margin-bottom:28px}
  header h1{font-size:1.4rem;font-weight:700;letter-spacing:.5px}
  header span{background:var(--accent);color:#fff;font-size:.7rem;padding:2px 8px;border-radius:99px;letter-spacing:1px;text-transform:uppercase}
  .meta{display:flex;gap:20px;font-size:.8rem;color:var(--muted);margin-bottom:24px;flex-wrap:wrap}
  .meta b{color:var(--text)}
  .sessions{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:16px}
  .card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:20px}
  .card-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}
  .card-header h2{font-size:.95rem;font-weight:600;font-family:monospace;color:var(--accent)}
  .badge{font-size:.7rem;padding:3px 10px;border-radius:99px;font-weight:600}
  .badge-green{background:#14532d;color:var(--green)}
  .badge-muted{background:#1e293b;color:var(--muted)}
  .stat-row{display:flex;gap:12px;margin-bottom:14px}
  .stat{flex:1;background:#0d1117;border-radius:8px;padding:10px 12px}
  .stat label{font-size:.65rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;display:block;margin-bottom:4px}
  .stat value{font-size:1.2rem;font-weight:700;font-family:monospace}
  .sensor-list{margin-top:4px}
  .sensor{display:flex;align-items:center;gap:8px;padding:7px 10px;background:#0d1117;border-radius:7px;margin-bottom:6px;font-size:.82rem}
  .dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}
  .dot-green{background:var(--green);box-shadow:0 0 6px var(--green)}
  .dot-yellow{background:var(--yellow);box-shadow:0 0 6px var(--yellow)}
  .sensor-id{font-family:monospace;font-weight:600;flex:1}
  .sensor-pts{color:var(--muted);font-size:.75rem}
  .sensor-cal{font-size:.65rem;padding:1px 6px;border-radius:99px}
  .cal-yes{background:#14532d;color:var(--green)}
  .cal-no {background:#1e293b;color:var(--muted)}
  .empty{color:var(--muted);font-size:.85rem;text-align:center;padding:20px}
  .fps-bar{height:3px;background:#1e293b;border-radius:99px;margin-top:14px;overflow:hidden}
  .fps-fill{height:100%;background:var(--accent);border-radius:99px;transition:width .5s}
  footer{margin-top:28px;font-size:.75rem;color:var(--muted);text-align:center}
  .blink{animation:blink 1.5s infinite}
  @keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
</style>
</head>
<body>
<header>
  <h1>Holovisualize</h1>
  <span>Server</span>
  <span id="live" class="blink" style="background:#22c55e">● LIVE</span>
</header>
<div class="meta">
  <div>WebSocket (producers) <b id="ws-port">—</b></div>
  <div>KCP / UDP (consumers) <b id="kcp-port">—</b></div>
  <div>Voxel resolution <b id="vox-res">—</b></div>
  <div>Uptime <b id="uptime">—</b></div>
</div>
<div class="card" style="margin-bottom:16px">
  <div class="card-header">
    <h2>Live view</h2>
    <select id="view-session" style="background:#0d1117;color:var(--text);border:1px solid var(--border);border-radius:6px;padding:4px 8px;font-family:monospace">
      <option value="">— select session —</option>
    </select>
  </div>
  <canvas id="glcanvas" width="1200" height="480" style="width:100%;height:420px;display:block;background:#000;border-radius:8px;cursor:grab"></canvas>
  <div id="view-stats" style="margin-top:8px;font-size:.75rem;color:var(--muted);font-family:monospace">not connected</div>

  <div style="margin-top:12px;display:flex;align-items:center;gap:10px;font-family:monospace;font-size:.75rem">
    <span style="color:var(--muted)">Point size</span>
    <input id="point-size" type="range" min="0.005" max="0.08" step="0.001" value="0.02" style="flex:1">
    <span id="point-size-val" style="color:var(--muted);width:40px">0.020</span>
  </div>

  <div style="margin-top:14px">
    <div style="font-size:.7rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:6px">
      Bound box (visual only — clips this view, doesn't touch the stream)
    </div>
    <div id="bounds-controls" style="display:grid;grid-template-columns:16px 1fr 1fr;gap:6px 10px;align-items:center;font-family:monospace;font-size:.75rem"></div>
  </div>
</div>
<div id="sessions" class="sessions">
  <div class="empty">Waiting for sessions...</div>
</div>
<footer>Auto-refreshes every second &nbsp;·&nbsp; Holovisualize</footer>
<script>
function fmt(n){return n>=1e6?(n/1e6).toFixed(1)+'M':n>=1e3?(n/1e3).toFixed(1)+'K':n}
function fmtUptime(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;
  return h?h+'h '+m+'m':m?m+'m '+ss+'s':ss+'s'}
async function refresh(){
  try{
    const d=await fetch('/api/status').then(r=>r.json());
    document.getElementById('ws-port').textContent=':'+d.ws_port;
    document.getElementById('kcp-port').textContent=':'+d.kcp_port;
    document.getElementById('vox-res').textContent=d.voxel_res+'\xb3';
    document.getElementById('uptime').textContent=fmtUptime(d.uptime_s);
    const el=document.getElementById('sessions');
    if(!d.sessions.length){el.innerHTML='<div class="empty">No active sessions yet.</div>';return;}
    el.innerHTML=d.sessions.map(s=>{
      const fps=Math.min(s.fps,30);
      const fpsPct=Math.round(fps/30*100);
      const sensors=s.sensors.length?s.sensors.map(sn=>`
        <div class="sensor">
          <div class="dot ${sn.points>0?'dot-green':'dot-yellow'}"></div>
          <span class="sensor-id">${sn.id}</span>
          <span class="sensor-pts">${fmt(sn.points)} pts</span>
          <span class="sensor-cal ${sn.calibrated?'cal-yes':'cal-no'}">${sn.calibrated?'calibrated':'uncalibrated'}</span>
        </div>`).join(''):'<div class="sensor"><span style="color:var(--muted)">No sensors connected</span></div>';
      return`<div class="card">
        <div class="card-header">
          <h2>${s.key}</h2>
          <span class="badge ${s.consumers?'badge-green':'badge-muted'}">${s.consumers} viewer${s.consumers!==1?'s':''}</span>
        </div>
        <div class="stat-row">
          <div class="stat"><label>FPS out</label><value>${s.fps.toFixed(1)}</value></div>
          <div class="stat"><label>Frames sent</label><value>${fmt(s.frame_count)}</value></div>
          <div class="stat"><label>Sensors</label><value>${s.sensors.length}</value></div>
        </div>
        <div class="sensor-list">${sensors}</div>
        <div class="fps-bar"><div class="fps-fill" style="width:${fpsPct}%"></div></div>
      </div>`;
    }).join('');
    document.getElementById('live').style.background='#22c55e';
    if(window.__syncSessionPicker) window.__syncSessionPicker(d.sessions.map(s=>s.key), d.ws_port);
  }catch(e){
    document.getElementById('live').style.background='#ef4444';
    document.getElementById('live').textContent='● ERROR';
  }
}
setInterval(refresh,1000);
refresh();
)html" R"html2(

// ─── Live view — WebGL point-cloud viewer ──────────────────────────────────────
// Connects as role=viewer over WebSocket (browsers can't do the raw KCP/UDP
// path capture/preview.exe use) and renders MESH binary frames as points.
(function(){
  const canvas = document.getElementById('glcanvas');
  const statsEl = document.getElementById('view-stats');
  const picker = document.getElementById('view-session');
  const gl = canvas.getContext('webgl');
  if(!gl){ statsEl.textContent='WebGL not available in this browser.'; return; }

  const vsSrc=`
    attribute vec3 aPos;
    attribute vec3 aColor;
    uniform mat4 uMVP;
    uniform vec3 uBoundsMin;
    uniform vec3 uBoundsMax;
    uniform float uPointSize; // world-space (metres) — matches the AR viewer
    uniform float uFocalPx;   // canvas.height/2 * projection[5]
    varying vec3 vColor;
    void main(){
      gl_Position = uMVP * vec4(aPos, 1.0);
      bool inBounds = all(greaterThanEqual(aPos, uBoundsMin)) && all(lessThanEqual(aPos, uBoundsMax));
      gl_PointSize = inBounds ? (uPointSize * uFocalPx / gl_Position.w) : 0.0;
      vColor = aColor;
    }`;
  const fsSrc=`
    precision mediump float;
    varying vec3 vColor;
    void main(){ gl_FragColor = vec4(vColor, 1.0); }`;

  function compile(type, src){
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src); gl.compileShader(sh);
    if(!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) console.error(gl.getShaderInfoLog(sh));
    return sh;
  }
  const prog = gl.createProgram();
  gl.attachShader(prog, compile(gl.VERTEX_SHADER, vsSrc));
  gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fsSrc));
  gl.linkProgram(prog);
  const uMVP = gl.getUniformLocation(prog, 'uMVP');
  const uBoundsMin = gl.getUniformLocation(prog, 'uBoundsMin');
  const uBoundsMax = gl.getUniformLocation(prog, 'uBoundsMax');
  const uPointSize = gl.getUniformLocation(prog, 'uPointSize');
  const uFocalPx = gl.getUniformLocation(prog, 'uFocalPx');
  const aPos = gl.getAttribLocation(prog, 'aPos');
  const aColor = gl.getAttribLocation(prog, 'aColor');

  // Point size + bound box are server-authoritative (see Hub::pointSize_/
  // bounds_) — every viewer (this dashboard, the AR page) reads the same
  // values from /api/status, and this UI writes back via POST /api/config,
  // instead of each viewer keeping its own local-only setting.
  let pointSize = 0.02;
  const bounds = { minX:-100, maxX:100, minY:-100, maxY:100, minZ:-100, maxZ:100 };

  const pointSizeInput = document.getElementById('point-size');
  const pointSizeVal   = document.getElementById('point-size-val');

  let pushTimer = null;
  function pushConfig(){
    clearTimeout(pushTimer);
    pushTimer = setTimeout(() => {
      fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({ pointSize, ...bounds }),
      }).catch(()=>{});
    }, 150);
  }

  pointSizeInput.addEventListener('input', () => {
    pointSize = parseFloat(pointSizeInput.value);
    pointSizeVal.textContent = pointSize.toFixed(3);
    pushConfig();
  });

  const boundsInputs = {};
  (function buildBoundsUI(){
    const el = document.getElementById('bounds-controls');
    const kRange = { x: 5, y: 5, z: 10 }; // slider span in metres
    const axes = [
      ['X', 'minX', 'maxX', -kRange.x, kRange.x],
      ['Y', 'minY', 'maxY', -kRange.y, kRange.y],
      ['Z', 'minZ', 'maxZ', -kRange.z, kRange.z],
    ];
    for (const [label, minKey, maxKey, lo, hi] of axes) {
      const mk = (key) => {
        const input = document.createElement('input');
        input.type = 'range'; input.min = lo; input.max = hi; input.step = 0.05;
        input.value = Math.max(lo, Math.min(hi, bounds[key])); input.style.width = '100%';
        input.addEventListener('input', () => { bounds[key] = parseFloat(input.value); pushConfig(); });
        boundsInputs[key] = input;
        return input;
      };
      const rowLabel = document.createElement('span');
      rowLabel.textContent = label; rowLabel.style.color = 'var(--muted)';
      el.appendChild(rowLabel);
      el.appendChild(mk(minKey));
      el.appendChild(mk(maxKey));
    }
  })();

  // Pull the server's current values once at load, so this UI reflects
  // whatever another viewer (or a previous session) last set.
  fetch('/api/status').then(r=>r.json()).then(d=>{
    if(typeof d.point_size === 'number'){
      pointSize = d.point_size;
      pointSizeInput.value = pointSize;
      pointSizeVal.textContent = pointSize.toFixed(3);
    }
    if(d.bounds){
      Object.assign(bounds, d.bounds);
      for(const k in boundsInputs) boundsInputs[k].value = bounds[k];
    }
  }).catch(()=>{});

  const vbo = gl.createBuffer();
  const colorVbo = gl.createBuffer();
  let pointCount = 0;

  // Orbit camera — same controls as preview.exe (drag to orbit, wheel to zoom).
  let yaw=0, pitch=20, dist=2.5, cx=0, cy=0.8, cz=0;
  let dragging=false, lastX=0, lastY=0;
  canvas.addEventListener('mousedown', e=>{dragging=true; lastX=e.clientX; lastY=e.clientY; canvas.style.cursor='grabbing';});
  window.addEventListener('mouseup', ()=>{dragging=false; canvas.style.cursor='grab';});
  window.addEventListener('mousemove', e=>{
    if(!dragging) return;
    yaw += (e.clientX-lastX)*0.4; pitch += (e.clientY-lastY)*0.4;
    pitch = Math.max(-89, Math.min(89, pitch));
    lastX=e.clientX; lastY=e.clientY;
  });
  canvas.addEventListener('wheel', e=>{ dist = Math.max(0.3, dist + e.deltaY*0.0015); e.preventDefault(); }, {passive:false});
  window.addEventListener('blur', () => { dragging=false; canvas.style.cursor='grab'; });

  // WASD + QE fly movement — moves the orbit centre (cx,cy,cz), mouse still
  // orbits/zooms around it. Ignored while typing in a text field elsewhere
  // on the page.
  const keysDown = new Set();
  const isTyping = () => {
    const t = document.activeElement && document.activeElement.tagName;
    return t === 'INPUT' || t === 'SELECT' || t === 'TEXTAREA';
  };
  window.addEventListener('keydown', e=>{
    if(isTyping()) return;
    const k = e.key.toLowerCase();
    if('wasdqe'.includes(k)){ keysDown.add(k); e.preventDefault(); }
  });
  window.addEventListener('keyup', e=>{ keysDown.delete(e.key.toLowerCase()); });
  // If the window/tab loses focus while a key is physically held, keyup
  // never arrives and the key stays "stuck" — the camera keeps drifting on
  // its own, which also fights with mouse orbiting (the centre keeps
  // moving mid-drag). Drop everything on blur as a fallback.
  window.addEventListener('blur', () => keysDown.clear());
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) keysDown.clear();
  });

  let lastFrameTime = performance.now();
  function applyFlyMovement(){
    const now = performance.now();
    const dt = Math.min((now - lastFrameTime) / 1000, 0.1);
    lastFrameTime = now;
    if(keysDown.size === 0) return;

    const speed = 2.0 * dt; // metres/sec
    const yr = yaw * Math.PI / 180;
    const fx = -Math.sin(yr), fz = -Math.cos(yr); // forward (where the camera looks)
    const rx =  Math.cos(yr), rz = -Math.sin(yr); // strafe right (matches lookAt()'s cross(f,up))

    if(keysDown.has('w')){ cx += fx*speed; cz += fz*speed; }
    if(keysDown.has('s')){ cx -= fx*speed; cz -= fz*speed; }
    if(keysDown.has('d')){ cx += rx*speed; cz += rz*speed; }
    if(keysDown.has('a')){ cx -= rx*speed; cz -= rz*speed; }
    if(keysDown.has('e')){ cy += speed; }
    if(keysDown.has('q')){ cy -= speed; }
  }

  function mat4Mul(a,b){
    const r=new Float32Array(16);
    for(let c=0;c<4;c++)for(let row=0;row<4;row++){let s=0;for(let k=0;k<4;k++)s+=a[k*4+row]*b[c*4+k];r[c*4+row]=s;}
    return r;
  }
  function perspective(fovy,aspect,zn,zf){
    const f=1/Math.tan(fovy/2), r=new Float32Array(16);
    r[0]=f/aspect; r[5]=f; r[10]=(zf+zn)/(zn-zf); r[11]=-1; r[14]=2*zf*zn/(zn-zf);
    return r;
  }
  function lookAt(ex,ey,ez,tx,ty,tz){
    let fx=tx-ex,fy=ty-ey,fz=tz-ez,fl=Math.hypot(fx,fy,fz); fx/=fl;fy/=fl;fz/=fl;
    let rx=fy*0-fz*1, ry=fz*0-fx*0, rz=fx*1-fy*0, rl=Math.hypot(rx,ry,rz); rx/=rl;ry/=rl;rz/=rl;
    let ux=ry*fz-rz*fy, uy=rz*fx-rx*fz, uz=rx*fy-ry*fx;
    return new Float32Array([rx,ux,-fx,0, ry,uy,-fy,0, rz,uz,-fz,0,
      -(rx*ex+ry*ey+rz*ez), -(ux*ex+uy*ey+uz*ez), (fx*ex+fy*ey+fz*ez), 1]);
  }

  function decodeMesh(buf){
    const dv = new DataView(buf);
    if(dv.getUint8(0)!==77||dv.getUint8(1)!==69||dv.getUint8(2)!==83||dv.getUint8(3)!==72) return null; // "MESH"
    const nv = dv.getUint32(4, true);
    const positions = new Float32Array(nv*3);
    // Raw point-cloud frames repurpose the "normal" slot (nx,ny,nz) to carry
    // RGB in 0..1 instead — see SessionModelView::buildFrame().
    const colors = new Float32Array(nv*3);
    let o = 12;
    for(let i=0;i<nv;i++){
      positions[i*3+0] = dv.getFloat32(o, true);
      positions[i*3+1] = dv.getFloat32(o+4, true);
      positions[i*3+2] = dv.getFloat32(o+8, true);
      colors[i*3+0] = dv.getFloat32(o+12, true);
      colors[i*3+1] = dv.getFloat32(o+16, true);
      colors[i*3+2] = dv.getFloat32(o+20, true);
      o += 24;
    }
    return {positions, colors};
  }

  let ws = null, frameCount=0, lastFrameAt=performance.now(), fps=0;

  function connect(sessionKey){
    if(ws){ ws.close(); ws=null; }
    pointCount = 0;
    if(!sessionKey){ statsEl.textContent='not connected'; return; }
    const url = `ws://${location.hostname}:${window.__wsPort}/ws?session=${encodeURIComponent(sessionKey)}&role=viewer`;
    ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    statsEl.textContent = `connecting to ${sessionKey}...`;
    ws.onopen = () => { statsEl.textContent = `${sessionKey} — connected, waiting for frames...`; };
    ws.onclose = () => { statsEl.textContent = `${sessionKey} — disconnected`; };
    ws.onerror = () => { statsEl.textContent = `${sessionKey} — connection error`; };
    ws.onmessage = (ev) => {
      const decoded = decodeMesh(ev.data);
      if(!decoded) return;
      pointCount = decoded.positions.length/3;
      gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
      gl.bufferData(gl.ARRAY_BUFFER, decoded.positions, gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, colorVbo);
      gl.bufferData(gl.ARRAY_BUFFER, decoded.colors, gl.DYNAMIC_DRAW);
      frameCount++;
      const now = performance.now();
      if(now - lastFrameAt > 1000){ fps = frameCount*1000/(now-lastFrameAt); frameCount=0; lastFrameAt=now; }
      statsEl.textContent = `${sessionKey} — ${fps.toFixed(1)} fps — ${pointCount.toLocaleString()} pts`;
    };
  }

  picker.addEventListener('change', () => connect(picker.value));

  // Keep the <select> options in sync with live sessions without
  // clobbering the user's current selection (unlike #sessions, this
  // element is never blown away by refresh()'s innerHTML rebuild).
  window.__syncSessionPicker = function(keys, wsPort){
    window.__wsPort = wsPort;
    const current = picker.value;
    const existing = Array.from(picker.options).map(o=>o.value).slice(1);
    if(existing.length===keys.length && existing.every((k,i)=>k===keys[i])) return;
    picker.innerHTML = '<option value="">— select session —</option>' +
      keys.map(k=>`<option value="${k}">${k}</option>`).join('');
    if(keys.includes(current)) picker.value = current;
  };

  function render(){
    requestAnimationFrame(render);
    applyFlyMovement();
    gl.viewport(0,0,canvas.width,canvas.height);
    gl.clearColor(0,0,0,1);
    gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    if(pointCount===0) return;

    const yr=yaw*Math.PI/180, pr=pitch*Math.PI/180;
    const ex=cx+dist*Math.cos(pr)*Math.sin(yr);
    const ey=cy+dist*Math.sin(pr);
    const ez=cz+dist*Math.cos(pr)*Math.cos(yr);
    const proj = perspective(45*Math.PI/180, canvas.width/canvas.height, 0.05, 50);
    const view = lookAt(ex,ey,ez,cx,cy,cz);
    const mvp = mat4Mul(proj, view);

    gl.useProgram(prog);
    gl.uniformMatrix4fv(uMVP, false, mvp);
    gl.uniform3f(uBoundsMin, bounds.minX, bounds.minY, bounds.minZ);
    gl.uniform3f(uBoundsMax, bounds.maxX, bounds.maxY, bounds.maxZ);
    gl.uniform1f(uPointSize, pointSize);
    gl.uniform1f(uFocalPx, canvas.height * 0.5 * proj[5]);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, colorVbo);
    gl.enableVertexAttribArray(aColor);
    gl.vertexAttribPointer(aColor, 3, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.POINTS, 0, pointCount);
  }
  render();
})();
</script>
</body>
</html>)html2";

static void startDashboard(Hub& hub, int httpPort) {
    ix::HttpServer httpServer(httpPort, "0.0.0.0");

    httpServer.setOnConnectionCallback(
        [&hub](ix::HttpRequestPtr req,
               std::shared_ptr<ix::ConnectionState> /*state*/) -> ix::HttpResponsePtr
    {
        auto resp = std::make_shared<ix::HttpResponse>();
        resp->statusCode = 200;

        if (req->uri == "/api/status" || req->uri.rfind("/api/status?", 0) == 0) {
            resp->headers["Content-Type"]  = "application/json";
            resp->headers["Cache-Control"] = "no-store";
            // Read-only aggregate stats, no session-specific secrets — safe to
            // expose cross-origin so the Vercel-hosted AR viewer can fetch it.
            resp->headers["Access-Control-Allow-Origin"] = "*";
            resp->body = buildStatusJson(hub);
        } else if (req->uri == "/api/config" && req->method == "POST") {
            // Server-authoritative display settings (point size, bound-box
            // clip) — set once from the dashboard, read by every viewer
            // (dashboard's own Live view, the AR page) so they stay in sync.
            float v;
            if (getJsonFloat(req->body, "pointSize", v) && v > 0.001f && v < 1.0f)
                hub.setPointSize(v);

            Hub::BoundBox b = hub.boundBox();
            getJsonFloat(req->body, "minX", b.minX); getJsonFloat(req->body, "maxX", b.maxX);
            getJsonFloat(req->body, "minY", b.minY); getJsonFloat(req->body, "maxY", b.maxY);
            getJsonFloat(req->body, "minZ", b.minZ); getJsonFloat(req->body, "maxZ", b.maxZ);
            hub.setBoundBox(b);
            saveConfig(hub);

            resp->headers["Content-Type"] = "application/json";
            resp->headers["Access-Control-Allow-Origin"] = "*";
            resp->body = buildStatusJson(hub);
        } else {
            // All other paths serve the dashboard HTML.
            resp->headers["Content-Type"]  = "text/html; charset=utf-8";
            resp->headers["Cache-Control"] = "no-store";
            resp->body = kDashboardHtml;
        }
        return resp;
    });

    auto res = httpServer.listen();
    if (!res.first) {
        std::cerr << "[dashboard] HTTP listen failed on port " << httpPort
                  << ": " << res.second << "\n";
        return;
    }
    httpServer.start();
    std::cout << "  dashboard     →  http://0.0.0.0:" << httpPort << "/\n";

    // Block until the process ends — httpServer.start() is non-blocking.
    // The server is kept alive by the caller's thread lifetime.
    httpServer.wait();
}

// ─── 30fps processing loop ────────────────────────────────────────────────────

static void processingLoop(Hub& hub) {
    using namespace std::chrono;
    const auto interval = milliseconds(33);

    while (g_running) {
        auto tick = steady_clock::now();
        try {
            hub.tick();
        } catch (const std::exception& e) {
            std::cerr << "[processingLoop] exception: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[processingLoop] unknown exception\n";
        }
        auto elapsed = steady_clock::now() - tick;
        if (elapsed < interval)
            std::this_thread::sleep_for(interval - elapsed);
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    int wsPort   = 8080; // WebSocket — producers only
    int httpPort = 8082; // HTTP dashboard
    int voxelRes = 128;

    if (argc >= 2) wsPort   = std::stoi(argv[1]);
    if (argc >= 3) voxelRes = std::stoi(argv[2]);

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // Hub owns the UDP/KCP consumer socket (port 8081) and session registry.
    Hub hub(voxelRes);
    hub.setWsPort(wsPort);
    loadConfig(hub);

    // ── Register gesture effects (Socket Pattern) ─────────────────────────────
    // Each factory is called when a new GestureEvent of that type fires.
    // Add or remove mappings here — no other file needs to change.
    hub.onSessionCreated([](SessionModelView& mv) {
        // Single-hand
        mv.registerEffect(GestureType::PalmUp,
            [] { return std::make_unique<FireEffect>(); });
        mv.registerEffect(GestureType::PalmDown,
            [] { return std::make_unique<ShockwaveEffect>(); });
        mv.registerEffect(GestureType::Fist,
            [] { return std::make_unique<IceSpikesEffect>(); });
        mv.registerEffect(GestureType::OpenHand,
            [] { return std::make_unique<ExplosionEffect>(); });
        mv.registerEffect(GestureType::Circle,
            [] { return std::make_unique<VortexEffect>(); });
        mv.registerEffect(GestureType::PointFinger, [] {
            SpawnConfig cfg;
            cfg.shape = SpawnShape::Sphere;
            cfg.color = {100, 200, 255};
            return std::make_unique<SpawnedObjectEffect>(cfg);
        });
        mv.registerEffect(GestureType::ThumbsUp, [] {
            SpawnConfig cfg;
            cfg.shape = SpawnShape::Star;
            cfg.color = {255, 220, 0};
            return std::make_unique<SpawnedObjectEffect>(cfg);
        });
        mv.registerEffect(GestureType::Peace, [] {
            SpawnConfig cfg;
            cfg.shape = SpawnShape::Crystal;
            cfg.color = {180, 100, 255};
            return std::make_unique<SpawnedObjectEffect>(cfg);
        });
        mv.registerEffect(GestureType::Pinch, [] {
            SpawnConfig cfg;
            cfg.shape      = SpawnShape::Heart;
            cfg.color      = {255, 60, 110};
            cfg.gravity    = 0.f;      // float in place
            cfg.spinSpeed  = 0.8f;
            cfg.lifetimeMs = 6000.f;
            return std::make_unique<SpawnedObjectEffect>(cfg);
        });
        // Motion
        mv.registerEffect(GestureType::Push,
            [] { return std::make_unique<ShockwaveEffect>(); });
        mv.registerEffect(GestureType::SwipeLeft,
            [] { return std::make_unique<ExplosionEffect>(); });
        mv.registerEffect(GestureType::SwipeRight,
            [] { return std::make_unique<ExplosionEffect>(); });
        mv.registerEffect(GestureType::Cross,
            [] { return std::make_unique<LightningEffect>(); });
        // Two-hand
        mv.registerEffect(GestureType::Clap,
            [] { return std::make_unique<LightningEffect>(); });
        mv.registerEffect(GestureType::TwoHandsClap,
            [] { return std::make_unique<ShockwaveEffect>(); });
        mv.registerEffect(GestureType::TwoHandsStretch,
            [] { return std::make_unique<BlackHoleEffect>(); });
        mv.registerEffect(GestureType::TwoHandsPinch, [] {
            SpawnConfig cfg;
            cfg.shape = SpawnShape::Torus;
            cfg.color = {0, 255, 180};
            cfg.gravity = 0.f; // float
            return std::make_unique<SpawnedObjectEffect>(cfg);
        });
        // Body
        mv.registerEffect(GestureType::ArmsRaised,
            [] { return std::make_unique<FireEffect>(); });
        mv.registerEffect(GestureType::Jump,
            [] { return std::make_unique<ShockwaveEffect>(); });
        mv.registerEffect(GestureType::Bow, [] {
            SpawnConfig cfg;
            cfg.shape = SpawnShape::Cube;
            cfg.color = {255, 100, 100};
            return std::make_unique<SpawnedObjectEffect>(cfg);
        });
    });

    // ── WebSocket server — producers only ─────────────────────────────────────
    // Consumers connect via KCP/UDP (port 8081), not WebSocket.
    ix::WebSocketServer wsServer(wsPort, "0.0.0.0");
    // Point-cloud/mesh frames are large (~2MB) and mostly-incompressible
    // float data — permessage-deflate (on by default) burns CPU compressing
    // it for basically no size win, and was capping broadcast FPS badly.
    wsServer.disablePerMessageDeflate();

    // Cache session/sensor per connection (URI is only available on Open).
    struct ConnInfo { std::string session; std::string sensor; bool isViewer = false; };
    std::mutex connMu;
    std::unordered_map<std::string, ConnInfo> connMap;

    wsServer.setOnClientMessageCallback(
        [&hub, &connMu, &connMap](std::shared_ptr<ix::ConnectionState> state,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg)
    {
        const std::string connId = state->getId();

        try {

        // ── Open ────────────────────────────────────────────────────────────
        if (msg->type == ix::WebSocketMessageType::Open) {
            // URI and query string are available in openInfo on the Open event.
            const std::string& uri = msg->openInfo.uri;
            std::string query;
            auto qpos = uri.find('?');
            if (qpos != std::string::npos) query = uri.substr(qpos + 1);

            std::string sessionKey = getParam(query, "session");
            std::string sensorId   = getParam(query, "sensor");
            std::string role       = getParam(query, "role");

            // Validate all inputs at the boundary before any logic runs.
            if (!isValidKey(sessionKey)) {
                ws.close(4000, "invalid or missing session");
                return;
            }

            // Browser dashboard viewer — receives MESH frames over this same
            // WS connection instead of KCP (browsers can't do raw UDP).
            if (role == "viewer") {
                { std::lock_guard<std::mutex> lk(connMu);
                  connMap[connId] = {sessionKey, "", true}; }

                hub.addWsViewer(sessionKey, connId,
                    [&ws](const std::vector<uint8_t>& frame) {
                        ws.sendBinary(std::string(
                            reinterpret_cast<const char*>(frame.data()),
                            frame.size()));
                    });

                std::cout << "[" << sessionKey << "] viewer connected\n";
                return;
            }

            if (!isValidSensorId(sensorId)) {
                ws.close(4001, "invalid or missing sensor");
                return;
            }
            // Producers and viewers only. Legacy KCP consumers use UDP 8081.
            if (role != "producer") {
                ws.close(4002, "use role=viewer, or KCP (UDP port 8081)");
                return;
            }

            { std::lock_guard<std::mutex> lk(connMu);
              connMap[connId] = {sessionKey, sensorId}; }

            std::cout << "[" << sessionKey << "] producer connected: "
                      << sensorId << "\n";
        }

        // ── Message ──────────────────────────────────────────────────────────
        else if (msg->type == ix::WebSocketMessageType::Message) {
            std::string sessionKey, sensorId;
            bool isViewer = false;
            { std::lock_guard<std::mutex> lk(connMu);
              auto it = connMap.find(connId);
              if (it == connMap.end()) return;
              sessionKey = it->second.session;
              sensorId   = it->second.sensor;
              isViewer   = it->second.isViewer; }
            if (isViewer) return; // viewers are receive-only
            if (!isValidKey(sessionKey) || !isValidSensorId(sensorId)) return;

            if (!msg->binary) {
                // Text → calibration JSON.
                std::array<float, 16> m{};
                if (parseCalibration(msg->str, m)) {
                    hub.setTransform(sessionKey, sensorId, m);
                }
                // Invalid calibration is silently ignored — no internal details returned.
                return;
            }

            const auto* data = reinterpret_cast<const uint8_t*>(msg->str.data());

            // Binary → GEVT gesture event (from the capture client's
            // MediaPipe sidecar) — checked first since it has its own magic.
            GestureEvent gestureEv;
            if (decodeGestureEvent(data, msg->str.size(), gestureEv)) {
                hub.producerPort(sessionKey).pushGestureEvent(sensorId, gestureEv);
                return;
            }

            // Binary → HOLO point cloud frame.
            PointCloud cloud;
            if (!decodeHolo(data, msg->str.size(), cloud)) return;

            // Enforce max point count (security: prevent memory exhaustion).
            if (cloud.size() > Limits::kMaxPointsPerFrame) {
                std::cerr << "[" << sessionKey << "] frame too large from "
                          << sensorId << " — dropping\n";
                return;
            }

            hub.producerPort(sessionKey).updateCloud(sensorId, std::move(cloud));
        }

        // ── Close ────────────────────────────────────────────────────────────
        else if (msg->type == ix::WebSocketMessageType::Close) {
            std::string sessionKey, sensorId;
            bool isViewer = false;
            { std::lock_guard<std::mutex> lk(connMu);
              auto it = connMap.find(connId);
              if (it != connMap.end()) {
                  sessionKey = it->second.session;
                  sensorId   = it->second.sensor;
                  isViewer   = it->second.isViewer;
                  connMap.erase(it);
              }
            }
            if (sessionKey.empty()) return;

            if (isViewer) {
                hub.removeWsViewer(sessionKey, connId);
                std::cout << "[" << sessionKey << "] viewer disconnected\n";
            } else {
                std::cout << "[" << sessionKey << "] producer disconnected: "
                          << sensorId << "\n";
            }
        }

        } catch (const std::exception& e) {
            std::cerr << "[ws callback] exception for connId=" << connId
                      << ": " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[ws callback] unknown exception for connId=" << connId << "\n";
        }
    });

    auto res = wsServer.listen();
    if (!res.first) {
        // Don't expose the internal error string to callers — log only.
        std::cerr << "[main] WebSocket listen failed: " << res.second << "\n";
        return 1;
    }
    wsServer.start();

    std::cout << "Holovisualize server\n"
              << "  producers  →  ws://0.0.0.0:"  << wsPort  << "/ws"
              << "?session=KEY&role=producer&sensor=ID\n"
              << "  consumers  →  UDP 0.0.0.0:" << Hub::kKcpPort
              << "  (KCP)\n"
              << "  dashboard  →  http://0.0.0.0:" << httpPort << "/\n"
              << "  voxel res  →  " << voxelRes << "\xc2\xb3\n";

    // 30fps processing on a background thread.
    std::thread processor([&hub]{ processingLoop(hub); });

    // HTTP dashboard on its own thread (startDashboard blocks via .wait()).
    std::thread dashThread([&hub, httpPort]{ startDashboard(hub, httpPort); });

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\nShutting down...\n";
    wsServer.stop();
    processor.join();
    dashThread.detach(); // HttpServer::stop() not exposed; let OS clean up on exit
    return 0;
}
