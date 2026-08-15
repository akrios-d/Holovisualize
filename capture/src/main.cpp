#include "Pipeline.h"
#include "Calibration.h"
#include "Sender.h"
#include "sensors/KinectV2Sensor.h"

#include <ixwebsocket/IXHttpClient.h>

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

// Usage:
//   capture [host:port] [session] [sensor_id]               — stream mode
//   capture [host:port] [session] [sensor_id] --calibrate   — calibrate mode
int main(int argc, char* argv[]) {
    const std::string host     = argc > 1 ? argv[1] : "localhost:8080";
    const std::string session  = argc > 2 ? argv[2] : "demo";
    const std::string sensorId = argc > 3 ? argv[3] : "sensor0";

    bool calibrateMode = false;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--calibrate")
            calibrateMode = true;

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

    std::cout << "Streaming. Press Ctrl+C to stop.\n";

    int frameCount = 0;
    while (true) {
        auto t0 = std::chrono::steady_clock::now();

        PointCloud cloud = pipeline.process();
        if (cloud.empty()) {
            std::cerr << "Capture failed, retrying...\n";
            continue;
        }

        bool ok = sender.send(cloud);

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
