#include "Calibration.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <iostream>
#include <algorithm>

Calibration::Calibration(int markerId, float markerSizeM)
    : markerId_(markerId), markerSizeM_(markerSizeM) {}

bool Calibration::detect(const Frame& frame) {
    if (calibrated_) return true;

    // Wrap the full-res BGRX colour buffer in a cv::Mat and convert to grey.
    cv::Mat bgrx(frame.colorHeight, frame.colorWidth, CV_8UC4,
                 const_cast<uint8_t*>(frame.color.data()));
    cv::Mat grey;
    cv::cvtColor(bgrx, grey, cv::COLOR_BGRA2GRAY);

    // Detect ArUco markers.
    cv::aruco::Dictionary dict =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_250);
    cv::aruco::DetectorParameters params;
    cv::aruco::ArucoDetector detector(dict, params);

    std::vector<int>                         ids;
    std::vector<std::vector<cv::Point2f>>    corners, rejected;
    detector.detectMarkers(grey, corners, ids, rejected);

    // Find the target marker ID.
    auto it = std::find(ids.begin(), ids.end(), markerId_);
    if (it == ids.end()) {
        std::cout << "[Calibration] Marker " << markerId_ << " not found in frame.\n";
        return false;
    }
    int idx = (int)std::distance(ids.begin(), it);

    // 3D corners of the marker in marker space (Z = 0, origin at centre).
    const float h = markerSizeM_ / 2.0f;
    std::vector<cv::Point3f> objPoints = {
        {-h,  h, 0},   // top-left
        { h,  h, 0},   // top-right
        { h, -h, 0},   // bottom-right
        {-h, -h, 0},   // bottom-left
    };

    // Camera matrix from libfreenect2 colour intrinsics.
    const auto& ci = frame.colorIntrinsics;
    cv::Mat K = (cv::Mat_<double>(3, 3)
        << ci.fx,    0, ci.cx,
              0, ci.fy, ci.cy,
              0,     0,    1);
    cv::Mat dist = cv::Mat::zeros(4, 1, CV_64F); // libfreenect2 colour is pre-undistorted

    cv::Vec3d rvec, tvec;
    if (!cv::solvePnP(objPoints, corners[idx], K, dist, rvec, tvec)) {
        std::cerr << "[Calibration] solvePnP failed.\n";
        return false;
    }

    // Build camera-to-world (marker space) transform.
    // solvePnP gives the marker pose in camera space: p_cam = R * p_marker + t
    // We want the inverse: p_world = R^T * p_cam - R^T * t
    cv::Mat R;
    cv::Rodrigues(rvec, R);         // 3x3 double
    cv::Mat Rt  = R.t();
    cv::Mat tVec = (cv::Mat_<double>(3,1) << tvec[0], tvec[1], tvec[2]);
    cv::Mat tWorld = -Rt * tVec;   // translation in world space

    // Pack into row-major 4x4 float array.
    transform_.fill(0.0f);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            transform_[r * 4 + c] = (float)Rt.at<double>(r, c);
    transform_[3]  = (float)tWorld.at<double>(0);
    transform_[7]  = (float)tWorld.at<double>(1);
    transform_[11] = (float)tWorld.at<double>(2);
    transform_[15] = 1.0f;

    calibrated_ = true;
    std::cout << "[Calibration] Done. Translation: ("
              << tWorld.at<double>(0) << ", "
              << tWorld.at<double>(1) << ", "
              << tWorld.at<double>(2) << ") m\n";
    return true;
}
