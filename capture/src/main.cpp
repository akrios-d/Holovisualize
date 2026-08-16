#include "CaptureApp.h"
#include "Pipeline.h"
#include "Sender.h"
#include "sensors/KinectV2Sensor.h"
#define MAKE_SENSOR() std::make_unique<KinectV2Sensor>()

#include <csignal>
#include <cstdlib>

#ifdef HOLOVISUALIZE_OPENCV
#  include "Calibration.h"
#  include "filters/BackgroundSubtractorFilter.h"
#endif
#include "filters/BodyFilter.h"

#include <ixwebsocket/IXHttpClient.h>

#include <array>
#include <iostream>
#include <string>
#include <sstream>

#if defined(_WIN32)
#  include <windows.h>
#endif

#ifdef HOLOVISUALIZE_OPENCV
// Sends the 4x4 calibration matrix to the server via HTTP POST.
static bool sendCalibration(const std::string& host,
                             const std::string& session,
                             const std::string& sensorId,
                             const std::array<float, 16>& transform) {
    std::ostringstream body;
    body << "{\"transform\":[";
    for (int i = 0; i < 16; i++) {
        body << transform[i];
        if (i < 15) body << ",";
    }
    body << "]}";

    std::string url = "http://" + host + "/calibrate?session=" + session
                    + "&sensor=" + sensorId;

    ix::HttpClient client;
    ix::HttpRequestArgsPtr args = client.createRequest();
    args->extraHeaders["Content-Type"] = "application/json";

    auto response = client.post(url, body.str(), args);
    if (response->statusCode != 200) {
        std::cerr << "[Calibration] HTTP POST failed: " << response->statusCode
                  << " " << response->errorMsg << "\n";
        return false;
    }
    std::cout << "[Calibration] Transform sent.\n";
    return true;
}
#endif // HOLOVISUALIZE_OPENCV

// Usage:
//   capture                                                 — GUI mode (default)
//   capture [host] [session] [sensor] [flags]              — GUI pre-populated
//   capture [host] [session] [sensor] --calibrate          — headless calibrate
//   capture [host] [session] [sensor] --headless           — headless stream
//
// Flags:
//   --calibrate          run ArUco calibration and exit
//   --headless           stream without UI
//   --filter=body        body bounding-box filter
//   --filter=background  background subtraction
//   --preview            show OpenCV preview (headless only)
int main(int argc, char* argv[]) {
#if defined(_WIN32)
    // Windows deprioritises background/unfocused windows' CPU (and often GPU)
    // scheduling, which starves USB isochronous servicing and GPU depth
    // decode — this looks like packet loss/slowdown but is really the
    // process just not getting scheduled in time. ABOVE_NORMAL gives a mild
    // edge without starving the rest of the system — HIGH_PRIORITY_CLASS was
    // tried first and made the whole machine stutter/lock up.
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

    const std::string host     = argc > 1 ? argv[1] : "localhost:8080";
    const std::string session  = argc > 2 ? argv[2] : "demo";
    const std::string sensorId = argc > 3 ? argv[3] : "sensor0";

    bool calibrateMode   = false;
    bool headlessMode    = false;
    bool filterBody      = false;
    bool filterBg        = false;

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--calibrate")          calibrateMode = true;
        if (arg == "--headless")           headlessMode  = true;
        if (arg == "--filter=body")        filterBody    = true;
        if (arg == "--filter=background")  filterBg      = true;
    }

    // ── Calibration mode (headless) ───────────────────────────────────────────
    if (calibrateMode) {
#ifdef HOLOVISUALIZE_OPENCV
        std::cout << "Calibration mode — point sensor at ArUco marker (ID 0, 5 cm).\n";

        Pipeline pipeline(MAKE_SENSOR());
        if (!pipeline.initialize()) {
            std::cerr << "Failed to initialise sensor.\n";
            return 1;
        }

        Calibration cal(0, 0.05f);
        Frame frame;
        while (!cal.isCalibrated()) {
            if (!pipeline.sensor().captureFrame(frame)) continue;
            cal.detect(frame);
        }
        pipeline.shutdown();
        return sendCalibration(host, session, sensorId, cal.getTransform()) ? 0 : 1;
#else
        std::cerr << "Calibration requires OpenCV. Rebuild with -DHOLOVISUALIZE_OPENCV=ON.\n";
        return 1;
#endif
    }

    // ── GUI mode (default) ────────────────────────────────────────────────────
    if (!headlessMode) {
        CaptureConfig cfg;
        std::strncpy(cfg.host,     host.c_str(),     sizeof(cfg.host)     - 1);
        std::strncpy(cfg.session,  session.c_str(),  sizeof(cfg.session)  - 1);
        std::strncpy(cfg.sensorId, sensorId.c_str(), sizeof(cfg.sensorId) - 1);
        cfg.filterBody       = filterBody;
        cfg.filterBackground = filterBg;

        CaptureApp app(cfg);
        app.run();
        return 0;
    }

    // ── Headless stream mode ──────────────────────────────────────────────────
    Pipeline pipeline(MAKE_SENSOR());
#ifdef HOLOVISUALIZE_OPENCV
    if (filterBg) pipeline.addFilter(std::make_unique<BackgroundSubtractorFilter>());
#else
    if (filterBg) std::cerr << "[warn] --filter=background requires OpenCV build; ignored.\n";
#endif
    if (filterBody) pipeline.addFilter(std::make_unique<BodyFilter>());

    if (!pipeline.initialize()) {
        std::cerr << "Failed to initialise sensor.\n";
        return 1;
    }

    const std::string wsUrl = "ws://" + host + "/ws?session=" + session
                            + "&role=producer&sensor=" + sensorId;
    Sender sender(wsUrl);
    if (!sender.connect()) {
        std::cerr << "Failed to connect to " << wsUrl << "\n";
        return 1;
    }

    std::cout << "Streaming headless. Press Ctrl+C to stop.\n";
    int frameCount = 0;
    while (true) {
        PointCloud cloud = pipeline.process();
        if (cloud.empty()) continue;
        sender.send(cloud);
        std::cout << "Frame " << ++frameCount << " | points: " << cloud.size() << "\n";
    }

    sender.disconnect();
    pipeline.shutdown();
    return 0;
}
