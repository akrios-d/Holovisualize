#include "Pipeline.h"
#include "Calibration.h"
#include "Sender.h"
#include "SceneFilter.h"
#include "sensors/KinectV2Sensor.h"

#include <ixwebsocket/IXHttpClient.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <iostream>
#include <chrono>
#include <string>
#include <sstream>

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

    std::string url = "http://" + host + "/calibrate?session=" + session + "&sensor=" + sensorId;

    ix::HttpClient client;
    ix::HttpRequestArgsPtr args = client.createRequest();
    args->extraHeaders["Content-Type"] = "application/json";

    auto response = client.post(url, body.str(), args);
    if (response->statusCode != 200) {
        std::cerr << "[Calibration] HTTP POST failed: " << response->statusCode
                  << " " << response->errorMsg << "\n";
        return false;
    }
    std::cout << "[Calibration] Transform sent to server.\n";
    return true;
}

// Renders a live OpenCV preview window showing the colour-aligned-to-depth
// frame and a false-colour depth heatmap.  Returns false if the user pressed
// Q or ESC (caller should stop the loop).
static bool showPreview(const Frame& frame) {
    // ── Colour frame (aligned to depth, BGRX 512×424) ────────────────────────
    if (!frame.colorAligned.empty()) {
        cv::Mat bgrx(424, 512, CV_8UC4,
                     const_cast<uint8_t*>(frame.colorAligned.data()));
        cv::Mat bgr;
        cv::cvtColor(bgrx, bgr, cv::COLOR_BGRA2BGR);
        cv::imshow("Capture — Color (aligned)", bgr);
    }

    // ── Depth frame (float mm → false-colour heatmap) ────────────────────────
    if (!frame.depth.empty()) {
        cv::Mat depthF(424, 512, CV_32FC1,
                       const_cast<float*>(frame.depth.data()));
        cv::Mat depth8;
        // Map [200 mm, 2500 mm] → [0, 255]
        depthF.convertTo(depth8, CV_8UC1, 255.0 / 2300.0, -200.0 * 255.0 / 2300.0);
        cv::Mat heatmap;
        cv::applyColorMap(depth8, heatmap, cv::COLORMAP_JET);
        cv::imshow("Capture — Depth", heatmap);
    }

    int key = cv::waitKey(1);
    return (key != 'q' && key != 'Q' && key != 27 /*ESC*/);
}

// Usage:
//   capture [host:port] [session] [sensor_id] [flags]
//
// Flags:
//   --calibrate          calibrate sensor transform via ArUco marker
//   --preview            show live OpenCV colour+depth windows
//   --filter=body        keep only points inside human-body bounding box
//   --filter=background  subtract static background (captures 30 frames first)
int main(int argc, char* argv[]) {
    const std::string host     = argc > 1 ? argv[1] : "localhost:8080";
    const std::string session  = argc > 2 ? argv[2] : "demo";
    const std::string sensorId = argc > 3 ? argv[3] : "sensor0";

    bool calibrateMode   = false;
    bool previewMode     = false;
    bool filterBody      = false;
    bool filterBg        = false;

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--calibrate")          calibrateMode = true;
        if (arg == "--preview")            previewMode   = true;
        if (arg == "--filter=body")        filterBody    = true;
        if (arg == "--filter=background")  filterBg      = true;
    }

    Pipeline pipeline(std::make_unique<KinectV2Sensor>());

    std::cout << "Initializing sensor...\n";
    if (!pipeline.initialize()) {
        std::cerr << "Failed to initialize sensor.\n";
        return 1;
    }

    // --- Calibration mode ---
    if (calibrateMode) {
        std::cout << "Calibration mode — point the sensor at the ArUco marker (ID 0, 5 cm).\n";

        Calibration cal(/*markerId=*/0, /*markerSizeM=*/0.05f);
        Frame frame;

        while (!cal.isCalibrated()) {
            if (!pipeline.sensor().captureFrame(frame)) continue;
            cal.detect(frame);
        }

        return sendCalibration(host, session, sensorId, cal.getTransform()) ? 0 : 1;
    }

    // --- Stream mode ---
    const std::string wsUrl =
        "ws://" + host + "/ws?session=" + session +
        "&role=producer&sensor=" + sensorId;

    Sender sender(wsUrl);
    std::cout << "Connecting to " << wsUrl << "...\n";
    if (!sender.connect()) {
        std::cerr << "Failed to connect to server.\n";
        return 1;
    }

    // ── Background subtraction setup ──────────────────────────────────────────
    BackgroundSubtractor bgSub;
    if (filterBg) {
        std::cout << "Background subtraction: capturing background — keep the scene empty...\n";
        while (!bgSub.isReady()) {
            Frame tmp;
            if (pipeline.sensor().captureFrame(tmp))
                bgSub.learn(tmp);
        }
        std::cout << "Background model ready.\n";
    }

    if (previewMode)
        std::cout << "Streaming. Press Q or ESC in the preview window to stop.\n";
    else
        std::cout << "Streaming. Press Ctrl+C to stop.\n";

    int frameCount = 0;
    bool running   = true;
    while (running) {
        auto t0 = std::chrono::steady_clock::now();

        PointCloud cloud = pipeline.process();
        if (cloud.empty()) {
            std::cerr << "Capture failed, retrying...\n";
            continue;
        }

        // Apply optional scene filters.
        if (filterBg) {
            // Background subtraction operates on the depth map inside pipeline.
            // We re-run point cloud generation on the filtered frame.
            Frame filtered = pipeline.lastFrame();
            bgSub.apply(filtered);
            cloud = generatePointCloud(filtered);
        }
        if (filterBody)
            ::filterBody(cloud);

        bool ok = sender.send(cloud);

        if (previewMode)
            running = showPreview(pipeline.lastFrame());

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();

        frameCount++;
        std::cout
            << "Frame " << frameCount
            << " | points: " << cloud.size()
            << " | sent: " << (ok ? "ok" : "FAIL")
            << " | " << ms << "ms\n";
    }

    sender.disconnect();
    pipeline.shutdown();
    return 0;
}
