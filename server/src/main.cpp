#include "Hub.h"
#include "Frame.h"
#include "ISession.h"

#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXHttpServer.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

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

// ─── Dashboard ────────────────────────────────────────────────────────────────

static std::string buildStatusJson(const Hub& hub) {
    const auto s = hub.getStats();
    std::ostringstream j;
    j << std::fixed << std::setprecision(1);
    j << R"({"ws_port":)"  << s.wsPort
      << R"(,"kcp_port":)" << s.kcpPort
      << R"(,"voxel_res":)"<< s.voxelRes
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
  }catch(e){
    document.getElementById('live').style.background='#ef4444';
    document.getElementById('live').textContent='● ERROR';
  }
}
setInterval(refresh,1000);
refresh();
</script>
</body>
</html>)html";

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
        hub.tick();
        auto elapsed = steady_clock::now() - tick;
        if (elapsed < interval)
            std::this_thread::sleep_for(interval - elapsed);
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
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

    // ── WebSocket server — producers only ─────────────────────────────────────
    // Consumers connect via KCP/UDP (port 8081), not WebSocket.
    ix::WebSocketServer wsServer(wsPort, "0.0.0.0");

    wsServer.setOnClientMessageCallback(
        [&hub](std::shared_ptr<ix::ConnectionState> state,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg)
    {
        const std::string& uri = state->getUri();
        std::string query;
        auto qpos = uri.find('?');
        if (qpos != std::string::npos) query = uri.substr(qpos + 1);

        std::string sessionKey = getParam(query, "session");
        std::string sensorId   = getParam(query, "sensor");

        // ── Open ────────────────────────────────────────────────────────────
        if (msg->type == ix::WebSocketMessageType::Open) {
            // Validate all inputs at the boundary before any logic runs.
            if (!isValidKey(sessionKey)) {
                ws.close(4000, "invalid or missing session");
                return;
            }
            if (!isValidSensorId(sensorId)) {
                ws.close(4001, "invalid or missing sensor");
                return;
            }
            // This server only accepts producers. Consumers use KCP (UDP 8081).
            std::string role = getParam(query, "role");
            if (role != "producer") {
                ws.close(4002, "consumers must use KCP (UDP port 8081)");
                return;
            }
            std::cout << "[" << sessionKey << "] producer connected: "
                      << sensorId << "\n";
        }

        // ── Message ──────────────────────────────────────────────────────────
        else if (msg->type == ix::WebSocketMessageType::Message) {
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

            // Binary → HOLO point cloud frame.
            const auto* data = reinterpret_cast<const uint8_t*>(msg->str.data());
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
            if (isValidKey(sessionKey) && isValidSensorId(sensorId))
                std::cout << "[" << sessionKey << "] producer disconnected: "
                          << sensorId << "\n";
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
              << "  voxel res  →  " << voxelRes << "\xb3\n";

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
